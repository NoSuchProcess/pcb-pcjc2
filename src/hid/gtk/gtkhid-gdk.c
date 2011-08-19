/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "crosshair.h"
#include "clip.h"
#include "../hidint.h"
#include "gui.h"
#include "hid/common/draw_helpers.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif


RCSID ("$Id$");


extern HID ghid_hid;

/* Sets priv->u_gc to the "right" GC to use (wrt mask or window)
*/
#define USE_GC(gc) if (!use_gc(gc)) return

static int cur_mask = -1;
static int mask_seq = 0;

typedef struct render_priv {
  GdkGC *bg_gc;
  GdkGC *offlimits_gc;
  GdkGC *mask_gc;
  GdkGC *u_gc;
  GdkGC *grid_gc;
  bool clip;
  GdkRectangle clip_rect;
  int attached_invalidate_depth;
  int mark_invalidate_depth;

  /* Feature for leading the user to a particular location */
  guint lead_user_timeout;
  GTimer *lead_user_timer;
  bool lead_user;
  Coord lead_user_radius;
  Coord lead_user_x;
  Coord lead_user_y;

} render_priv;


typedef struct hid_gc_struct
{
  HID *me_pointer;
  GdkGC *gc;

  gchar *colorname;
  Coord width;
  gint cap, join;
  gchar xor_mask;
  gint mask_seq;
}
hid_gc_struct;


static void draw_lead_user (render_priv *priv);


int
ghid_set_layer (const char *name, int group, int empty)
{
  int idx = group;
  if (idx >= 0 && idx < max_group)
    {
      int n = PCB->LayerGroups.Number[group];
      for (idx = 0; idx < n-1; idx ++)
	{
	  int ni = PCB->LayerGroups.Entries[group][idx];
	  if (ni >= 0 && ni < max_copper_layer + 2
	      && PCB->Data->Layer[ni].On)
	    break;
	}
      idx = PCB->LayerGroups.Entries[group][idx];
    }

  if (idx >= 0 && idx < max_copper_layer + 2)
    return /*pinout ? 1 : */ PCB->Data->Layer[idx].On;
  if (idx < 0)
    {
      switch (SL_TYPE (idx))
	{
	case SL_INVISIBLE:
	  return /* pinout ? 0 : */ PCB->InvisibleObjectsOn;
	case SL_MASK:
	  if (SL_MYSIDE (idx) /*&& !pinout */ )
	    return TEST_FLAG (SHOWMASKFLAG, PCB);
	  return 0;
	case SL_SILK:
	  if (SL_MYSIDE (idx) /*|| pinout */ )
	    return PCB->ElementOn;
	  return 0;
	case SL_ASSY:
	  return 0;
	case SL_PDRILL:
	case SL_UDRILL:
	  return 1;
	case SL_RATS:
	  return PCB->RatOn;
	}
    }
  return 0;
}

void
ghid_destroy_gc (hidGC gc)
{
  if (gc->gc)
    g_object_unref (gc->gc);
  g_free (gc);
}

hidGC
ghid_make_gc (void)
{
  hidGC rv;

  rv = g_new0 (hid_gc_struct, 1);
  rv->me_pointer = &ghid_hid;
  rv->colorname = Settings.BackgroundColor;
  return rv;
}

static void
set_clip (render_priv *priv, GdkGC *gc)
{
  if (gc == NULL)
    return;

  if (priv->clip)
    gdk_gc_set_clip_rectangle (gc, &priv->clip_rect);
  else
    gdk_gc_set_clip_mask (gc, NULL);
}

static void
ghid_draw_grid (void)
{
  static GdkPoint *points = 0;
  static int npoints = 0;
  Coord x1, y1, x2, y2, x, y;
  int n, i;
  render_priv *priv = gport->render_priv;

  if (!Settings.DrawGrid)
    return;
  if (Vz (PCB->Grid) < MIN_GRID_DISTANCE)
    return;
  if (!priv->grid_gc)
    {
      if (gdk_color_parse (Settings.GridColor, &gport->grid_color))
	{
	  gport->grid_color.red ^= gport->bg_color.red;
	  gport->grid_color.green ^= gport->bg_color.green;
	  gport->grid_color.blue ^= gport->bg_color.blue;
	  gdk_color_alloc (gport->colormap, &gport->grid_color);
	}
      priv->grid_gc = gdk_gc_new (gport->drawable);
      gdk_gc_set_function (priv->grid_gc, GDK_XOR);
      gdk_gc_set_foreground (priv->grid_gc, &gport->grid_color);
      gdk_gc_set_clip_origin (priv->grid_gc, 0, 0);
      set_clip (priv, priv->grid_gc);
    }
  x1 = GridFit (SIDE_X (gport->view_x0), PCB->Grid, PCB->GridOffsetX);
  y1 = GridFit (SIDE_Y (gport->view_y0), PCB->Grid, PCB->GridOffsetY);
  x2 = GridFit (SIDE_X (gport->view_x0 + gport->view_width - 1),
                PCB->Grid, PCB->GridOffsetX);
  y2 = GridFit (SIDE_Y (gport->view_y0 + gport->view_height - 1),
                PCB->Grid, PCB->GridOffsetY);
  if (x1 > x2)
    {
      Coord tmp = x1;
      x1 = x2;
      x2 = tmp;
    }
  if (y1 > y2)
    {
      Coord tmp = y1;
      y1 = y2;
      y2 = tmp;
    }
  if (Vx (x1) < 0)
    x1 += PCB->Grid;
  if (Vy (y1) < 0)
    y1 += PCB->Grid;
  if (Vx (x2) >= gport->width)
    x2 -= PCB->Grid;
  if (Vy (y2) >= gport->height)
    y2 -= PCB->Grid;
  n = (x2 - x1) / PCB->Grid + 1;
  if (n > npoints)
    {
      npoints = n + 10;
      points = (GdkPoint *)realloc (points, npoints * sizeof (GdkPoint));
    }
  n = 0;
  for (x = x1; x <= x2; x += PCB->Grid)
    {
      points[n].x = Vx (x);
      n++;
    }
  if (n == 0)
    return;
  for (y = y1; y <= y2; y += PCB->Grid)
    {
      for (i = 0; i < n; i++)
	points[i].y = Vy (y);
      gdk_draw_points (gport->drawable, priv->grid_gc, points, n);
    }
}

/* ------------------------------------------------------------ */
static void
ghid_draw_bg_image (void)
{
  static GdkPixbuf *pixbuf;
  GdkInterpType interp_type;
  gint x, y, w, h, w_src, h_src;
  static gint w_scaled, h_scaled;
  render_priv *priv = gport->render_priv;

  if (!ghidgui->bg_pixbuf)
    return;

  w = PCB->MaxWidth / gport->zoom;
  h = PCB->MaxHeight / gport->zoom;
  x = gport->view_x0 / gport->zoom;
  y = gport->view_y0 / gport->zoom;

  if (w_scaled != w || h_scaled != h)
    {
      if (pixbuf)
	g_object_unref (G_OBJECT (pixbuf));

      w_src = gdk_pixbuf_get_width (ghidgui->bg_pixbuf);
      h_src = gdk_pixbuf_get_height (ghidgui->bg_pixbuf);
      if (w > w_src && h > h_src)
	interp_type = GDK_INTERP_NEAREST;
      else
	interp_type = GDK_INTERP_BILINEAR;

      pixbuf =
	gdk_pixbuf_scale_simple (ghidgui->bg_pixbuf, w, h, interp_type);
      w_scaled = w;
      h_scaled = h;
    }
  if (pixbuf)
    gdk_pixbuf_render_to_drawable (pixbuf, gport->drawable, priv->bg_gc,
				   x, y, 0, 0,
				   w - x, h - y, GDK_RGB_DITHER_NORMAL, 0, 0);
}

#define WHICH_GC(gc) (cur_mask == HID_MASK_CLEAR ? priv->mask_gc : (gc)->gc)

void
ghid_use_mask (int use_it)
{
  static int mask_seq_id = 0;
  GdkColor color;
  render_priv *priv = gport->render_priv;

  if (!gport->pixmap)
    return;
  if (use_it == cur_mask)
    return;
  switch (use_it)
    {
    case HID_MASK_OFF:
      gport->drawable = gport->pixmap;
      mask_seq = 0;
      break;

    case HID_MASK_BEFORE:
      /* The HID asks not to receive this mask type, so warn if we get it */
      g_return_if_reached ();

    case HID_MASK_CLEAR:
      if (!gport->mask)
	gport->mask = gdk_pixmap_new (0, gport->width, gport->height, 1);
      gport->drawable = gport->mask;
      mask_seq = 0;
      if (!priv->mask_gc)
	{
	  priv->mask_gc = gdk_gc_new (gport->drawable);
	  gdk_gc_set_clip_origin (priv->mask_gc, 0, 0);
	  set_clip (priv, priv->mask_gc);
	}
      color.pixel = 1;
      gdk_gc_set_foreground (priv->mask_gc, &color);
      gdk_draw_rectangle (gport->drawable, priv->mask_gc, TRUE, 0, 0,
			  gport->width, gport->height);
      color.pixel = 0;
      gdk_gc_set_foreground (priv->mask_gc, &color);
      break;

    case HID_MASK_AFTER:
      mask_seq_id++;
      if (!mask_seq_id)
	mask_seq_id = 1;
      mask_seq = mask_seq_id;

      gport->drawable = gport->pixmap;
      break;

    }
  cur_mask = use_it;
}


typedef struct
{
  int color_set;
  GdkColor color;
  int xor_set;
  GdkColor xor_color;
} ColorCache;


  /* Config helper functions for when the user changes color preferences.
     |  set_special colors used in the gtkhid.
   */
static void
set_special_grid_color (void)
{
  render_priv *priv = gport->render_priv;

  if (!gport->colormap)
    return;
  gport->grid_color.red ^= gport->bg_color.red;
  gport->grid_color.green ^= gport->bg_color.green;
  gport->grid_color.blue ^= gport->bg_color.blue;
  gdk_color_alloc (gport->colormap, &gport->grid_color);
  if (priv->grid_gc)
    gdk_gc_set_foreground (priv->grid_gc, &gport->grid_color);
}

void
ghid_set_special_colors (HID_Attribute * ha)
{
  render_priv *priv = gport->render_priv;

  if (!ha->name || !ha->value)
    return;
  if (!strcmp (ha->name, "background-color") && priv->bg_gc)
    {
      ghid_map_color_string (*(char **) ha->value, &gport->bg_color);
      gdk_gc_set_foreground (priv->bg_gc, &gport->bg_color);
      set_special_grid_color ();
    }
  else if (!strcmp (ha->name, "off-limit-color") && priv->offlimits_gc)
    {
      ghid_map_color_string (*(char **) ha->value, &gport->offlimits_color);
      gdk_gc_set_foreground (priv->offlimits_gc, &gport->offlimits_color);
    }
  else if (!strcmp (ha->name, "grid-color") && priv->grid_gc)
    {
      ghid_map_color_string (*(char **) ha->value, &gport->grid_color);
      set_special_grid_color ();
    }
}

void
ghid_set_color (hidGC gc, const char *name)
{
  static void *cache = 0;
  hidval cval;

  if (name == NULL)
    {
      fprintf (stderr, "%s():  name = NULL, setting to magenta\n",
	       __FUNCTION__);
      name = "magenta";
    }

  gc->colorname = (char *) name;
  if (!gc->gc)
    return;
  if (gport->colormap == 0)
    gport->colormap = gtk_widget_get_colormap (gport->top_window);

  if (strcmp (name, "erase") == 0)
    {
      gdk_gc_set_foreground (gc->gc, &gport->bg_color);
    }
  else if (strcmp (name, "drill") == 0)
    {
      gdk_gc_set_foreground (gc->gc, &gport->offlimits_color);
    }
  else
    {
      ColorCache *cc;
      if (hid_cache_color (0, name, &cval, &cache))
	cc = (ColorCache *) cval.ptr;
      else
	{
	  cc = (ColorCache *) malloc (sizeof (ColorCache));
	  memset (cc, 0, sizeof (*cc));
	  cval.ptr = cc;
	  hid_cache_color (1, name, &cval, &cache);
	}

      if (!cc->color_set)
	{
	  if (gdk_color_parse (name, &cc->color))
	    gdk_color_alloc (gport->colormap, &cc->color);
	  else
	    gdk_color_white (gport->colormap, &cc->color);
	  cc->color_set = 1;
	}
      if (gc->xor_mask)
	{
	  if (!cc->xor_set)
	    {
	      cc->xor_color.red = cc->color.red ^ gport->bg_color.red;
	      cc->xor_color.green = cc->color.green ^ gport->bg_color.green;
	      cc->xor_color.blue = cc->color.blue ^ gport->bg_color.blue;
	      gdk_color_alloc (gport->colormap, &cc->xor_color);
	      cc->xor_set = 1;
	    }
	  gdk_gc_set_foreground (gc->gc, &cc->xor_color);
	}
      else
	{
	  gdk_gc_set_foreground (gc->gc, &cc->color);
	}
    }
}

void
ghid_set_line_cap (hidGC gc, EndCapStyle style)
{
  render_priv *priv = gport->render_priv;

  switch (style)
    {
    case Trace_Cap:
    case Round_Cap:
      gc->cap = GDK_CAP_ROUND;
      gc->join = GDK_JOIN_ROUND;
      break;
    case Square_Cap:
    case Beveled_Cap:
      gc->cap = GDK_CAP_PROJECTING;
      gc->join = GDK_JOIN_MITER;
      break;
    }
  if (gc->gc)
    gdk_gc_set_line_attributes (WHICH_GC (gc),
				Vz (gc->width), GDK_LINE_SOLID,
				(GdkCapStyle)gc->cap, (GdkJoinStyle)gc->join);
}

void
ghid_set_line_width (hidGC gc, Coord width)
{
  render_priv *priv = gport->render_priv;

  gc->width = width;
  if (gc->gc)
    gdk_gc_set_line_attributes (WHICH_GC (gc),
				Vz (gc->width), GDK_LINE_SOLID,
				(GdkCapStyle)gc->cap, (GdkJoinStyle)gc->join);
}

void
ghid_set_draw_xor (hidGC gc, int xor_mask)
{
  gc->xor_mask = xor_mask;
  if (!gc->gc)
    return;
  gdk_gc_set_function (gc->gc, xor_mask ? GDK_XOR : GDK_COPY);
  ghid_set_color (gc, gc->colorname);
}

static int
use_gc (hidGC gc)
{
  render_priv *priv = gport->render_priv;

  if (gc->me_pointer != &ghid_hid)
    {
      fprintf (stderr, "Fatal: GC from another HID passed to GTK HID\n");
      abort ();
    }

  if (!gport->pixmap)
    return 0;
  if (!gc->gc)
    {
      gc->gc = gdk_gc_new (gport->top_window->window);
      ghid_set_color (gc, gc->colorname);
      ghid_set_line_width (gc, gc->width);
      ghid_set_line_cap (gc, (EndCapStyle)gc->cap);
      ghid_set_draw_xor (gc, gc->xor_mask);
      gdk_gc_set_clip_origin (gc->gc, 0, 0);
    }
  if (gc->mask_seq != mask_seq)
    {
      if (mask_seq)
	gdk_gc_set_clip_mask (gc->gc, gport->mask);
      else
	set_clip (priv, gc->gc);
      gc->mask_seq = mask_seq;
    }
  priv->u_gc = WHICH_GC (gc);
  return 1;
}

void
ghid_draw_line (hidGC gc, Coord x1, Coord y1, Coord x2, Coord y2)
{
  double dx1, dy1, dx2, dy2;
  render_priv *priv = gport->render_priv;

  dx1 = Vx ((double) x1);
  dy1 = Vy ((double) y1);
  dx2 = Vx ((double) x2);
  dy2 = Vy ((double) y2);

  if (!ClipLine (0, 0, gport->width, gport->height,
		 &dx1, &dy1, &dx2, &dy2, gc->width / gport->zoom))
    return;

  USE_GC (gc);
  gdk_draw_line (gport->drawable, priv->u_gc, dx1, dy1, dx2, dy2);
}

void
ghid_draw_arc (hidGC gc, Coord cx, Coord cy,
	       Coord xradius, Coord yradius, Angle start_angle, Angle delta_angle)
{
  gint vrx, vry;
  gint w, h, radius;
  render_priv *priv = gport->render_priv;

  w = gport->width * gport->zoom;
  h = gport->height * gport->zoom;
  radius = (xradius > yradius) ? xradius : yradius;
  if (SIDE_X (cx) < gport->view_x0 - radius
      || SIDE_X (cx) > gport->view_x0 + w + radius
      || SIDE_Y (cy) < gport->view_y0 - radius
      || SIDE_Y (cy) > gport->view_y0 + h + radius)
    return;

  USE_GC (gc);
  vrx = Vz (xradius);
  vry = Vz (yradius);

  if (ghid_flip_x)
    {
      start_angle = 180 - start_angle;
      delta_angle = -delta_angle;
    }
  if (ghid_flip_y)
    {
      start_angle = -start_angle;
      delta_angle = -delta_angle;
    }
  /* make sure we fall in the -180 to +180 range */
  start_angle = NormalizeAngle (start_angle);
  if (start_angle >= 180)  start_angle -= 360;

  gdk_draw_arc (gport->drawable, priv->u_gc, 0,
		Vx (cx) - vrx, Vy (cy) - vry,
		vrx * 2, vry * 2, (start_angle + 180) * 64, delta_angle * 64);
}

void
ghid_draw_rect (hidGC gc, Coord x1, Coord y1, Coord x2, Coord y2)
{
  gint w, h, lw;
  render_priv *priv = gport->render_priv;

  lw = gc->width;
  w = gport->width * gport->zoom;
  h = gport->height * gport->zoom;

  if ((SIDE_X (x1) < gport->view_x0 - lw
       && SIDE_X (x2) < gport->view_x0 - lw)
      || (SIDE_X (x1) > gport->view_x0 + w + lw
	  && SIDE_X (x2) > gport->view_x0 + w + lw)
      || (SIDE_Y (y1) < gport->view_y0 - lw
	  && SIDE_Y (y2) < gport->view_y0 - lw)
      || (SIDE_Y (y1) > gport->view_y0 + h + lw
	  && SIDE_Y (y2) > gport->view_y0 + h + lw))
    return;

  x1 = Vx (x1);
  y1 = Vy (y1);
  x2 = Vx (x2);
  y2 = Vy (y2);

  if (x1 > x2)
    {
      gint xt = x1;
      x1 = x2;
      x2 = xt;
    }
  if (y1 > y2)
    {
      gint yt = y1;
      y1 = y2;
      y2 = yt;
    }

  USE_GC (gc);
  gdk_draw_rectangle (gport->drawable, priv->u_gc, FALSE,
		      x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}


void
ghid_fill_circle (hidGC gc, Coord cx, Coord cy, Coord radius)
{
  gint w, h, vr;
  render_priv *priv = gport->render_priv;

  w = gport->width * gport->zoom;
  h = gport->height * gport->zoom;
  if (SIDE_X (cx) < gport->view_x0 - radius
      || SIDE_X (cx) > gport->view_x0 + w + radius
      || SIDE_Y (cy) < gport->view_y0 - radius
      || SIDE_Y (cy) > gport->view_y0 + h + radius)
    return;

  USE_GC (gc);
  vr = Vz (radius);
  gdk_draw_arc (gport->drawable, priv->u_gc, TRUE,
		Vx (cx) - vr, Vy (cy) - vr, vr * 2, vr * 2, 0, 360 * 64);
}

void
ghid_fill_polygon (hidGC gc, int n_coords, Coord *x, Coord *y)
{
  static GdkPoint *points = 0;
  static int npoints = 0;
  int i;
  render_priv *priv = gport->render_priv;
  USE_GC (gc);

  if (npoints < n_coords)
    {
      npoints = n_coords + 1;
      points = (GdkPoint *)realloc (points, npoints * sizeof (GdkPoint));
    }
  for (i = 0; i < n_coords; i++)
    {
      points[i].x = Vx (x[i]);
      points[i].y = Vy (y[i]);
    }
  gdk_draw_polygon (gport->drawable, priv->u_gc, 1, points, n_coords);
}

void
ghid_fill_rect (hidGC gc, Coord x1, Coord y1, Coord x2, Coord y2)
{
  gint w, h, lw, xx, yy;
  render_priv *priv = gport->render_priv;

  lw = gc->width;
  w = gport->width * gport->zoom;
  h = gport->height * gport->zoom;

  if ((SIDE_X (x1) < gport->view_x0 - lw
       && SIDE_X (x2) < gport->view_x0 - lw)
      || (SIDE_X (x1) > gport->view_x0 + w + lw
	  && SIDE_X (x2) > gport->view_x0 + w + lw)
      || (SIDE_Y (y1) < gport->view_y0 - lw
	  && SIDE_Y (y2) < gport->view_y0 - lw)
      || (SIDE_Y (y1) > gport->view_y0 + h + lw
	  && SIDE_Y (y2) > gport->view_y0 + h + lw))
    return;

  x1 = Vx (x1);
  y1 = Vy (y1);
  x2 = Vx (x2);
  y2 = Vy (y2);
  if (x2 < x1)
    {
      xx = x1;
      x1 = x2;
      x2 = xx;
    }
  if (y2 < y1)
    {
      yy = y1;
      y1 = y2;
      y2 = yy;
    }
  USE_GC (gc);
  gdk_draw_rectangle (gport->drawable, priv->u_gc, TRUE,
		      x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}

static void
redraw_region (GdkRectangle *rect)
{
  int eleft, eright, etop, ebottom;
  BoxType region;
  render_priv *priv = gport->render_priv;

  if (!gport->pixmap)
    return;

  if (rect != NULL)
    {
      priv->clip_rect = *rect;
      priv->clip = true;
    }
  else
    {
      priv->clip_rect.x = 0;
      priv->clip_rect.y = 0;
      priv->clip_rect.width = gport->width;
      priv->clip_rect.height = gport->height;
      priv->clip = false;
    }

  set_clip (priv, priv->bg_gc);
  set_clip (priv, priv->offlimits_gc);
  set_clip (priv, priv->mask_gc);
  set_clip (priv, priv->grid_gc);

  region.X1 = MIN(Px(priv->clip_rect.x),
                  Px(priv->clip_rect.x + priv->clip_rect.width + 1));
  region.Y1 = MIN(Py(priv->clip_rect.y),
                  Py(priv->clip_rect.y + priv->clip_rect.height + 1));
  region.X2 = MAX(Px(priv->clip_rect.x),
                  Px(priv->clip_rect.x + priv->clip_rect.width + 1));
  region.Y2 = MAX(Py(priv->clip_rect.y),
                  Py(priv->clip_rect.y + priv->clip_rect.height + 1));

  region.X1 = MAX (0, MIN (PCB->MaxWidth,  region.X1));
  region.X2 = MAX (0, MIN (PCB->MaxWidth,  region.X2));
  region.Y1 = MAX (0, MIN (PCB->MaxHeight, region.Y1));
  region.Y2 = MAX (0, MIN (PCB->MaxHeight, region.Y2));

  eleft = Vx (0);
  eright = Vx (PCB->MaxWidth);
  etop = Vy (0);
  ebottom = Vy (PCB->MaxHeight);
  if (eleft > eright)
    {
      int tmp = eleft;
      eleft = eright;
      eright = tmp;
    }
  if (etop > ebottom)
    {
      int tmp = etop;
      etop = ebottom;
      ebottom = tmp;
    }

  if (eleft > 0)
    gdk_draw_rectangle (gport->drawable, priv->offlimits_gc,
                        1, 0, 0, eleft, gport->height);
  else
    eleft = 0;
  if (eright < gport->width)
    gdk_draw_rectangle (gport->drawable, priv->offlimits_gc,
                        1, eright, 0, gport->width - eright, gport->height);
  else
    eright = gport->width;
  if (etop > 0)
    gdk_draw_rectangle (gport->drawable, priv->offlimits_gc,
                        1, eleft, 0, eright - eleft + 1, etop);
  else
    etop = 0;
  if (ebottom < gport->height)
    gdk_draw_rectangle (gport->drawable, priv->offlimits_gc,
                        1, eleft, ebottom, eright - eleft + 1,
                        gport->height - ebottom);
  else
    ebottom = gport->height;

  gdk_draw_rectangle (gport->drawable, priv->bg_gc, 1,
                      eleft, etop, eright - eleft + 1, ebottom - etop + 1);

  ghid_draw_bg_image();

  hid_expose_callback (&ghid_hid, &region, 0);
  ghid_draw_grid ();

  /* In some cases we are called with the crosshair still off */
  if (priv->attached_invalidate_depth == 0)
    DrawAttached ();

  /* In some cases we are called with the mark still off */
  if (priv->mark_invalidate_depth == 0)
    DrawMark ();

  draw_lead_user (priv);

  priv->clip = false;

  /* Rest the clip for bg_gc, as it is used outside this function */
  gdk_gc_set_clip_mask (priv->bg_gc, NULL);
}

void
ghid_invalidate_lr (int left, int right, int top, int bottom)
{
  int dleft, dright, dtop, dbottom;
  int minx, maxx, miny, maxy;
  GdkRectangle rect;

  dleft = Vx (left);
  dright = Vx (right);
  dtop = Vy (top);
  dbottom = Vy (bottom);

  minx = MIN (dleft, dright);
  maxx = MAX (dleft, dright);
  miny = MIN (dtop, dbottom);
  maxy = MAX (dtop, dbottom);

  rect.x = minx;
  rect.y = miny;
  rect.width = maxx - minx;
  rect.height = maxy - miny;

  redraw_region (&rect);
  ghid_screen_update ();
}


void
ghid_invalidate_all ()
{
  redraw_region (NULL);
  ghid_screen_update ();
}

void
ghid_notify_crosshair_change (bool changes_complete)
{
  render_priv *priv = gport->render_priv;

  /* We sometimes get called before the GUI is up */
  if (gport->drawing_area == NULL)
    return;

  if (changes_complete)
    priv->attached_invalidate_depth --;

  if (priv->attached_invalidate_depth < 0)
    {
      priv->attached_invalidate_depth = 0;
      /* A mismatch of changes_complete == false and == true notifications
       * is not expected to occur, but we will try to handle it gracefully.
       * As we know the crosshair will have been shown already, we must
       * repaint the entire view to be sure not to leave an artaefact.
       */
      ghid_invalidate_all ();
      return;
    }

  if (priv->attached_invalidate_depth == 0)
    DrawAttached ();

  if (!changes_complete)
    {
      priv->attached_invalidate_depth ++;
    }
  else if (gport->drawing_area != NULL)
    {
      /* Queue a GTK expose when changes are complete */
      ghid_draw_area_update (gport, NULL);
    }
}

void
ghid_notify_mark_change (bool changes_complete)
{
  render_priv *priv = gport->render_priv;

  /* We sometimes get called before the GUI is up */
  if (gport->drawing_area == NULL)
    return;

  if (changes_complete)
    priv->mark_invalidate_depth --;

  if (priv->mark_invalidate_depth < 0)
    {
      priv->mark_invalidate_depth = 0;
      /* A mismatch of changes_complete == false and == true notifications
       * is not expected to occur, but we will try to handle it gracefully.
       * As we know the mark will have been shown already, we must
       * repaint the entire view to be sure not to leave an artaefact.
       */
      ghid_invalidate_all ();
      return;
    }

  if (priv->mark_invalidate_depth == 0)
    DrawMark ();

  if (!changes_complete)
    {
      priv->mark_invalidate_depth ++;
    }
  else if (gport->drawing_area != NULL)
    {
      /* Queue a GTK expose when changes are complete */
      ghid_draw_area_update (gport, NULL);
    }
}

static void
draw_right_cross (GdkGC *xor_gc, gint x, gint y)
{
  gdk_draw_line (gport->drawing_area->window, xor_gc, x, 0, x, gport->height);
  gdk_draw_line (gport->drawing_area->window, xor_gc, 0, y, gport->width, y);
}

static void
draw_slanted_cross (GdkGC *xor_gc, gint x, gint y)
{
  gint x0, y0, x1, y1;

  x0 = x + (gport->height - y);
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x - y;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + (gport->width - x);
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - x;
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);

  x0 = x - (gport->height - y);
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x + y;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + x;
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - (gport->width - x);
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);
}

static void
draw_dozen_cross (GdkGC *xor_gc, gint x, gint y)
{
  gint x0, y0, x1, y1;
  gdouble tan60 = sqrt (3);

  x0 = x + (gport->height - y) / tan60;
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x - y / tan60;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + (gport->width - x) * tan60;
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - x * tan60;
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);

  x0 = x + (gport->height - y) * tan60;
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x - y * tan60;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + (gport->width - x) / tan60;
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - x / tan60;
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);

  x0 = x - (gport->height - y) / tan60;
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x + y / tan60;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + x * tan60;
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - (gport->width - x) * tan60;
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);

  x0 = x - (gport->height - y) * tan60;
  x0 = MAX(0, MIN (x0, gport->width));
  x1 = x + y * tan60;
  x1 = MAX(0, MIN (x1, gport->width));
  y0 = y + x / tan60;
  y0 = MAX(0, MIN (y0, gport->height));
  y1 = y - (gport->width - x) / tan60;
  y1 = MAX(0, MIN (y1, gport->height));
  gdk_draw_line (gport->drawing_area->window, xor_gc, x0, y0, x1, y1);
}

static void
draw_crosshair (GdkGC *xor_gc, gint x, gint y)
{
  static enum crosshair_shape prev = Basic_Crosshair_Shape;

  draw_right_cross (xor_gc, x, y);
  if (prev == Union_Jack_Crosshair_Shape)
    draw_slanted_cross (xor_gc, x, y);
  if (prev == Dozen_Crosshair_Shape)
    draw_dozen_cross (xor_gc, x, y);
  prev = Crosshair.shape;
}

#define VCW 16
#define VCD 8

static void
show_crosshair (gboolean paint_new_location)
{
  render_priv *priv = gport->render_priv;
  gint x, y;
  static gint x_prev = -1, y_prev = -1;
  static gboolean draw_markers, draw_markers_prev = FALSE;
  static GdkGC *xor_gc;
  static GdkColor cross_color;

  if (gport->crosshair_x < 0 || ghidgui->creating || !gport->has_entered)
    return;

  if (!xor_gc)
    {
      xor_gc = gdk_gc_new (ghid_port.drawing_area->window);
      gdk_gc_copy (xor_gc, ghid_port.drawing_area->style->white_gc);
      gdk_gc_set_function (xor_gc, GDK_XOR);
      gdk_gc_set_clip_origin (xor_gc, 0, 0);
      set_clip (priv, xor_gc);
      /* FIXME: when CrossColor changed from config */
      ghid_map_color_string (Settings.CrossColor, &cross_color);
    }
  x = DRAW_X (gport->crosshair_x);
  y = DRAW_Y (gport->crosshair_y);

  gdk_gc_set_foreground (xor_gc, &cross_color);

  if (x_prev >= 0 && !paint_new_location)
    {
      draw_crosshair (xor_gc, x_prev, y_prev);
      if (draw_markers_prev)
	{
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      0, y_prev - VCD, VCD, VCW);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      gport->width - VCD, y_prev - VCD, VCD, VCW);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      x_prev - VCD, 0, VCW, VCD);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      x_prev - VCD, gport->height - VCD, VCW, VCD);
	}
    }

  if (x >= 0 && paint_new_location)
    {
      draw_crosshair (xor_gc, x, y);
      draw_markers = ghidgui->auto_pan_on && have_crosshair_attachments ();
      if (draw_markers)
	{
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      0, y - VCD, VCD, VCW);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      gport->width - VCD, y - VCD, VCD, VCW);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      x - VCD, 0, VCW, VCD);
	  gdk_draw_rectangle (gport->drawing_area->window, xor_gc, TRUE,
			      x - VCD, gport->height - VCD, VCW, VCD);
	}
      x_prev = x;
      y_prev = y;
      draw_markers_prev = draw_markers;
    }
  else
    {
      x_prev = y_prev = -1;
      draw_markers_prev = FALSE;
    }
}

void
ghid_init_renderer (int *argc, char ***argv, GHidPort *port)
{
  /* Init any GC's required */
  port->render_priv = g_new0 (render_priv, 1);
}

void
ghid_shutdown_renderer (GHidPort *port)
{
  ghid_cancel_lead_user ();
  g_free (port->render_priv);
  port->render_priv = NULL;
}

void
ghid_init_drawing_widget (GtkWidget *widget, GHidPort *port)
{
}

void
ghid_drawing_area_configure_hook (GHidPort *port)
{
  static int done_once = 0;
  render_priv *priv = port->render_priv;

  if (!done_once)
    {
      priv->bg_gc = gdk_gc_new (port->drawable);
      gdk_gc_set_foreground (priv->bg_gc, &port->bg_color);
      gdk_gc_set_clip_origin (priv->bg_gc, 0, 0);

      priv->offlimits_gc = gdk_gc_new (port->drawable);
      gdk_gc_set_foreground (priv->offlimits_gc, &port->offlimits_color);
      gdk_gc_set_clip_origin (priv->offlimits_gc, 0, 0);
      done_once = 1;
    }

  if (port->mask)
    {
      gdk_pixmap_unref (port->mask);
      port->mask = gdk_pixmap_new (0, port->width, port->height, 1);
    }
}

void
ghid_screen_update (void)
{
  render_priv *priv = gport->render_priv;

  gdk_draw_drawable (gport->drawing_area->window, priv->bg_gc, gport->pixmap,
		     0, 0, 0, 0, gport->width, gport->height);
  show_crosshair (TRUE);
}

gboolean
ghid_drawing_area_expose_cb (GtkWidget *widget,
                             GdkEventExpose *ev,
                             GHidPort *port)
{
  render_priv *priv = port->render_priv;

  gdk_draw_drawable (widget->window, priv->bg_gc, port->pixmap,
                    ev->area.x, ev->area.y, ev->area.x, ev->area.y,
                    ev->area.width, ev->area.height);
  show_crosshair (TRUE);
  return FALSE;
}

void
ghid_port_drawing_realize_cb (GtkWidget *widget, gpointer data)
{
}

gboolean
ghid_pinout_preview_expose (GtkWidget *widget,
                            GdkEventExpose *ev)
{
  GhidPinoutPreview *pinout = GHID_PINOUT_PREVIEW (widget);
  GdkDrawable *save_drawable;
  double save_zoom;
  int da_w, da_h;
  int save_left, save_top;
  int save_width, save_height;
  int save_view_width, save_view_height;
  double xz, yz;
  render_priv *priv = gport->render_priv;

  save_zoom = gport->zoom;
  save_width = gport->width;
  save_height = gport->height;
  save_left = gport->view_x0;
  save_top = gport->view_y0;
  save_view_width = gport->view_width;
  save_view_height = gport->view_height;

  /* Setup drawable and zoom factor for drawing routines
   */
  save_drawable = gport->drawable;

  gdk_window_get_geometry (widget->window, 0, 0, &da_w, &da_h, 0);
  xz = (double) pinout->x_max / da_w;
  yz = (double) pinout->y_max / da_h;
  if (xz > yz)
    gport->zoom = xz;
  else
    gport->zoom = yz;

  gport->drawable = widget->window;
  gport->width = da_w;
  gport->height = da_h;
  gport->view_width = da_w * gport->zoom;
  gport->view_height = da_h * gport->zoom;
  gport->view_x0 = (pinout->x_max - gport->view_width) / 2;
  gport->view_y0 = (pinout->y_max - gport->view_height) / 2;

  /* clear background */
  gdk_draw_rectangle (widget->window, priv->bg_gc, TRUE, 0, 0, da_w, da_h);

  /* call the drawing routine */
  hid_expose_callback (&ghid_hid, NULL, &pinout->element);

  gport->drawable = save_drawable;
  gport->zoom = save_zoom;
  gport->width = save_width;
  gport->height = save_height;
  gport->view_x0 = save_left;
  gport->view_y0 = save_top;
  save_top = gport->view_y0;
  save_view_width = gport->view_width;
  save_view_height = gport->view_height;

  /* Setup drawable and zoom factor for drawing routines
   */
  save_drawable = gport->drawable;

  gdk_window_get_geometry (widget->window, 0, 0, &da_w, &da_h, 0);
  xz = (double) pinout->x_max / da_w;
  yz = (double) pinout->y_max / da_h;
  if (xz > yz)
    gport->zoom = xz;
  else
    gport->zoom = yz;

  gport->drawable = widget->window;
  gport->width = da_w;
  gport->height = da_h;
  gport->view_width = da_w * gport->zoom;
  gport->view_height = da_h * gport->zoom;
  gport->view_x0 = (pinout->x_max - gport->view_width) / 2;
  gport->view_y0 = (pinout->y_max - gport->view_height) / 2;

  /* clear background */
  gdk_draw_rectangle (widget->window, priv->bg_gc, TRUE, 0, 0, da_w, da_h);

  /* call the drawing routine */
  hid_expose_callback (&ghid_hid, NULL, &pinout->element);

  gport->drawable = save_drawable;
  gport->zoom = save_zoom;
  gport->width = save_width;
  gport->height = save_height;
  gport->view_x0 = save_left;
  gport->view_y0 = save_top;
  gport->view_width = save_view_width;
  gport->view_height = save_view_height;

  return FALSE;
}

GdkPixmap *
ghid_render_pixmap (int cx, int cy, double zoom, int width, int height, int depth)
{
  GdkPixmap *pixmap;
  GdkDrawable *save_drawable;
  double save_zoom;
  int save_left, save_top;
  int save_width, save_height;
  int save_view_width, save_view_height;
  BoxType region;
  render_priv *priv = gport->render_priv;

  save_drawable = gport->drawable;
  save_zoom = gport->zoom;
  save_width = gport->width;
  save_height = gport->height;
  save_left = gport->view_x0;
  save_top = gport->view_y0;
  save_view_width = gport->view_width;
  save_view_height = gport->view_height;

  pixmap = gdk_pixmap_new (NULL, width, height, depth);

  /* Setup drawable and zoom factor for drawing routines
   */

  gport->drawable = pixmap;
  gport->zoom = zoom;
  gport->width = width;
  gport->height = height;
  gport->view_width = width * gport->zoom;
  gport->view_height = height * gport->zoom;
  gport->view_x0 = ghid_flip_x ? PCB->MaxWidth - cx : cx;
  gport->view_x0 -= gport->view_height / 2;
  gport->view_y0 = ghid_flip_y ? PCB->MaxHeight - cy : cy;
  gport->view_y0 -= gport->view_width  / 2;

  /* clear background */
  gdk_draw_rectangle (pixmap, priv->bg_gc, TRUE, 0, 0, width, height);

  /* call the drawing routine */
  region.X1 = MIN(Px(0), Px(gport->width + 1));
  region.Y1 = MIN(Py(0), Py(gport->height + 1));
  region.X2 = MAX(Px(0), Px(gport->width + 1));
  region.Y2 = MAX(Py(0), Py(gport->height + 1));

  region.X1 = MAX (0, MIN (PCB->MaxWidth,  region.X1));
  region.X2 = MAX (0, MIN (PCB->MaxWidth,  region.X2));
  region.Y1 = MAX (0, MIN (PCB->MaxHeight, region.Y1));
  region.Y2 = MAX (0, MIN (PCB->MaxHeight, region.Y2));

  hid_expose_callback (&ghid_hid, &region, NULL);

  gport->drawable = save_drawable;
  gport->zoom = save_zoom;
  gport->width = save_width;
  gport->height = save_height;
  gport->view_x0 = save_left;
  gport->view_y0 = save_top;
  gport->view_width = save_view_width;
  gport->view_height = save_view_height;

  return pixmap;
}

HID *
ghid_request_debug_draw (void)
{
  /* No special setup requirements, drawing goes into
   * the backing pixmap. */
  return &ghid_hid;
}

void
ghid_flush_debug_draw (void)
{
  ghid_screen_update ();
  gdk_flush ();
}

void
ghid_finish_debug_draw (void)
{
  ghid_flush_debug_draw ();
  /* No special tear down requirements
   */
}

bool
ghid_event_to_pcb_coords (int event_x, int event_y, Coord *pcb_x, Coord *pcb_y)
{
  *pcb_x = EVENT_TO_PCB_X (event_x);
  *pcb_y = EVENT_TO_PCB_Y (event_y);

  return true;
}

bool
ghid_pcb_to_event_coords (Coord pcb_x, Coord pcb_y, int *event_x, int *event_y)
{
  *event_x = DRAW_X (pcb_x);
  *event_y = DRAW_Y (pcb_y);

  return true;
}

static void
pan_common (GHidPort *port)
{
  int event_x, event_y;

  /* We need to fix up the PCB coordinates corresponding to the last
  * event so convert it back to event coordinates temporarily. */
  ghid_pcb_to_event_coords (gport->pcb_x, gport->pcb_y, &event_x, &event_y);

  /* Don't pan so far the board is completely off the screen */
  port->view_x0 = MAX (-port->view_width,  port->view_x0);
  port->view_y0 = MAX (-port->view_height, port->view_y0);
  port->view_x0 = MIN ( port->view_x0, PCB->MaxWidth);
  port->view_y0 = MIN ( port->view_y0, PCB->MaxHeight);

  /* Fix up noted event coordinates to match where we clamped. Alternatively
   * we could call ghid_note_event_location (NULL); to get a new pointer
   * location, but this costs us an xserver round-trip (on X11 platforms)
   */
  ghid_event_to_pcb_coords (event_x, event_y, &gport->pcb_x, &gport->pcb_y);

  ghidgui->adjustment_changed_holdoff = TRUE;
  gtk_range_set_value (GTK_RANGE (ghidgui->h_range), gport->view_x0);
  gtk_range_set_value (GTK_RANGE (ghidgui->v_range), gport->view_y0);
  ghidgui->adjustment_changed_holdoff = FALSE;

  ghid_port_ranges_changed();
}

static void
ghid_pan_view_abs (Coord pcb_x, Coord pcb_y, int widget_x, int widget_y)
{
  gport->view_x0 = SIDE_X (pcb_x) - widget_x * gport->zoom;
  gport->view_y0 = SIDE_Y (pcb_y) - widget_y * gport->zoom;

  pan_common (gport);
}

void
ghid_pan_view_rel (Coord dx, Coord dy)
{
  gport->view_x0 += dx;
  gport->view_y0 += dy;

  pan_common (gport);
}


/* gport->zoom:
 * zoom value is PCB units per screen pixel.  Larger numbers mean zooming
 * out - the largest value means you are looking at the whole board.
 *
 * gport->view_width and gport->view_height are in PCB coordinates
 */

#define ALLOW_ZOOM_OUT_BY 10 /* Arbitrary, and same as the lesstif HID */
static void
ghid_zoom_view_abs (Coord center_x, Coord center_y, double new_zoom)
{
  double min_zoom, max_zoom;
  double xtmp, ytmp;

  /* Limit the "minimum" zoom constant (maximum zoom), at 1 pixel per PCB
   * unit, and set the "maximum" zoom constant (minimum zoom), such that
   * the entire board just fits inside the viewport
   */
  min_zoom = 1;
  max_zoom = MAX (PCB->MaxWidth  / gport->width,
                  PCB->MaxHeight / gport->height) * ALLOW_ZOOM_OUT_BY;
  new_zoom = MIN (MAX (min_zoom, new_zoom), max_zoom);

  if (gport->zoom == new_zoom)
    return;

  xtmp = (SIDE_X (center_x) - gport->view_x0) / (double)gport->view_width;
  ytmp = (SIDE_Y (center_y) - gport->view_y0) / (double)gport->view_height;

  gport->zoom = new_zoom;
  pixel_slop = new_zoom;
  ghid_port_ranges_scale ();

  gport->view_x0 = SIDE_X (center_x) - xtmp * gport->view_width;
  gport->view_y0 = SIDE_Y (center_y) - ytmp * gport->view_height;

  pan_common (gport);

  ghid_set_status_line_label ();
}

static void
ghid_zoom_view_rel (Coord center_x, Coord center_y, double factor)
{
  ghid_zoom_view_abs (center_x, center_y, gport->zoom * factor);
}

static void
ghid_zoom_view_fit (void)
{
  ghid_pan_view_abs (0, 0, 0, 0);
  ghid_zoom_view_abs (0, 0, MAX (PCB->MaxWidth  / gport->width,
                                 PCB->MaxHeight / gport->height));
}

static void
ghid_flip_view (Coord center_x, Coord center_y, bool flip_x, bool flip_y)
{
  int widget_x, widget_y;

  /* Work out where on the screen the flip point is */
  ghid_pcb_to_event_coords (center_x, center_y, &widget_x, &widget_y);

  ghid_flip_x = ghid_flip_x != flip_x;
  ghid_flip_y = ghid_flip_y != flip_y;

  /* Pan the board so the center location remains in the same place */
  ghid_pan_view_abs (center_x, center_y, widget_x, widget_y);

  ghid_invalidate_all ();
}


#define LEAD_USER_WIDTH           0.2          /* millimeters */
#define LEAD_USER_PERIOD          (1000 / 5)   /* 5fps (in ms) */
#define LEAD_USER_VELOCITY        3.           /* millimeters per second */
#define LEAD_USER_ARC_COUNT       3
#define LEAD_USER_ARC_SEPARATION  3.           /* millimeters */
#define LEAD_USER_INITIAL_RADIUS  10.          /* millimetres */
#define LEAD_USER_COLOR_R         1.
#define LEAD_USER_COLOR_G         1.
#define LEAD_USER_COLOR_B         0.

static void
draw_lead_user (render_priv *priv)
{
  int i;
  Coord radius = priv->lead_user_radius;
  Coord width = MM_TO_COORD (LEAD_USER_WIDTH);
  Coord separation = MM_TO_COORD (LEAD_USER_ARC_SEPARATION);
  static GdkGC *lead_gc = NULL;
  GdkColor lead_color;

  if (!priv->lead_user)
    return;

  if (lead_gc == NULL)
    {
      lead_gc = gdk_gc_new (ghid_port.drawing_area->window);
      gdk_gc_copy (lead_gc, ghid_port.drawing_area->style->white_gc);
      gdk_gc_set_function (lead_gc, GDK_XOR);
      gdk_gc_set_clip_origin (lead_gc, 0, 0);
      lead_color.pixel = 0;
      lead_color.red   = (int)(65535. * LEAD_USER_COLOR_R);
      lead_color.green = (int)(65535. * LEAD_USER_COLOR_G);
      lead_color.blue  = (int)(65535. * LEAD_USER_COLOR_B);
      gdk_color_alloc (gport->colormap, &lead_color);
      gdk_gc_set_foreground (lead_gc, &lead_color);
    }

  set_clip (priv, lead_gc);
  gdk_gc_set_line_attributes (lead_gc, Vz (width),
                              GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);

  /* arcs at the approrpriate radii */

  for (i = 0; i < LEAD_USER_ARC_COUNT; i++, radius -= separation)
    {
      if (radius < width)
        radius += MM_TO_COORD (LEAD_USER_INITIAL_RADIUS);

      /* Draw an arc at radius */
      gdk_draw_arc (gport->drawable, lead_gc, FALSE,
                    Vx (priv->lead_user_x - radius),
                    Vy (priv->lead_user_y - radius),
                    Vz (2. * radius), Vz (2. * radius),
                    0, 360 * 64);
    }
}

gboolean
lead_user_cb (gpointer data)
{
  render_priv *priv = data;
  Coord step;
  double elapsed_time;

  /* Queue a redraw */
  ghid_invalidate_all ();

  /* Update radius */
  elapsed_time = g_timer_elapsed (priv->lead_user_timer, NULL);
  g_timer_start (priv->lead_user_timer);

  step = MM_TO_COORD (LEAD_USER_VELOCITY * elapsed_time);
  if (priv->lead_user_radius > step)
    priv->lead_user_radius -= step;
  else
    priv->lead_user_radius = MM_TO_COORD (LEAD_USER_INITIAL_RADIUS);

  return TRUE;
}

void
ghid_lead_user_to_location (Coord x, Coord y)
{
  render_priv *priv = gport->render_priv;

  ghid_cancel_lead_user ();

  priv->lead_user = true;
  priv->lead_user_x = x;
  priv->lead_user_y = y;
  priv->lead_user_radius = MM_TO_COORD (LEAD_USER_INITIAL_RADIUS);
  priv->lead_user_timeout = g_timeout_add (LEAD_USER_PERIOD, lead_user_cb, priv);
  priv->lead_user_timer = g_timer_new ();
}

void
ghid_cancel_lead_user (void)
{
  render_priv *priv = gport->render_priv;

  if (priv->lead_user_timeout)
    g_source_remove (priv->lead_user_timeout);

  if (priv->lead_user_timer)
    g_timer_destroy (priv->lead_user_timer);

  if (priv->lead_user)
    ghid_invalidate_all ();

  priv->lead_user_timeout = 0;
  priv->lead_user_timer = NULL;
  priv->lead_user = false;
}
