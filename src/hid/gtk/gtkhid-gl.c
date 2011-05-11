/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "crosshair.h"
#include "clip.h"
#include "../hidint.h"
#include "gui.h"
#include "draw.h"
#include "draw_funcs.h"
#include "rtree.h"
#include "polygon.h"
#include "gui-pinout-preview.h"

/* The Linux OpenGL ABI 1.0 spec requires that we define
 * GL_GLEXT_PROTOTYPES before including gl.h or glx.h for extensions
 * in order to get prototypes:
 *   http://www.opengl.org/registry/ABI/
 */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <gtk/gtkgl.h>
#include "hid/common/hidgl.h"

#include "hid/common/draw_helpers.h"
#include "hid/common/trackball.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

//#define VIEW_ORTHO

RCSID ("$Id$");


extern HID ghid_hid;

static void ghid_global_alpha_mult (hidGC, double);

static hidGC current_gc = NULL;
static char *current_color = NULL;
static double global_alpha_mult = 1.0;
static int alpha_changed = 0;
static bool check_gl_drawing_ok_hack = false;


/* Sets gport->u_gc to the "right" GC to use (wrt mask or window)
*/
#define USE_GC(gc) if (!use_gc(gc)) return

static int cur_mask = -1;
static GLfloat view_matrix[4][4] = {{1.0, 0.0, 0.0, 0.0},
                                    {0.0, 1.0, 0.0, 0.0},
                                    {0.0, 0.0, 1.0, 0.0},
                                    {0.0, 0.0, 0.0, 1.0}};
static GLfloat last_modelview_matrix[4][4] = {{1.0, 0.0, 0.0, 0.0},
                                              {0.0, 1.0, 0.0, 0.0},
                                              {0.0, 0.0, 1.0, 0.0},
                                              {0.0, 0.0, 0.0, 1.0}};
static int global_view_2d = 1;

typedef struct render_priv {
  GdkGLConfig *glconfig;
  bool trans_lines;
  bool in_context;
  int subcomposite_stencil_bit;
  GTimer *time_since_expose;
} render_priv;


typedef struct hid_gc_struct
{
  HID *me_pointer;

  gchar *colorname;
  gint width;
  gint cap, join;
  gchar xor;
  gchar erase;
}
hid_gc_struct;

#define BOARD_THICKNESS 6300
#define MASK_COPPER_SPACING 200
#define SILK_MASK_SPACING 50
static int
compute_depth (int group)
{
  static int last_depth_computed = 0;

  int solder_group;
  int component_group;
  int min_copper_group;
  int max_copper_group;
  int num_copper_groups;
  int middle_copper_group;
  int depth;

  if (global_view_2d)
    return 0;

  solder_group = GetLayerGroupNumberByNumber (solder_silk_layer);
  component_group = GetLayerGroupNumberByNumber (component_silk_layer);

  min_copper_group = MIN (solder_group, component_group);
  max_copper_group = MAX (solder_group, component_group);
  num_copper_groups = max_copper_group - min_copper_group;// + 1;
  middle_copper_group = min_copper_group + num_copper_groups / 2;

  if (group >= 0 && group < max_group) {
    if (group >= min_copper_group && group <= max_copper_group) {
      /* XXX: IS THIS INCORRECT FOR REVERSED GROUP ORDERINGS? */
      depth = -(group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups;
    } else {
      depth = 0;
    }

  } else if (SL_TYPE (group) == SL_MASK) {
    if (SL_SIDE (group) == SL_TOP_SIDE) {
      depth = -((min_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups - MASK_COPPER_SPACING);
    } else {
      depth = -((max_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups + MASK_COPPER_SPACING);
    }
  } else if (SL_TYPE (group) == SL_SILK) {
    if (SL_SIDE (group) == SL_TOP_SIDE) {
      depth = -((min_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups - MASK_COPPER_SPACING - SILK_MASK_SPACING);
    } else {
      depth = -((max_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups + MASK_COPPER_SPACING + SILK_MASK_SPACING);
    }

  } else if (SL_TYPE (group) == SL_INVISIBLE) {
    /* Same as silk, but for the back-side layer */
    if (Settings.ShowSolderSide) {
      depth = -((min_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups - MASK_COPPER_SPACING - SILK_MASK_SPACING);
    } else {
      depth = -((max_copper_group - middle_copper_group) * BOARD_THICKNESS / num_copper_groups + MASK_COPPER_SPACING + SILK_MASK_SPACING);
    }
  } else if (SL_TYPE (group) == SL_RATS) {
    depth = last_depth_computed;
  } else {
    /* DEFAULT CASE */
    printf ("Unknown layer group to set depth for: %i\n", group);
    depth = last_depth_computed;
  }

  last_depth_computed = depth;
  return depth;
}

static void
start_subcomposite (void)
{
  render_priv *priv = gport->render_priv;
  int stencil_bit;

  /* Flush out any existing geoemtry to be rendered */
  hidgl_flush_triangles (&buffer);

  glEnable (GL_STENCIL_TEST);                                 /* Enable Stencil test */
  glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);                 /* Stencil pass => replace stencil value (with 1) */

  stencil_bit = hidgl_assign_clear_stencil_bit();             /* Get a new (clean) bitplane to stencil with */
  glStencilMask (stencil_bit);                                /* Only write to our subcompositing stencil bitplane */
  glStencilFunc (GL_GREATER, stencil_bit, stencil_bit);       /* Pass stencil test if our assigned bit is clear */

  priv->subcomposite_stencil_bit = stencil_bit;
}

static void
end_subcomposite (void)
{
  render_priv *priv = gport->render_priv;

  /* Flush out any existing geoemtry to be rendered */
  hidgl_flush_triangles (&buffer);

  hidgl_return_stencil_bit (priv->subcomposite_stencil_bit);  /* Relinquish any bitplane we previously used */

  glStencilMask (0);
  glStencilFunc (GL_ALWAYS, 0, 0);                            /* Always pass stencil test */
  glDisable (GL_STENCIL_TEST);                                /* Disable Stencil test */

  priv->subcomposite_stencil_bit = 0;
}

int
ghid_set_layer (const char *name, int group, int empty)
{
  render_priv *priv = gport->render_priv;
  bool group_visible = false;
  bool subcomposite = true;
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
    {
      priv->trans_lines = true;
      subcomposite = true;
      group_visible = PCB->Data->Layer[idx].On;
    }
  else if (idx < 0)
    {
      switch (SL_TYPE (idx))
	{
	case SL_INVISIBLE:
	  priv->trans_lines = false;
	  subcomposite = false;
	  group_visible = PCB->InvisibleObjectsOn;
	  break;
	case SL_MASK:
	  priv->trans_lines = true;
	  subcomposite = false;
	  group_visible = TEST_FLAG (SHOWMASKFLAG, PCB);
	  break;
	case SL_SILK:
	  priv->trans_lines = true;
	  subcomposite = true;
	  group_visible = PCB->ElementOn;
	  break;
	case SL_ASSY:
	  break;
	case SL_PDRILL:
	case SL_UDRILL:
	  priv->trans_lines = true;
	  subcomposite = true;
	  group_visible = true;
	  break;
	case SL_RATS:
	  priv->trans_lines = true;
	  subcomposite = false;
	  group_visible = PCB->RatOn;
	  break;
	}
    }

  end_subcomposite ();

  if (group_visible && subcomposite)
    start_subcomposite ();

  /* Drawing is already flushed by {start,end}_subcomposite */
  hidgl_set_depth (compute_depth (group));

  return group_visible;
}

static void
ghid_end_layer (void)
{
  end_subcomposite ();
}

void
ghid_destroy_gc (hidGC gc)
{
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
ghid_draw_grid (BoxTypePtr drawn_area)
{
  if (Vz (PCB->Grid) < MIN_GRID_DISTANCE)
    return;

  if (gdk_color_parse (Settings.GridColor, &gport->grid_color))
    {
      gport->grid_color.red ^= gport->bg_color.red;
      gport->grid_color.green ^= gport->bg_color.green;
      gport->grid_color.blue ^= gport->bg_color.blue;
    }

  glTexCoord2f (0., 0.);

  glDisable (GL_STENCIL_TEST);
  glEnable (GL_COLOR_LOGIC_OP);
  glLogicOp (GL_XOR);

  glColor3f (gport->grid_color.red / 65535.,
             gport->grid_color.green / 65535.,
             gport->grid_color.blue / 65535.);

  hidgl_draw_grid (drawn_area);

  glDisable (GL_COLOR_LOGIC_OP);
  glEnable (GL_STENCIL_TEST);
}

static void
ghid_draw_bg_image (void)
{
  static GLuint texture_handle = 0;

  if (!ghidgui->bg_pixbuf)
    return;

  if (texture_handle == 0)
    {
      int width =             gdk_pixbuf_get_width (ghidgui->bg_pixbuf);
      int height =            gdk_pixbuf_get_height (ghidgui->bg_pixbuf);
      int rowstride =         gdk_pixbuf_get_rowstride (ghidgui->bg_pixbuf);
      int bits_per_sample =   gdk_pixbuf_get_bits_per_sample (ghidgui->bg_pixbuf);
      int n_channels =        gdk_pixbuf_get_n_channels (ghidgui->bg_pixbuf);
      unsigned char *pixels = gdk_pixbuf_get_pixels (ghidgui->bg_pixbuf);

      g_warn_if_fail (bits_per_sample == 8);
      g_warn_if_fail (rowstride == width * n_channels);

      glGenTextures (1, &texture_handle);
      glBindTexture (GL_TEXTURE_2D, texture_handle);

      /* XXX: We should proabbly determine what the maxmimum texture supported is,
       *      and if our image is larger, shrink it down using GDK pixbuf routines
       *      rather than having it fail below.
       */

      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                    (n_channels == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels);
    }

  glBindTexture (GL_TEXTURE_2D, texture_handle);

  glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
  glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glEnable (GL_TEXTURE_2D);

  /* Render a quad with the background as a texture */

  glBegin (GL_QUADS);
  glTexCoord2d (0., 0.);
  glVertex3i (0,             0,              0);
  glTexCoord2d (1., 0.);
  glVertex3i (PCB->MaxWidth, 0,              0);
  glTexCoord2d (1., 1.);
  glVertex3i (PCB->MaxWidth, PCB->MaxHeight, 0);
  glTexCoord2d (0., 1.);
  glVertex3i (0,             PCB->MaxHeight, 0);
  glEnd ();

  glDisable (GL_TEXTURE_2D);
}

void
ghid_use_mask (int use_it)
{
  static int stencil_bit = 0;

  if (use_it == cur_mask)
    return;

  /* Flush out any existing geoemtry to be rendered */
  hidgl_flush_triangles (&buffer);

  switch (use_it)
    {
    case HID_MASK_BEFORE:
      /* The HID asks not to receive this mask type, so warn if we get it */
      g_return_if_reached ();

    case HID_MASK_CLEAR:
      /* Write '1' to the stencil buffer where the solder-mask should not be drawn. */
      glColorMask (0, 0, 0, 0);                             /* Disable writting in color buffer */
      glEnable (GL_STENCIL_TEST);                           /* Enable Stencil test */
      stencil_bit = hidgl_assign_clear_stencil_bit();       /* Get a new (clean) bitplane to stencil with */
      glStencilFunc (GL_ALWAYS, stencil_bit, stencil_bit);  /* Always pass stencil test, write stencil_bit */
      glStencilMask (stencil_bit);                          /* Only write to our subcompositing stencil bitplane */
      glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);           /* Stencil pass => replace stencil value (with 1) */
      break;

    case HID_MASK_AFTER:
      /* Drawing operations as masked to areas where the stencil buffer is '0' */
      glColorMask (1, 1, 1, 1);                   /* Enable drawing of r, g, b & a */
      glStencilFunc (GL_GEQUAL, 0, stencil_bit);  /* Draw only where our bit of the stencil buffer is clear */
      glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);    /* Stencil buffer read only */
      break;

    case HID_MASK_OFF:
      /* Disable stenciling */
      hidgl_return_stencil_bit (stencil_bit);     /* Relinquish any bitplane we previously used */
      glDisable (GL_STENCIL_TEST);                /* Disable Stencil test */
      break;
    }
  cur_mask = use_it;
}


  /* Config helper functions for when the user changes color preferences.
     |  set_special colors used in the gtkhid.
   */
static void
set_special_grid_color (void)
{
  if (!gport->colormap)
    return;
  gport->grid_color.red ^= gport->bg_color.red;
  gport->grid_color.green ^= gport->bg_color.green;
  gport->grid_color.blue ^= gport->bg_color.blue;
}

void
ghid_set_special_colors (HID_Attribute * ha)
{
  if (!ha->name || !ha->value)
    return;
  if (!strcmp (ha->name, "background-color"))
    {
      ghid_map_color_string (*(char **) ha->value, &gport->bg_color);
      set_special_grid_color ();
    }
  else if (!strcmp (ha->name, "off-limit-color"))
  {
      ghid_map_color_string (*(char **) ha->value, &gport->offlimits_color);
    }
  else if (!strcmp (ha->name, "grid-color"))
    {
      ghid_map_color_string (*(char **) ha->value, &gport->grid_color);
      set_special_grid_color ();
    }
}

typedef struct
{
  int color_set;
  GdkColor color;
  int xor_set;
  GdkColor xor_color;
  double red;
  double green;
  double blue;
} ColorCache;

void
ghid_set_color (hidGC gc, const char *name)
{
  render_priv *priv = gport->render_priv;
  static void *cache = NULL;
  hidval cval;
  ColorCache *cc;
  double alpha_mult = 1.0;
  double r, g, b, a;
  a = 1.0;

  current_gc = gc;

  if (!alpha_changed && current_color != NULL &&
      strcmp (name, current_color) == 0)
    return;

  free (current_color);
  current_color = NULL;

  if (name == NULL)
    return;

  alpha_changed = 0;
  gc->colorname = (char *) name;

  if (!check_gl_drawing_ok_hack)
    return;

  current_color = strdup (name);

  if (gport->colormap == NULL)
    gport->colormap = gtk_widget_get_colormap (gport->top_window);
  if (strcmp (name, "erase") == 0)
    {
      gc->erase = 1;
      r = gport->bg_color.red   / 65535.;
      g = gport->bg_color.green / 65535.;
      b = gport->bg_color.blue  / 65535.;
    }
  else if (strcmp (name, "drill") == 0)
    {
      gc->erase = 0;
      alpha_mult = 0.85;
      r = gport->offlimits_color.red   / 65535.;
      g = gport->offlimits_color.green / 65535.;
      b = gport->offlimits_color.blue  / 65535.;
    }
  else
    {
      alpha_mult = 0.7;
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
          cc->red   = cc->color.red   / 65535.;
          cc->green = cc->color.green / 65535.;
          cc->blue  = cc->color.blue  / 65535.;
          cc->color_set = 1;
        }
      if (gc->xor)
        {
          if (!cc->xor_set)
            {
              cc->xor_color.red = cc->color.red ^ gport->bg_color.red;
              cc->xor_color.green = cc->color.green ^ gport->bg_color.green;
              cc->xor_color.blue = cc->color.blue ^ gport->bg_color.blue;
              gdk_color_alloc (gport->colormap, &cc->xor_color);
              cc->red   = cc->color.red   / 65535.;
              cc->green = cc->color.green / 65535.;
              cc->blue  = cc->color.blue  / 65535.;
              cc->xor_set = 1;
            }
        }
      r = cc->red;
      g = cc->green;
      b = cc->blue;

      gc->erase = 0;
    }
  if (1) {
    double maxi, mult;
    alpha_mult *= global_alpha_mult;
    if (priv->trans_lines)
      a = a * alpha_mult;
    maxi = r;
    if (g > maxi) maxi = g;
    if (b > maxi) maxi = b;
    mult = MIN (1 / alpha_mult, 1 / maxi);
#if 1
    r = r * mult;
    g = g * mult;
    b = b * mult;
#endif
  }

  if(!priv->in_context)
    return;

  hidgl_flush_triangles (&buffer);
  glColor4d (r, g, b, a);
}

static void
ghid_global_alpha_mult (hidGC gc, double alpha_mult)
{
  if (alpha_mult != global_alpha_mult) {
    global_alpha_mult = alpha_mult;
    alpha_changed = 1;
    ghid_set_color (gc, gc->colorname);
  }
}

void
ghid_set_line_cap (hidGC gc, EndCapStyle style)
{
  gc->cap = style;
}

void
ghid_set_line_width (hidGC gc, int width)
{
  gc->width = width;
}


void
ghid_set_draw_xor (hidGC gc, int xor)
{
  /* NOT IMPLEMENTED */

  /* Only presently called when setting up a crosshair GC.
   * We manage our own drawing model for that anyway. */
}

void
ghid_set_draw_faded (hidGC gc, int faded)
{
  printf ("ghid_set_draw_faded(%p,%d) -- not implemented\n", gc, faded);
}

void
ghid_set_line_cap_angle (hidGC gc, int x1, int y1, int x2, int y2)
{
  printf ("ghid_set_line_cap_angle() -- not implemented\n");
}

static void
ghid_invalidate_current_gc (void)
{
  current_gc = NULL;
}

static int
use_gc (hidGC gc)
{
  if (gc->me_pointer != &ghid_hid)
    {
      fprintf (stderr, "Fatal: GC from another HID passed to GTK HID\n");
      abort ();
    }

  if (current_gc == gc)
    return 1;

  current_gc = gc;

  ghid_set_color (gc, gc->colorname);
  return 1;
}

void
ghid_draw_line (hidGC gc, int x1, int y1, int x2, int y2)
{
  USE_GC (gc);

  hidgl_draw_line (gc->cap, gc->width, x1, y1, x2, y2, gport->zoom);
}

void
ghid_draw_arc (hidGC gc, int cx, int cy, int xradius, int yradius,
                         int start_angle, int delta_angle)
{
  USE_GC (gc);

  hidgl_draw_arc (gc->width, cx, cy, xradius, yradius,
                  start_angle, delta_angle, gport->zoom);
}

void
ghid_draw_rect (hidGC gc, int x1, int y1, int x2, int y2)
{
  USE_GC (gc);

  hidgl_draw_rect (x1, y1, x2, y2);
}


void
ghid_fill_circle (hidGC gc, int cx, int cy, int radius)
{
  USE_GC (gc);

  hidgl_fill_circle (cx, cy, radius);
}


void
ghid_fill_polygon (hidGC gc, int n_coords, int *x, int *y)
{
  USE_GC (gc);

  hidgl_fill_polygon (n_coords, x, y);
}

void
ghid_fill_pcb_polygon (hidGC gc, PolygonType *poly, const BoxType *clip_box)
{
  USE_GC (gc);

  hidgl_fill_pcb_polygon (poly, clip_box);
}

void
ghid_thindraw_pcb_polygon (hidGC gc, PolygonType *poly, const BoxType *clip_box)
{
  common_thindraw_pcb_polygon (gc, poly, clip_box);
  ghid_global_alpha_mult (gc, 0.25);
  ghid_fill_pcb_polygon (gc, poly, clip_box);
  ghid_global_alpha_mult (gc, 1.0);
}

void
ghid_fill_rect (hidGC gc, int x1, int y1, int x2, int y2)
{
  USE_GC (gc);

  hidgl_fill_rect (x1, y1, x2, y2);
}

void
ghid_invalidate_lr (int left, int right, int top, int bottom)
{
  ghid_invalidate_all ();
}

#define MAX_ELAPSED (50. / 1000.) /* 50ms */
void
ghid_invalidate_all ()
{
  render_priv *priv = gport->render_priv;
  double elapsed = g_timer_elapsed (priv->time_since_expose, NULL);

  ghid_draw_area_update (gport, NULL);

  if (elapsed > MAX_ELAPSED)
    gdk_window_process_all_updates ();
}

void
ghid_notify_crosshair_change (bool changes_complete)
{
  /* We sometimes get called before the GUI is up */
  if (gport->drawing_area == NULL)
    return;

  /* FIXME: We could just invalidate the bounds of the crosshair attached objects? */
  ghid_invalidate_all ();
}

void
ghid_notify_mark_change (bool changes_complete)
{
  /* We sometimes get called before the GUI is up */
  if (gport->drawing_area == NULL)
    return;

  /* FIXME: We could just invalidate the bounds of the mark? */
  ghid_invalidate_all ();
}

static void
draw_right_cross (gint x, gint y, gint z)
{
  glVertex3i (x, 0, z);
  glVertex3i (x, PCB->MaxHeight, z);
  glVertex3i (0, y, z);
  glVertex3i (PCB->MaxWidth, y, z);
}

static void
draw_slanted_cross (gint x, gint y, gint z)
{
  gint x0, y0, x1, y1;

  x0 = x + (PCB->MaxHeight - y);
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x - y;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + (PCB->MaxWidth - x);
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - x;
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);

  x0 = x - (PCB->MaxHeight - y);
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x + y;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + x;
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - (PCB->MaxWidth - x);
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);
}

static void
draw_dozen_cross (gint x, gint y, gint z)
{
  gint x0, y0, x1, y1;
  gdouble tan60 = sqrt (3);

  x0 = x + (PCB->MaxHeight - y) / tan60;
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x - y / tan60;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + (PCB->MaxWidth - x) * tan60;
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - x * tan60;
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);

  x0 = x + (PCB->MaxHeight - y) * tan60;
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x - y * tan60;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + (PCB->MaxWidth - x) / tan60;
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - x / tan60;
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);

  x0 = x - (PCB->MaxHeight - y) / tan60;
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x + y / tan60;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + x * tan60;
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - (PCB->MaxWidth - x) * tan60;
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);

  x0 = x - (PCB->MaxHeight - y) * tan60;
  x0 = MAX(0, MIN (x0, PCB->MaxWidth));
  x1 = x + y * tan60;
  x1 = MAX(0, MIN (x1, PCB->MaxWidth));
  y0 = y + x / tan60;
  y0 = MAX(0, MIN (y0, PCB->MaxHeight));
  y1 = y - (PCB->MaxWidth - x) / tan60;
  y1 = MAX(0, MIN (y1, PCB->MaxHeight));
  glVertex3i (x0, y0, z);
  glVertex3i (x1, y1, z);
}

static void
draw_crosshair (gint x, gint y, gint z)
{
  static enum crosshair_shape prev = Basic_Crosshair_Shape;

  draw_right_cross (x, y, z);
  if (prev == Union_Jack_Crosshair_Shape)
    draw_slanted_cross (x, y, z);
  if (prev == Dozen_Crosshair_Shape)
    draw_dozen_cross (x, y, z);
  prev = Crosshair.shape;
}

#define VCW 16
#define VCD 8

void
ghid_show_crosshair (gboolean paint_new_location)
{
  gint x, y, z;
  gboolean draw_markers;
  static int done_once = 0;
  static GdkColor cross_color;
  extern float global_depth;

  if (!paint_new_location)
    return;

  if (!check_gl_drawing_ok_hack)
    return;

  if (!done_once)
    {
      done_once = 1;
      /* FIXME: when CrossColor changed from config */
      ghid_map_color_string (Settings.CrossColor, &cross_color);
    }
  x = gport->x_crosshair;
  y = gport->y_crosshair;
  z = global_depth;

  glEnable (GL_COLOR_LOGIC_OP);
  glLogicOp (GL_XOR);

  hidgl_flush_triangles (&buffer);

  glColor3f (cross_color.red / 65535.,
             cross_color.green / 65535.,
             cross_color.blue / 65535.);

  if (x >= 0 && paint_new_location)
    {
      glBegin (GL_LINES);
      draw_crosshair (x, y, z);
      glEnd ();
    }

  draw_markers = ghidgui->auto_pan_on && have_crosshair_attachments ();
  if (x >= 0 && paint_new_location && draw_markers)
    {
      glBegin (GL_QUADS);
      glVertex3i (0,                       y - VCD,                  z);
      glVertex3i (0,                       y - VCD + VCW,            z);
      glVertex3i (VCD,                     y - VCD + VCW,            z);
      glVertex3i (VCD,                     y - VCD,                  z);
      glVertex3i (gport->view_width,       y - VCD,                  z);
      glVertex3i (gport->view_width,       y - VCD + VCW,            z);
      glVertex3i (gport->view_width - VCD, y - VCD + VCW,            z);
      glVertex3i (gport->view_width - VCD, y - VCD,                  z);
      glVertex3i (x - VCD,                 0,                        z);
      glVertex3i (x - VCD,                 VCD,                      z);
      glVertex3i (x - VCD + VCW,           VCD,                      z);
      glVertex3i (x - VCD + VCW,           0,                        z);
      glVertex3i (x - VCD,                 gport->view_height - VCD, z);
      glVertex3i (x - VCD,                 gport->view_height,       z);
      glVertex3i (x - VCD + VCW,           gport->view_height,       z);
      glVertex3i (x - VCD + VCW,           gport->view_height - VCD, z);
      glEnd ();
    }

  glDisable (GL_COLOR_LOGIC_OP);
}

void
ghid_init_renderer (int *argc, char ***argv, GHidPort *port)
{
  render_priv *priv;

  port->render_priv = priv = g_new0 (render_priv, 1);

  priv->time_since_expose = g_timer_new ();

  gtk_gl_init(argc, argv);

  /* setup GL-context */
  priv->glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGBA    |
                                              GDK_GL_MODE_STENCIL |
                                              GDK_GL_MODE_DOUBLE);
  if (!priv->glconfig)
    {
      printf ("Could not setup GL-context!\n");
      return; /* Should we abort? */
    }

  /* Setup HID function pointers specific to the GL renderer*/
  ghid_hid.end_layer = ghid_end_layer;
}

void
ghid_init_drawing_widget (GtkWidget *widget, GHidPort *port)
{
  render_priv *priv = port->render_priv;

  gtk_widget_set_gl_capability (widget,
                                priv->glconfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE);
}

void
ghid_drawing_area_configure_hook (GHidPort *port)
{
}

gboolean
ghid_start_drawing (GHidPort *port)
{
  GtkWidget *widget = port->drawing_area;
  GdkGLContext *pGlContext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *pGlDrawable = gtk_widget_get_gl_drawable (widget);

  /* make GL-context "current" */
  if (!gdk_gl_drawable_gl_begin (pGlDrawable, pGlContext))
    return FALSE;

  port->render_priv->in_context = true;

  Output.fgGC = gui->make_gc ();
  Output.bgGC = gui->make_gc ();
  Output.pmGC = gui->make_gc ();

  return TRUE;
}

void
ghid_end_drawing (GHidPort *port)
{
  GtkWidget *widget = port->drawing_area;
  GdkGLDrawable *pGlDrawable = gtk_widget_get_gl_drawable (widget);

  if (gdk_gl_drawable_is_double_buffered (pGlDrawable))
    gdk_gl_drawable_swap_buffers (pGlDrawable);
  else
    glFlush ();

  port->render_priv->in_context = false;

  /* end drawing to current GL-context */
  gdk_gl_drawable_gl_end (pGlDrawable);

  gui->destroy_gc (Output.fgGC);
  gui->destroy_gc (Output.bgGC);
  gui->destroy_gc (Output.pmGC);

  Output.fgGC = NULL;
  Output.bgGC = NULL;
  Output.pmGC = NULL;
}

void
ghid_screen_update (void)
{
}

static int
element_callback (const BoxType * b, void *cl)
{
  ElementTypePtr element = (ElementTypePtr) b;
  int *side = cl;

  if (ON_SIDE (element, *side))
    DrawElementPackage (element);
  return 1;
}

static int
name_callback (const BoxType * b, void *cl)
{
  TextTypePtr text = (TextTypePtr) b;
  ElementTypePtr element = (ElementTypePtr) text->Element;
  int *side = cl;

  if (TEST_FLAG (HIDENAMEFLAG, element))
    return 0;

  if (ON_SIDE (element, *side))
    DrawElementName (element);
  return 0;
}

static int
EMark_callback (const BoxType * b, void *cl)
{
  ElementTypePtr element = (ElementTypePtr) b;

  DrawEMark (element, element->MarkX, element->MarkY, !FRONT (element));
  return 1;
}

static void
SetPVColor (PinTypePtr Pin, int Type)
{
  char *color;

  if (Type == VIA_TYPE)
    {
      if (TEST_FLAG (WARNFLAG | SELECTEDFLAG | FOUNDFLAG, Pin))
	{
	  if (TEST_FLAG (WARNFLAG, Pin))
	    color = PCB->WarnColor;
	  else if (TEST_FLAG (SELECTEDFLAG, Pin))
	    color = PCB->ViaSelectedColor;
	  else
	    color = PCB->ConnectedColor;
	}
      else
	color = PCB->ViaColor;
    }
  else
    {
      if (TEST_FLAG (WARNFLAG | SELECTEDFLAG | FOUNDFLAG, Pin))
	{
	  if (TEST_FLAG (WARNFLAG, Pin))
	    color = PCB->WarnColor;
	  else if (TEST_FLAG (SELECTEDFLAG, Pin))
	    color = PCB->PinSelectedColor;
	  else
	    color = PCB->ConnectedColor;
	}
      else
	color = PCB->PinColor;
    }

  gui->set_color (Output.fgGC, color);
}

static void
SetPVColor_inlayer (PinTypePtr Pin, LayerTypePtr Layer, int Type)
{
  char *color;

  if (TEST_FLAG (WARNFLAG, Pin))
    color = PCB->WarnColor;
  else if (TEST_FLAG (SELECTEDFLAG, Pin))
    color = (Type == VIA_TYPE) ? PCB->ViaSelectedColor : PCB->PinSelectedColor;
  else if (TEST_FLAG (FOUNDFLAG, Pin))
    color = PCB->ConnectedColor;
  else
    {
      int component_group = GetLayerGroupNumberByNumber (component_silk_layer);
      int solder_group    = GetLayerGroupNumberByNumber (solder_silk_layer);
      int this_group      = GetLayerGroupNumberByPointer (Layer);

      if (this_group == component_group || this_group == solder_group)
        color = (SWAP_IDENT == (this_group == solder_group))
                  ? PCB->ViaColor : PCB->InvisibleObjectsColor;
      else
        color = Layer->Color;
    }

  gui->set_color (Output.fgGC, color);
}

static void
_draw_pv_name (PinType *pv)
{
  BoxType box;
  bool vert;
  TextType text;

  if (!pv->Name || !pv->Name[0])
    text.TextString = EMPTY (pv->Number);
  else
    text.TextString = EMPTY (TEST_FLAG (SHOWNUMBERFLAG, PCB) ? pv->Number : pv->Name);

  vert = TEST_FLAG (EDGE2FLAG, pv);

  if (vert)
    {
      box.X1 = pv->X - pv->Thickness    / 2 + Settings.PinoutTextOffsetY;
      box.Y1 = pv->Y - pv->DrillingHole / 2 - Settings.PinoutTextOffsetX;
    }
  else
    {
      box.X1 = pv->X + pv->DrillingHole / 2 + Settings.PinoutTextOffsetX;
      box.Y1 = pv->Y - pv->Thickness    / 2 + Settings.PinoutTextOffsetY;
    }

  gui->set_color (Output.fgGC, PCB->PinNameColor);

  text.Flags = NoFlags ();
  text.Scale = pv->Thickness / 80;
  text.X = box.X1;
  text.Y = box.Y1;
  text.Direction = vert ? 1 : 0;

  DrawTextLowLevel (&text, 0);
}

static void
_draw_pv (PinTypePtr pv, bool draw_hole)
{
  if (TEST_FLAG (THINDRAWFLAG, PCB))
    gui->thindraw_pcb_pv (Output.fgGC, Output.fgGC, pv, draw_hole, false);
  else
    gui->fill_pcb_pv (Output.fgGC, Output.bgGC, pv, draw_hole, false);

  if (!TEST_FLAG (HOLEFLAG, pv) && TEST_FLAG (DISPLAYNAMEFLAG, pv))
    _draw_pv_name (pv);
}

static void
draw_pin (PinTypePtr pin, bool draw_hole)
{
  SetPVColor (pin, PIN_TYPE);
  _draw_pv (pin, draw_hole);
}

static int
pin_callback (const BoxType * b, void *cl)
{
  PinTypePtr pin = (PinTypePtr) b;

  if (!TEST_FLAG (HOLEFLAG, pin) && TEST_FLAG (DISPLAYNAMEFLAG, pin))
    _draw_pv_name (pin);
  draw_pin (pin, false);
  return 1;
}

static int
pin_name_callback (const BoxType * b, void *cl)
{
  PinTypePtr pin = (PinTypePtr) b;

  if (!TEST_FLAG (HOLEFLAG, pin) && TEST_FLAG (DISPLAYNAMEFLAG, pin))
    _draw_pv_name (pin);
  return 1;
}

static int
pin_inlayer_callback (const BoxType * b, void *cl)
{
  SetPVColor_inlayer ((PinTypePtr) b, cl, PIN_TYPE);
  _draw_pv ((PinType *) b, false);
  return 1;
}

static void
draw_via (PinTypePtr via, bool draw_hole)
{
  SetPVColor (via, VIA_TYPE);
  _draw_pv (via, draw_hole);
}

static int
via_callback (const BoxType * b, void *cl)
{
  draw_via ((PinType *)b, TEST_FLAG (THINDRAWFLAG, PCB));
  return 1;
}

static int
via_inlayer_callback (const BoxType * b, void *cl)
{
  SetPVColor_inlayer ((PinTypePtr) b, cl, VIA_TYPE);
  _draw_pv ((PinType *) b, TEST_FLAG (THINDRAWFLAG, PCB));
  return 1;
}

static void
draw_pad_name (PadType *pad)
{
  BoxType box;
  bool vert;
  TextType text;

  if (!pad->Name || !pad->Name[0])
    text.TextString = EMPTY (pad->Number);
  else
    text.TextString = EMPTY (TEST_FLAG (SHOWNUMBERFLAG, PCB) ? pad->Number : pad->Name);

  /* should text be vertical ? */
  vert = (pad->Point1.X == pad->Point2.X);

  if (vert)
    {
      box.X1 = pad->Point1.X                      - pad->Thickness / 2;
      box.Y1 = MAX (pad->Point1.Y, pad->Point2.Y) + pad->Thickness / 2;
      box.X1 += Settings.PinoutTextOffsetY;
      box.Y1 -= Settings.PinoutTextOffsetX;
    }
  else
    {
      box.X1 = MIN (pad->Point1.X, pad->Point2.X) - pad->Thickness / 2;
      box.Y1 = pad->Point1.Y                      - pad->Thickness / 2;
      box.X1 += Settings.PinoutTextOffsetX;
      box.Y1 += Settings.PinoutTextOffsetY;
    }

  gui->set_color (Output.fgGC, PCB->PinNameColor);

  text.Flags = NoFlags ();
  text.Scale = pad->Thickness / 50;
  text.X = box.X1;
  text.Y = box.Y1;
  text.Direction = vert ? 1 : 0;

  DrawTextLowLevel (&text, 0);
}

static void
_draw_pad (hidGC gc, PadType *pad, bool clear, bool mask)
{
  if (clear && !mask && pad->Clearance <= 0)
    return;

  if (TEST_FLAG (THINDRAWFLAG, PCB) ||
      (clear && TEST_FLAG (THINDRAWPOLYFLAG, PCB)))
    gui->thindraw_pcb_pad (gc, pad, clear, mask);
  else
    gui->fill_pcb_pad (gc, pad, clear, mask);
}

static void
draw_pad (PadType *pad)
{
  if (TEST_FLAG (WARNFLAG | SELECTEDFLAG | FOUNDFLAG, pad))
   {
     if (TEST_FLAG (WARNFLAG, pad))
       gui->set_color (Output.fgGC, PCB->WarnColor);
     else if (TEST_FLAG (SELECTEDFLAG, pad))
       gui->set_color (Output.fgGC, PCB->PinSelectedColor);
     else
       gui->set_color (Output.fgGC, PCB->ConnectedColor);
   }
  else if (FRONT (pad))
   gui->set_color (Output.fgGC, PCB->PinColor);
  else
   gui->set_color (Output.fgGC, PCB->InvisibleObjectsColor);

  _draw_pad (Output.fgGC, pad, false, false);

  if (TEST_FLAG (DISPLAYNAMEFLAG, pad))
    draw_pad_name (pad);
}

static int
pad_callback (const BoxType * b, void *cl)
{
  PadTypePtr pad = (PadTypePtr) b;
  int *side = cl;

  if (ON_SIDE (pad, *side)) {
    if (TEST_FLAG (DISPLAYNAMEFLAG, pad))
      draw_pad_name (pad);
    draw_pad (pad);
  }
  return 1;
}


static int
hole_callback (const BoxType * b, void *cl)
{
  PinTypePtr pv = (PinTypePtr) b;
  int plated = cl ? *(int *) cl : -1;

  if ((plated == 0 && !TEST_FLAG (HOLEFLAG, pv)) ||
      (plated == 1 &&  TEST_FLAG (HOLEFLAG, pv)))
    return 1;

  if (TEST_FLAG (THINDRAWFLAG, PCB))
    {
      if (!TEST_FLAG (HOLEFLAG, pv))
        {
          gui->set_line_cap (Output.fgGC, Round_Cap);
          gui->set_line_width (Output.fgGC, 0);
          gui->draw_arc (Output.fgGC,
                         pv->X, pv->Y, pv->DrillingHole / 2,
                         pv->DrillingHole / 2, 0, 360);
        }
    }
  else
    gui->fill_circle (Output.bgGC, pv->X, pv->Y, pv->DrillingHole / 2);

  if (TEST_FLAG (HOLEFLAG, pv))
    {
      if (TEST_FLAG (WARNFLAG, pv))
        gui->set_color (Output.fgGC, PCB->WarnColor);
      else if (TEST_FLAG (SELECTEDFLAG, pv))
        gui->set_color (Output.fgGC, PCB->ViaSelectedColor);
      else
        gui->set_color (Output.fgGC, Settings.BlackColor);

      gui->set_line_cap (Output.fgGC, Round_Cap);
      gui->set_line_width (Output.fgGC, 0);
      gui->draw_arc (Output.fgGC,
                     pv->X, pv->Y, pv->DrillingHole / 2,
                     pv->DrillingHole / 2, 0, 360);
    }
  return 1;
}

static void
_draw_line (LineType *line)
{
  gui->set_line_cap (Output.fgGC, Trace_Cap);
  if (TEST_FLAG (THINDRAWFLAG, PCB))
    gui->set_line_width (Output.fgGC, 0);
  else
    gui->set_line_width (Output.fgGC, line->Thickness);

  gui->draw_line (Output.fgGC,
		  line->Point1.X, line->Point1.Y,
		  line->Point2.X, line->Point2.Y);
}

static void
draw_line (LayerType *layer, LineType *line)
{
  if (TEST_FLAG (SELECTEDFLAG | FOUNDFLAG, line))
    {
      if (TEST_FLAG (SELECTEDFLAG, line))
        gui->set_color (Output.fgGC, layer->SelectedColor);
      else
        gui->set_color (Output.fgGC, PCB->ConnectedColor);
    }
  else
    gui->set_color (Output.fgGC, layer->Color);
  _draw_line (line);
}

static int
line_callback (const BoxType * b, void *cl)
{
  draw_line ((LayerType *) cl, (LineType *) b);
  return 1;
}

static void
_draw_arc (ArcType *arc)
{
  if (!arc->Thickness)
    return;

  if (TEST_FLAG (THINDRAWFLAG, PCB))
    gui->set_line_width (Output.fgGC, 0);
  else
    gui->set_line_width (Output.fgGC, arc->Thickness);
  gui->set_line_cap (Output.fgGC, Trace_Cap);

  gui->draw_arc (Output.fgGC, arc->X, arc->Y, arc->Width,
                 arc->Height, arc->StartAngle, arc->Delta);
}

static void
draw_arc (LayerType *layer, ArcType *arc)
{
  if (TEST_FLAG (SELECTEDFLAG | FOUNDFLAG, arc))
    {
      if (TEST_FLAG (SELECTEDFLAG, arc))
        gui->set_color (Output.fgGC, layer->SelectedColor);
      else
        gui->set_color (Output.fgGC, PCB->ConnectedColor);
    }
  else
    gui->set_color (Output.fgGC, layer->Color);

  _draw_arc (arc);
}

static int
arc_callback (const BoxType * b, void *cl)
{
  draw_arc ((LayerTypePtr) cl, (ArcTypePtr) b);
  return 1;
}

static int
text_callback (const BoxType * b, void *cl)
{
  LayerType *layer = cl;
  TextType *text = (TextType *)b;
  int min_silk_line;

  if (TEST_FLAG (SELECTEDFLAG, text))
    gui->set_color (Output.fgGC, layer->SelectedColor);
  else
    gui->set_color (Output.fgGC, layer->Color);
  if (layer == &PCB->Data->SILKLAYER ||
      layer == &PCB->Data->BACKSILKLAYER)
    min_silk_line = PCB->minSlk;
  else
    min_silk_line = PCB->minWid;
  DrawTextLowLevel (text, min_silk_line);
  return 1;
}

static void
DrawPlainPolygon (LayerTypePtr Layer, PolygonTypePtr Polygon, const BoxType *drawn_area)
{
  static char *color;

  if (!Polygon->Clipped)
    return;

  if (TEST_FLAG (SELECTEDFLAG, Polygon))
    color = Layer->SelectedColor;
  else if (TEST_FLAG (FOUNDFLAG, Polygon))
    color = PCB->ConnectedColor;
  else
    color = Layer->Color;
  gui->set_color (Output.fgGC, color);

  if (gui->thindraw_pcb_polygon != NULL &&
      (TEST_FLAG (THINDRAWFLAG, PCB) ||
       TEST_FLAG (THINDRAWPOLYFLAG, PCB)))
    gui->thindraw_pcb_polygon (Output.fgGC, Polygon, drawn_area);
  else
    gui->fill_pcb_polygon (Output.fgGC, Polygon, drawn_area);

  /* If checking planes, thin-draw any pieces which have been clipped away */
  if (gui->thindraw_pcb_polygon != NULL &&
      TEST_FLAG (CHECKPLANESFLAG, PCB) &&
      !TEST_FLAG (FULLPOLYFLAG, Polygon))
    {
      PolygonType poly = *Polygon;

      for (poly.Clipped = Polygon->Clipped->f;
           poly.Clipped != Polygon->Clipped;
           poly.Clipped = poly.Clipped->f)
        gui->thindraw_pcb_polygon (Output.fgGC, &poly, drawn_area);
    }
}

struct poly_info
{
  LayerTypePtr Layer;
  const BoxType *drawn_area;
};

static int
poly_callback_no_clear (const BoxType * b, void *cl)
{
  struct poly_info *i = (struct poly_info *) cl;
  PolygonType *polygon = (PolygonType *) b;

  if (TEST_FLAG (CLEARPOLYFLAG, polygon))
    return 0;

  DrawPlainPolygon (i->Layer, polygon, i->drawn_area);
  return 1;
}

static int
poly_callback_clearing (const BoxType * b, void *cl)
{
  struct poly_info *i = (struct poly_info *) cl;
  PolygonType *polygon = (PolygonType *) b;

  if (!TEST_FLAG (CLEARPOLYFLAG, polygon))
    return 0;

  DrawPlainPolygon (i->Layer, polygon, i->drawn_area);
  return 1;
}

static int
clearPin_callback (const BoxType * b, void *cl)
{
  PinType *pin = (PinTypePtr) b;
  if (TEST_FLAG (THINDRAWFLAG, PCB) || TEST_FLAG (THINDRAWPOLYFLAG, PCB))
    gui->thindraw_pcb_pv (Output.pmGC, Output.pmGC, pin, false, true);
  else
    gui->fill_pcb_pv (Output.pmGC, Output.pmGC, pin, false, true);
  return 1;
}

static int
clearPad_callback (const BoxType * b, void *cl)
{
  PadTypePtr pad = (PadTypePtr) b;
  int *side = cl;
  if (ON_SIDE (pad, *side) && pad->Mask)
    _draw_pad (Output.pmGC, pad, true, true);
  return 1;
}

static int
clearPin_callback_solid (const BoxType * b, void *cl)
{
  PinTypePtr pin = (PinTypePtr) b;
  gui->fill_pcb_pv (Output.pmGC, Output.pmGC, pin, false, true);
  return 1;
}

static int
clearPad_callback_solid (const BoxType * b, void *cl)
{
  PadTypePtr pad = (PadTypePtr) b;
  int *side = cl;
  if (ON_SIDE (pad, *side) && pad->Mask)
    gui->fill_pcb_pad (Output.pmGC, pad, true, true);
  return 1;
}

static void
DrawMask (int side, BoxType * screen)
{
  int thin = TEST_FLAG(THINDRAWFLAG, PCB) || TEST_FLAG(THINDRAWPOLYFLAG, PCB);
  PolygonType polygon;

  OutputType *out = &Output;

  if (thin)
    {
      gui->set_line_width (Output.pmGC, 0);
      gui->set_color (Output.pmGC, PCB->MaskColor);
      r_search (PCB->Data->pin_tree, screen, NULL, clearPin_callback, NULL);
      r_search (PCB->Data->via_tree, screen, NULL, clearPin_callback, NULL);
      r_search (PCB->Data->pad_tree, screen, NULL, clearPad_callback, &side);
      gui->set_color (Output.pmGC, "erase");
    }

  gui->use_mask (HID_MASK_CLEAR);
  r_search (PCB->Data->pin_tree, screen, NULL, clearPin_callback_solid, NULL);
  r_search (PCB->Data->via_tree, screen, NULL, clearPin_callback_solid, NULL);
  r_search (PCB->Data->pad_tree, screen, NULL, clearPad_callback_solid, &side);

  gui->use_mask (HID_MASK_AFTER);
  gui->set_color (out->fgGC, PCB->MaskColor);
  ghid_global_alpha_mult (out->fgGC, thin ? 0.35 : 1.0);

  polygon.Clipped = board_outline_poly ();
  polygon.NoHoles = NULL;
  polygon.NoHolesValid = 0;
  if (screen)
    polygon.BoundingBox = *screen;
  SET_FLAG (FULLPOLYFLAG, &polygon);
  common_fill_pcb_polygon (out->fgGC, &polygon, screen);
  poly_Free (&polygon.Clipped);
  poly_FreeContours (&polygon.NoHoles);
  /* THE GL fill_pcb_polygon doesn't work whilst masking */
//  gui->fill_pcb_polygon (out->fgGC, &polygon, screen);
//  gui->fill_rect (out->fgGC, 0, 0, PCB->MaxWidth, PCB->MaxHeight);
  ghid_global_alpha_mult (out->fgGC, 1.0);

  gui->use_mask (HID_MASK_OFF);
}

static int
DrawLayerGroup (int group, const BoxType * screen)
{
  int i, rv = 1;
  int layernum;
  int side;
  struct poly_info info;
  LayerTypePtr Layer;
  int n_entries = PCB->LayerGroups.Number[group];
  Cardinal *layers = PCB->LayerGroups.Entries[group];
  int first_run = 1;
  int component_group = GetLayerGroupNumberByNumber (component_silk_layer);
  int solder_group    = GetLayerGroupNumberByNumber (solder_silk_layer);

  if (!gui->set_layer (0, group, 0))
    return 0;

  /* HACK: Subcomposite each layer in a layer group separately */
  for (i = n_entries - 1; i >= 0; i--) {
    layernum = layers[i];
    Layer = PCB->Data->Layer + layers[i];

    if (strcmp (Layer->Name, "outline") == 0 ||
        strcmp (Layer->Name, "route") == 0)
      rv = 0;

    if (layernum < max_copper_layer && Layer->On) {

      if (!first_run)
        gui->set_layer (0, group, 0);

      first_run = 0;

      if (rv && !TEST_FLAG (THINDRAWFLAG, PCB)) {
        /* Mask out drilled holes on this layer */
        hidgl_flush_triangles (&buffer);
        glPushAttrib (GL_COLOR_BUFFER_BIT);
        glColorMask (0, 0, 0, 0);
        gui->set_color (Output.bgGC, PCB->MaskColor);
        if (PCB->PinOn) r_search (PCB->Data->pin_tree, screen, NULL, hole_callback, NULL);
        if (PCB->ViaOn) r_search (PCB->Data->via_tree, screen, NULL, hole_callback, NULL);
        hidgl_flush_triangles (&buffer);
        glPopAttrib ();
      }

      /* draw all polygons on this layer */
      if (Layer->PolygonN) {
        info.Layer = Layer;
        info.drawn_area = screen;
        r_search (Layer->polygon_tree, screen, NULL, poly_callback_no_clear, &info);
        r_search (Layer->polygon_tree, screen, NULL, poly_callback_clearing, &info);

        /* HACK: Subcomposite polygons separately from other layer primitives */
        /* Reset the compositing */
        gui->end_layer ();
        gui->set_layer (0, group, 0);

        if (rv && !TEST_FLAG (THINDRAWFLAG, PCB)) {
          hidgl_flush_triangles (&buffer);
          glPushAttrib (GL_COLOR_BUFFER_BIT);
          glColorMask (0, 0, 0, 0);
          /* Mask out drilled holes on this layer */
          if (PCB->PinOn) r_search (PCB->Data->pin_tree, screen, NULL, hole_callback, NULL);
          if (PCB->ViaOn) r_search (PCB->Data->via_tree, screen, NULL, hole_callback, NULL);
          hidgl_flush_triangles (&buffer);
          glPopAttrib ();
        }
      }

      /* Draw pins, vias and pads on this layer */
      if (!global_view_2d && rv) {
        if (PCB->PinOn &&
            (group == solder_group || group == component_group))
          r_search (PCB->Data->pin_tree, screen, NULL, pin_name_callback, Layer);
        if (PCB->PinOn) r_search (PCB->Data->pin_tree, screen, NULL, pin_inlayer_callback, Layer);
        if (PCB->ViaOn) r_search (PCB->Data->via_tree, screen, NULL, via_inlayer_callback, Layer);
        if (PCB->PinOn && group == component_group)
          {
            side = COMPONENT_LAYER;
            r_search (PCB->Data->pad_tree, screen, NULL, pad_callback, &side);
          }
        if (PCB->PinOn && group == solder_group)
          {
            side = SOLDER_LAYER;
            r_search (PCB->Data->pad_tree, screen, NULL, pad_callback, &side);
          }
      }

      if (TEST_FLAG (CHECKPLANESFLAG, PCB))
        continue;

      r_search (Layer->line_tree, screen, NULL, line_callback, Layer);
      r_search (Layer->arc_tree, screen, NULL, arc_callback, Layer);
      r_search (Layer->text_tree, screen, NULL, text_callback, Layer);
    }
  }

  gui->end_layer ();

  return (n_entries > 1);
}

static void
DrawDrillChannel (int vx, int vy, int vr, int from_layer, int to_layer, double scale)
{
#define PIXELS_PER_CIRCLINE 5.
#define MIN_FACES_PER_CYL 6
#define MAX_FACES_PER_CYL 360
  float radius = vr;
  float x, y, z1, z2;
  int i;
  int slices;

  slices = M_PI * 2 * vr / scale / PIXELS_PER_CIRCLINE;

  if (slices < MIN_FACES_PER_CYL)
    slices = MIN_FACES_PER_CYL;

  if (slices > MAX_FACES_PER_CYL)
    slices = MAX_FACES_PER_CYL;

  z1 = compute_depth (from_layer);
  z2 = compute_depth (to_layer);

  x = vx + vr;
  y = vy;

  hidgl_ensure_vertex_space (&buffer, 2 * slices + 2 + 2);

  /* NB: Repeated first virtex to separate from other tri-strip */
  hidgl_add_vertex_3D_tex (&buffer, x, y, z1, 0.0, 0.0);
  hidgl_add_vertex_3D_tex (&buffer, x, y, z1, 0.0, 0.0);
  hidgl_add_vertex_3D_tex (&buffer, x, y, z2, 0.0, 0.0);

  for (i = 0; i < slices; i++)
    {
      x = radius * cosf (((float)(i + 1)) * 2. * M_PI / (float)slices) + vx;
      y = radius * sinf (((float)(i + 1)) * 2. * M_PI / (float)slices) + vy;

      hidgl_add_vertex_3D_tex (&buffer, x, y, z1, 0.0, 0.0);
      hidgl_add_vertex_3D_tex (&buffer, x, y, z2, 0.0, 0.0);
    }

  /* NB: Repeated last virtex to separate from other tri-strip */
  hidgl_add_vertex_3D_tex (&buffer, x, y, z2, 0.0, 0.0);
}

struct cyl_info {
  int from_layer;
  int to_layer;
  double scale;
};

static int
draw_hole_cyl (PinType *Pin, struct cyl_info *info, int Type)
{
  char *color;

  if (TEST_FLAG (WARNFLAG, Pin))
    color = PCB->WarnColor;
  else if (TEST_FLAG (SELECTEDFLAG, Pin))
    color = (Type == VIA_TYPE) ? PCB->ViaSelectedColor : PCB->PinSelectedColor;
  else if (TEST_FLAG (FOUNDFLAG, Pin))
    color = PCB->ConnectedColor;
  else
    color = "drill";

  gui->set_color (Output.fgGC, color);
  DrawDrillChannel (Pin->X, Pin->Y, Pin->DrillingHole / 2, info->from_layer, info->to_layer, info->scale);
  return 0;
}

static int
pin_hole_cyl_callback (const BoxType * b, void *cl)
{
  return draw_hole_cyl ((PinType *)b, (struct cyl_info *)cl, PIN_TYPE);
}

static int
via_hole_cyl_callback (const BoxType * b, void *cl)
{
  return draw_hole_cyl ((PinType *)b, (struct cyl_info *)cl, VIA_TYPE);
}

void
ghid_draw_everything (BoxTypePtr drawn_area)
{
  int i, ngroups;
  int side;
  /* This is the list of layer groups we will draw.  */
  int do_group[MAX_LAYER];
  /* This is the reverse of the order in which we draw them.  */
  int drawn_groups[MAX_LAYER];
  struct cyl_info cyl_info;
  int reverse_layers;
  int save_show_solder;
  int solder_group;
  int component_group;
  int min_phys_group;
  int max_phys_group;

  current_color = NULL;

  /* Test direction of rendering */
  /* Look at sign of eye coordinate system z-coord when projecting a
     world vector along +ve Z axis, (0, 0, 1). */
  /* XXX: This isn't strictly correct, as I've ignored the matrix
     elements for homogeneous coordinates. */
  /* NB: last_modelview_matrix is transposed in memory! */
  reverse_layers = (last_modelview_matrix[2][2] < 0);

  save_show_solder = Settings.ShowSolderSide;
  Settings.ShowSolderSide = reverse_layers;

  PCB->Data->SILKLAYER.Color = PCB->ElementColor;
  PCB->Data->BACKSILKLAYER.Color = PCB->InvisibleObjectsColor;

  solder_group = GetLayerGroupNumberByNumber (solder_silk_layer);
  component_group = GetLayerGroupNumberByNumber (component_silk_layer);

  min_phys_group = MIN (solder_group, component_group);
  max_phys_group = MAX (solder_group, component_group);

  memset (do_group, 0, sizeof (do_group));
  if (global_view_2d) {
    /* Draw in layer stack order when in 2D view */
    for (ngroups = 0, i = 0; i < max_copper_layer; i++) {
      int group = GetLayerGroupNumberByNumber (LayerStack[i]);

      if (!do_group[group]) {
        do_group[group] = 1;
        drawn_groups[ngroups++] = group;
      }
    }
  } else {
    /* Draw in group order when in 3D view */
    for (ngroups = 0, i = 0; i < max_group; i++) {
      int group = reverse_layers ? max_group - 1 - i : i;

      if (!do_group[group]) {
        do_group[group] = 1;
        drawn_groups[ngroups++] = group;
      }
    }
  }

  /*
   * first draw all 'invisible' stuff
   */
  side = SWAP_IDENT ? COMPONENT_LAYER : SOLDER_LAYER;

  if (!TEST_FLAG (CHECKPLANESFLAG, PCB) &&
      gui->set_layer ("invisible", SL (INVISIBLE, 0), 0)) {
    if (PCB->ElementOn) {
      r_search (PCB->Data->name_tree[NAME_INDEX (PCB)], drawn_area, NULL, name_callback, &side);
      dapi->draw_layer (&(PCB->Data->Layer[max_copper_layer + side]), drawn_area, NULL);
    }
    if (global_view_2d) {
      r_search (PCB->Data->pad_tree, drawn_area, NULL, pad_callback, &side);
    } else {
      /* Draw the reverse-side solder mask if turned on */
      if (gui->set_layer (SWAP_IDENT ? "componentmask" : "soldermask",
                          SWAP_IDENT ? SL (MASK, TOP) : SL (MASK, BOTTOM), 0)) {
        DrawMask (side, drawn_area);
        gui->end_layer ();
      }
      gui->set_layer ("invisible", SL (INVISIBLE, 0), 0);
    }
    if (PCB->ElementOn)
      r_search (PCB->Data->element_tree, drawn_area, NULL, element_callback, &side);
    gui->end_layer ();
  }

  /* draw all layers in layerstack order */
  for (i = ngroups - 1; i >= 0; i--) {
    DrawLayerGroup (drawn_groups [i], drawn_area);

#if 1
    if (!global_view_2d && i > 0 &&
        drawn_groups[i] >= min_phys_group &&
        drawn_groups[i] <= max_phys_group &&
        drawn_groups[i - 1] >= min_phys_group &&
        drawn_groups[i - 1] <= max_phys_group) {
      cyl_info.from_layer = drawn_groups[i];
      cyl_info.to_layer = drawn_groups[i - 1];
      cyl_info.scale = gport->zoom;
      gui->set_color (Output.fgGC, "drill");
      ghid_global_alpha_mult (Output.fgGC, 0.75);
      if (PCB->PinOn) r_search (PCB->Data->pin_tree, drawn_area, NULL, pin_hole_cyl_callback, &cyl_info);
      if (PCB->ViaOn) r_search (PCB->Data->via_tree, drawn_area, NULL, via_hole_cyl_callback, &cyl_info);
      ghid_global_alpha_mult (Output.fgGC, 1.0);
    }
#endif
  }

  if (TEST_FLAG (CHECKPLANESFLAG, PCB))
    return;

  side = SWAP_IDENT ? SOLDER_LAYER : COMPONENT_LAYER;

  /* Draw pins, pads, vias below silk */
  if (global_view_2d) {
    start_subcomposite ();

    if (!TEST_FLAG (THINDRAWFLAG, PCB)) {
      /* Mask out drilled holes */
      hidgl_flush_triangles (&buffer);
      glPushAttrib (GL_COLOR_BUFFER_BIT);
      glColorMask (0, 0, 0, 0);
      if (PCB->PinOn) r_search (PCB->Data->pin_tree, drawn_area, NULL, hole_callback, NULL);
      if (PCB->ViaOn) r_search (PCB->Data->via_tree, drawn_area, NULL, hole_callback, NULL);
      hidgl_flush_triangles (&buffer);
      glPopAttrib ();
    }

    if (PCB->PinOn) r_search (PCB->Data->pad_tree, drawn_area, NULL, pad_callback, &side);
    if (PCB->PinOn) r_search (PCB->Data->pin_tree, drawn_area, NULL, pin_callback, NULL);
    if (PCB->ViaOn) r_search (PCB->Data->via_tree, drawn_area, NULL, via_callback, NULL);

    end_subcomposite ();
  }

  /* Draw the solder mask if turned on */
  if (gui->set_layer (SWAP_IDENT ? "soldermask" : "componentmask",
                      SWAP_IDENT ? SL (MASK, BOTTOM) : SL (MASK, TOP), 0)) {
    DrawMask (side, drawn_area);
    gui->end_layer ();
  }

  if (gui->set_layer (SWAP_IDENT ? "bottomsilk" : "topsilk",
                      SWAP_IDENT ? SL (SILK, BOTTOM) : SL (SILK, TOP), 0)) {
      DrawSilk (side, drawn_area);
      gui->end_layer ();
  }

  /* Draw element Marks */
  if (PCB->PinOn)
    r_search (PCB->Data->element_tree, drawn_area, NULL, EMark_callback, NULL);

  /* Draw rat lines on top */
  if (PCB->RatOn && gui->set_layer ("rats", SL (RATS, 0), 0)) {
    DrawRats(drawn_area);
    gui->end_layer ();
  }

  Settings.ShowSolderSide = save_show_solder;
}

#define Z_NEAR 3.0
gboolean
ghid_drawing_area_expose_cb (GtkWidget *widget,
                             GdkEventExpose *ev,
                             GHidPort *port)
{
  render_priv *priv = port->render_priv;
  BoxType region;
  int min_x, min_y;
  int max_x, max_y;
  int new_x, new_y;
  int min_depth;
  int max_depth;
  float aspect;
  GLfloat scale[] = {1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1};
  bool horizon_problem = false;

  ghid_start_drawing (port);

  hidgl_in_context (true);
  hidgl_init ();
  check_gl_drawing_ok_hack = true;

  /* If we don't have any stencil bits available,
     we can't use the hidgl polygon drawing routine */
  /* TODO: We could use the GLU tessellator though */
  if (hidgl_stencil_bits() == 0)
    ghid_hid.fill_pcb_polygon = common_fill_pcb_polygon;

  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport (0, 0, widget->allocation.width, widget->allocation.height);

  glEnable (GL_SCISSOR_TEST);
  glScissor (ev->area.x,
             widget->allocation.height - ev->area.height - ev->area.y,
             ev->area.width, ev->area.height);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  aspect = (float)widget->allocation.width / (float)widget->allocation.height;

#ifdef VIEW_ORTHO
  glOrtho (-1. * aspect, 1. * aspect, 1., -1., 1., 24.);
#else
  glFrustum (-1. * aspect, 1 * aspect, 1., -1., 1., 24.);
#endif

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

#ifndef VIEW_ORTHO
  /* TEST HACK */
  glScalef (11., 11., 1.);
#endif

  /* Push the space coordinates board back into the middle of the z-view volume */
  glTranslatef (0., 0., -11.);

  /* Rotate about the center of the board space */
  glMultMatrixf ((GLfloat *)view_matrix);

  /* Flip about the center of the viewed area */
  glScalef ((ghid_flip_x ? -1. : 1.),
            (ghid_flip_y ? -1. : 1.),
            ((ghid_flip_x == ghid_flip_y) ? 1. : -1.));

  /* Scale board coordiantes to (-1,-1)-(1,1) coordiantes */
  /* Adjust the "w" coordinate of our homogeneous coodinates. We coulld in
   * theory just use glScalef to transform, but on mesa this produces errors
   * as the resulting modelview matrix has a very small determinant.
   */
  scale[15] = (float)port->zoom * (float)MIN (widget->allocation.width, widget->allocation.height) / 2.;
  /* XXX: Need to choose which to use (width or height) based on the aspect of the window
   *      AND the aspect of the board!
   */
  glMultMatrixf (scale);

  /* Translate to the center of the board space view */
  glTranslatef (-(port->view_x0 + port->view_width / 2),
                -(port->view_y0 + port->view_height / 2),
                0.);

  /* Stash the model view matrix so we can work out the screen coordinate -> board coordinate mapping */
  glGetFloatv (GL_MODELVIEW_MATRIX, (GLfloat *)last_modelview_matrix);

#if 0
  /* Fix up matrix so the board Z coordinate does not affect world Z
   * this lets us view each stacked layer without parallax effects.
   *
   * Commented out because it breaks:
   *   Board view "which side should I render first" calculation
   *   Z-buffer depth occlusion when rendering component models
   */
  last_modelview_matrix[2][2] = 0.;
  glLoadMatrixf ((GLfloat *)last_modelview_matrix);
#endif

  glEnable (GL_STENCIL_TEST);
  glClearColor (port->bg_color.red / 65535.,
                port->bg_color.green / 65535.,
                port->bg_color.blue / 65535.,
                1.);
  glStencilMask (~0);
  glClearStencil (0);
  glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  hidgl_reset_stencil_usage ();

  /* Disable the stencil test until we need it - otherwise it gets dirty */
  glDisable (GL_STENCIL_TEST);
  glStencilMask (0);
  glStencilFunc (GL_ALWAYS, 0, 0);

  /* Test the 8 corners of a cube spanning the event */
  min_depth = -50 + compute_depth (0);                    /* FIXME: NEED TO USE PHYSICAL GROUPS */
  max_depth =  50 + compute_depth (max_copper_layer - 1); /* FIXME: NEED TO USE PHYSICAL GROUPS */

  if (!ghid_unproject_to_z_plane (ev->area.x,
                                  ev->area.y,
                                  min_depth, &new_x, &new_y))
    horizon_problem = true;
  max_x = min_x = new_x;
  max_y = min_y = new_y;

  if (!ghid_unproject_to_z_plane (ev->area.x,
                                  ev->area.y,
                                  max_depth, &new_x, &new_y))
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  /* */
  if (!ghid_unproject_to_z_plane (ev->area.x + ev->area.width,
                                 ev->area.y,
                                 min_depth, &new_x, &new_y))
    horizon_problem = true;
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  if (!ghid_unproject_to_z_plane (ev->area.x + ev->area.width,
                                  ev->area.y,
                                  max_depth, &new_x, &new_y))
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  /* */
  if (!ghid_unproject_to_z_plane (ev->area.x + ev->area.width,
                                  ev->area.y + ev->area.height,
                                  min_depth, &new_x, &new_y))
    horizon_problem = true;
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  if (!ghid_unproject_to_z_plane (ev->area.x + ev->area.width,
                                  ev->area.y + ev->area.height,
                                  max_depth, &new_x, &new_y))
    horizon_problem = true;
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  /* */
  if (!ghid_unproject_to_z_plane (ev->area.x,
                                  ev->area.y + ev->area.height,
                                  min_depth,
                                  &new_x, &new_y))
    horizon_problem = true;
  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  if (!ghid_unproject_to_z_plane (ev->area.x,
                                  ev->area.y + ev->area.height,
                                  max_depth,
                                  &new_x, &new_y))
    horizon_problem = true;

  min_x = MIN (min_x, new_x);  max_x = MAX (max_x, new_x);
  min_y = MIN (min_y, new_y);  max_y = MAX (max_y, new_y);

  if (horizon_problem) {
    min_x = 0;
    min_y = 0;
    max_x = PCB->MaxWidth;
    max_y = PCB->MaxHeight;
  }

  region.X1 = min_x;  region.X2 = max_x + 1;
  region.Y1 = min_y;  region.Y2 = max_y + 1;

  glColor3f (port->bg_color.red / 65535.,
             port->bg_color.green / 65535.,
             port->bg_color.blue / 65535.);

  hidgl_init_triangle_array (&buffer);
  ghid_invalidate_current_gc ();

  /* Setup stenciling */
  /* Drawing operations set the stencil buffer to '1' */
  glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE); /* Stencil pass => replace stencil value (with 1) */
  /* Drawing operations as masked to areas where the stencil buffer is '0' */
  /* glStencilFunc (GL_GREATER, 1, 1); */           /* Draw only where stencil buffer is 0 */

#if 0
  if (global_view_2d) {
    glBegin (GL_QUADS);
    glVertex3i (0,             0,              0);
    glVertex3i (PCB->MaxWidth, 0,              0);
    glVertex3i (PCB->MaxWidth, PCB->MaxHeight, 0);
    glVertex3i (0,             PCB->MaxHeight, 0);
    glEnd ();
  } else {
    int solder_group;
    int component_group;
    int min_phys_group;
    int max_phys_group;
    int i;

    solder_group = GetLayerGroupNumberByNumber (solder_silk_layer);
    component_group = GetLayerGroupNumberByNumber (component_silk_layer);

    min_phys_group = MIN (solder_group, component_group);
    max_phys_group = MAX (solder_group, component_group);

    glBegin (GL_QUADS);
    for (i = min_phys_group; i <= max_phys_group; i++) {
      int depth = compute_depth (i);
      glVertex3i (0,             0,              depth);
      glVertex3i (PCB->MaxWidth, 0,              depth);
      glVertex3i (PCB->MaxWidth, PCB->MaxHeight, depth);
      glVertex3i (0,             PCB->MaxHeight, depth);
    }
    glEnd ();
  }
#endif

  ghid_draw_bg_image ();

  /* hid_expose_callback (&ghid_hid, &region, 0); */
  ghid_draw_everything (&region);
  hidgl_flush_triangles (&buffer);

  /* Just prod the drawing code so the current depth gets set to
     the right value for the layer we are editing */
  hidgl_set_depth (compute_depth (GetLayerGroupNumberByNumber (INDEXOFCURRENT)));
  ghid_draw_grid (&region);

  ghid_invalidate_current_gc ();

  DrawAttached ();
  DrawMark ();
  hidgl_flush_triangles (&buffer);

  ghid_show_crosshair (TRUE);

  hidgl_flush_triangles (&buffer);
  hidgl_finish_triangle_array (&buffer);

  check_gl_drawing_ok_hack = false;
  hidgl_in_context (false);
  ghid_end_drawing (port);

  g_timer_start (priv->time_since_expose);

  return FALSE;
}

/* This realize callback is used to work around a crash bug in some mesa
 * versions (observed on a machine running the intel i965 driver. It isn't
 * obvious why it helps, but somehow fiddling with the GL context here solves
 * the issue. The problem appears to have been fixed in recent mesa versions.
 */
void
ghid_port_drawing_realize_cb (GtkWidget *widget, gpointer data)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (widget);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
    return;

  gdk_gl_drawable_gl_end (gldrawable);
  return;
}

gboolean
ghid_pinout_preview_expose (GtkWidget *widget,
                            GdkEventExpose *ev)
{
  GdkGLContext* pGlContext = gtk_widget_get_gl_context (widget);
  GdkGLDrawable* pGlDrawable = gtk_widget_get_gl_drawable (widget);
  GhidPinoutPreview *pinout = GHID_PINOUT_PREVIEW (widget);
  double save_zoom;
  int da_w, da_h;
  int save_left, save_top;
  int save_width, save_height;
  int save_view_width, save_view_height;
  double xz, yz;

  save_zoom = gport->zoom;
  save_width = gport->width;
  save_height = gport->height;
  save_left = gport->view_x0;
  save_top = gport->view_y0;
  save_view_width = gport->view_width;
  save_view_height = gport->view_height;

  /* Setup zoom factor for drawing routines */

  gdk_window_get_geometry (widget->window, 0, 0, &da_w, &da_h, 0);
  xz = (double) pinout->x_max / da_w;
  yz = (double) pinout->y_max / da_h;
  if (xz > yz)
    gport->zoom = xz;
  else
    gport->zoom = yz;

  gport->width = da_w;
  gport->height = da_h;
  gport->view_width = da_w * gport->zoom;
  gport->view_height = da_h * gport->zoom;
  gport->view_x0 = (pinout->x_max - gport->view_width) / 2;
  gport->view_y0 = (pinout->y_max - gport->view_height) / 2;

  /* make GL-context "current" */
  if (!gdk_gl_drawable_gl_begin (pGlDrawable, pGlContext)) {
    return FALSE;
  }
  gport->render_priv->in_context = true;

  check_gl_drawing_ok_hack = true;
  hidgl_in_context (true);

  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport (0, 0, widget->allocation.width, widget->allocation.height);

  glEnable (GL_SCISSOR_TEST);
  glScissor (ev->area.x,
             widget->allocation.height - ev->area.height - ev->area.y,
             ev->area.width, ev->area.height);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, widget->allocation.width, widget->allocation.height, 0, -100000, 100000);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0f, 0.0f, -Z_NEAR);

  glClearColor (gport->bg_color.red / 65535.,
                gport->bg_color.green / 65535.,
                gport->bg_color.blue / 65535.,
                1.);
  glStencilMask (~0);
  glClearStencil (0);
  glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  hidgl_reset_stencil_usage ();

  /* call the drawing routine */
  hidgl_init_triangle_array (&buffer);
  ghid_invalidate_current_gc ();
  glPushMatrix ();
  glScalef ((ghid_flip_x ? -1. : 1.) / gport->zoom,
            (ghid_flip_y ? -1. : 1.) / gport->zoom, 1);
  glTranslatef (ghid_flip_x ? gport->view_x0 - PCB->MaxWidth  :
                             -gport->view_x0,
                ghid_flip_y ? gport->view_y0 - PCB->MaxHeight :
                             -gport->view_y0, 0);
  hid_expose_callback (&ghid_hid, NULL, &pinout->element);
  hidgl_flush_triangles (&buffer);
  glPopMatrix ();

  if (gdk_gl_drawable_is_double_buffered (pGlDrawable))
    gdk_gl_drawable_swap_buffers (pGlDrawable);
  else
    glFlush ();

  check_gl_drawing_ok_hack = false;
  hidgl_in_context (false);

  /* end drawing to current GL-context */
  gport->render_priv->in_context = false;
  gdk_gl_drawable_gl_end (pGlDrawable);

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
  GdkGLConfig *glconfig;
  GdkPixmap *pixmap;
  GdkGLPixmap *glpixmap;
  GdkGLContext* glcontext;
  GdkGLDrawable* gldrawable;
  double save_zoom;
  int save_left, save_top;
  int save_width, save_height;
  int save_view_width, save_view_height;
  BoxType region;
  bool save_check_gl_drawing_ok_hack;

  save_zoom = gport->zoom;
  save_width = gport->width;
  save_height = gport->height;
  save_left = gport->view_x0;
  save_top = gport->view_y0;
  save_view_width = gport->view_width;
  save_view_height = gport->view_height;

  /* Setup rendering context for drawing routines
   */

  glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGB     |
                                        GDK_GL_MODE_STENCIL |
                                        GDK_GL_MODE_SINGLE);

  pixmap = gdk_pixmap_new (NULL, width, height, depth);
  glpixmap = gdk_pixmap_set_gl_capability (pixmap, glconfig, NULL);
  gldrawable = GDK_GL_DRAWABLE (glpixmap);
  glcontext = gdk_gl_context_new (gldrawable, NULL, FALSE, GDK_GL_RGBA_TYPE);

  /* Setup zoom factor for drawing routines */

  gport->zoom = zoom;
  gport->width = width;
  gport->height = height;
  gport->view_width = width * gport->zoom;
  gport->view_height = height * gport->zoom;
  gport->view_x0 = ghid_flip_x ? PCB->MaxWidth - cx : cx;
  gport->view_x0 -= gport->view_height / 2;
  gport->view_y0 = ghid_flip_y ? PCB->MaxHeight - cy : cy;
  gport->view_y0 -= gport->view_width  / 2;

  /* make GL-context "current" */
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext)) {
    return NULL;
  }
  gport->render_priv->in_context = true;

  save_check_gl_drawing_ok_hack = check_gl_drawing_ok_hack;
  check_gl_drawing_ok_hack = true;
  hidgl_in_context (true);

  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport (0, 0, width, height);

  glEnable (GL_SCISSOR_TEST);
  glScissor (0, 0, width, height);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, width, height, 0, -100000, 100000);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0f, 0.0f, -Z_NEAR);

  glClearColor (gport->bg_color.red / 65535.,
                gport->bg_color.green / 65535.,
                gport->bg_color.blue / 65535.,
                1.);
  glStencilMask (~0);
  glClearStencil (0);
  glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  hidgl_reset_stencil_usage ();

  /* call the drawing routine */
  hidgl_init_triangle_array (&buffer);
  ghid_invalidate_current_gc ();
  glPushMatrix ();
  glScalef ((ghid_flip_x ? -1. : 1.) / gport->zoom,
            (ghid_flip_y ? -1. : 1.) / gport->zoom, 1);
  glTranslatef (ghid_flip_x ? gport->view_x0 - PCB->MaxWidth  :
                             -gport->view_x0,
                ghid_flip_y ? gport->view_y0 - PCB->MaxHeight :
                             -gport->view_y0, 0);
  region.X1 = MIN(Px(0), Px(gport->width + 1));
  region.Y1 = MIN(Py(0), Py(gport->height + 1));
  region.X2 = MAX(Px(0), Px(gport->width + 1));
  region.Y2 = MAX(Py(0), Py(gport->height + 1));
  hid_expose_callback (&ghid_hid, &region, NULL);
  hidgl_flush_triangles (&buffer);
  glPopMatrix ();

  glFlush ();

  check_gl_drawing_ok_hack = save_check_gl_drawing_ok_hack;
  hidgl_in_context (false);

  /* end drawing to current GL-context */
  gport->render_priv->in_context = false;
  gdk_gl_drawable_gl_end (gldrawable);

  gdk_pixmap_unset_gl_capability (pixmap);

  g_object_unref (glconfig);
  g_object_unref (glcontext);

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
  GHidPort *port = gport;
  GtkWidget *widget = port->drawing_area;

  ghid_start_drawing (port);

  glViewport (0, 0, widget->allocation.width, widget->allocation.height);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, widget->allocation.width, widget->allocation.height, 0, 0, 100);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0f, 0.0f, -Z_NEAR);

  hidgl_init_triangle_array (&buffer);
  ghid_invalidate_current_gc ();

  /* Setup stenciling */
  glDisable (GL_STENCIL_TEST);

  glPushMatrix ();
  glScalef ((ghid_flip_x ? -1. : 1.) / port->zoom,
            (ghid_flip_y ? -1. : 1.) / port->zoom,
            (ghid_flip_x == ghid_flip_y) ? 1. : -1.);
  glTranslatef (ghid_flip_x ? port->view_x0 - PCB->MaxWidth  :
                             -port->view_x0,
                ghid_flip_y ? port->view_y0 - PCB->MaxHeight :
                             -port->view_y0, 0);

  return &ghid_hid;
}

void
ghid_flush_debug_draw (void)
{
  GtkWidget *widget = gport->drawing_area;
  GdkGLDrawable *pGlDrawable = gtk_widget_get_gl_drawable (widget);

  hidgl_flush_triangles (&buffer);

  if (gdk_gl_drawable_is_double_buffered (pGlDrawable))
    gdk_gl_drawable_swap_buffers (pGlDrawable);
  else
    glFlush ();
}

void
ghid_finish_debug_draw (void)
{
  hidgl_flush_triangles (&buffer);
  glPopMatrix ();

  ghid_end_drawing (gport);
}

static double
determinant_2x2 (double m[2][2])
{
  double det;
  det = m[0][0] * m[1][1] -
        m[0][1] * m[1][0];
  return det;
}

#if 0
static float
determinant_4x4 (float m[4][4])
{
  float det;
  det = m[0][3] * m[1][2] * m[2][1] * m[3][0]-m[0][2] * m[1][3] * m[2][1] * m[3][0] -
        m[0][3] * m[1][1] * m[2][2] * m[3][0]+m[0][1] * m[1][3] * m[2][2] * m[3][0] +
        m[0][2] * m[1][1] * m[2][3] * m[3][0]-m[0][1] * m[1][2] * m[2][3] * m[3][0] -
        m[0][3] * m[1][2] * m[2][0] * m[3][1]+m[0][2] * m[1][3] * m[2][0] * m[3][1] +
        m[0][3] * m[1][0] * m[2][2] * m[3][1]-m[0][0] * m[1][3] * m[2][2] * m[3][1] -
        m[0][2] * m[1][0] * m[2][3] * m[3][1]+m[0][0] * m[1][2] * m[2][3] * m[3][1] +
        m[0][3] * m[1][1] * m[2][0] * m[3][2]-m[0][1] * m[1][3] * m[2][0] * m[3][2] -
        m[0][3] * m[1][0] * m[2][1] * m[3][2]+m[0][0] * m[1][3] * m[2][1] * m[3][2] +
        m[0][1] * m[1][0] * m[2][3] * m[3][2]-m[0][0] * m[1][1] * m[2][3] * m[3][2] -
        m[0][2] * m[1][1] * m[2][0] * m[3][3]+m[0][1] * m[1][2] * m[2][0] * m[3][3] +
        m[0][2] * m[1][0] * m[2][1] * m[3][3]-m[0][0] * m[1][2] * m[2][1] * m[3][3] -
        m[0][1] * m[1][0] * m[2][2] * m[3][3]+m[0][0] * m[1][1] * m[2][2] * m[3][3];
   return det;
}
#endif

static void
invert_2x2 (double m[2][2], double out[2][2])
{
  double scale = 1 / determinant_2x2 (m);
  out[0][0] =  m[1][1] * scale;
  out[0][1] = -m[0][1] * scale;
  out[1][0] = -m[1][0] * scale;
  out[1][1] =  m[0][0] * scale;
}

#if 0
static void
invert_4x4 (float m[4][4], float out[4][4])
{
  float scale = 1 / determinant_4x4 (m);

  out[0][0] = (m[1][2] * m[2][3] * m[3][1] - m[1][3] * m[2][2] * m[3][1] +
               m[1][3] * m[2][1] * m[3][2] - m[1][1] * m[2][3] * m[3][2] -
               m[1][2] * m[2][1] * m[3][3] + m[1][1] * m[2][2] * m[3][3]) * scale;
  out[0][1] = (m[0][3] * m[2][2] * m[3][1] - m[0][2] * m[2][3] * m[3][1] -
               m[0][3] * m[2][1] * m[3][2] + m[0][1] * m[2][3] * m[3][2] +
               m[0][2] * m[2][1] * m[3][3] - m[0][1] * m[2][2] * m[3][3]) * scale;
  out[0][2] = (m[0][2] * m[1][3] * m[3][1] - m[0][3] * m[1][2] * m[3][1] +
               m[0][3] * m[1][1] * m[3][2] - m[0][1] * m[1][3] * m[3][2] -
               m[0][2] * m[1][1] * m[3][3] + m[0][1] * m[1][2] * m[3][3]) * scale;
  out[0][3] = (m[0][3] * m[1][2] * m[2][1] - m[0][2] * m[1][3] * m[2][1] -
               m[0][3] * m[1][1] * m[2][2] + m[0][1] * m[1][3] * m[2][2] +
               m[0][2] * m[1][1] * m[2][3] - m[0][1] * m[1][2] * m[2][3]) * scale;
  out[1][0] = (m[1][3] * m[2][2] * m[3][0] - m[1][2] * m[2][3] * m[3][0] -
               m[1][3] * m[2][0] * m[3][2] + m[1][0] * m[2][3] * m[3][2] +
               m[1][2] * m[2][0] * m[3][3] - m[1][0] * m[2][2] * m[3][3]) * scale;
  out[1][1] = (m[0][2] * m[2][3] * m[3][0] - m[0][3] * m[2][2] * m[3][0] +
               m[0][3] * m[2][0] * m[3][2] - m[0][0] * m[2][3] * m[3][2] -
               m[0][2] * m[2][0] * m[3][3] + m[0][0] * m[2][2] * m[3][3]) * scale;
  out[1][2] = (m[0][3] * m[1][2] * m[3][0] - m[0][2] * m[1][3] * m[3][0] -
               m[0][3] * m[1][0] * m[3][2] + m[0][0] * m[1][3] * m[3][2] +
               m[0][2] * m[1][0] * m[3][3] - m[0][0] * m[1][2] * m[3][3]) * scale;
  out[1][3] = (m[0][2] * m[1][3] * m[2][0] - m[0][3] * m[1][2] * m[2][0] +
               m[0][3] * m[1][0] * m[2][2] - m[0][0] * m[1][3] * m[2][2] -
               m[0][2] * m[1][0] * m[2][3] + m[0][0] * m[1][2] * m[2][3]) * scale;
  out[2][0] = (m[1][1] * m[2][3] * m[3][0] - m[1][3] * m[2][1] * m[3][0] +
               m[1][3] * m[2][0] * m[3][1] - m[1][0] * m[2][3] * m[3][1] -
               m[1][1] * m[2][0] * m[3][3] + m[1][0] * m[2][1] * m[3][3]) * scale;
  out[2][1] = (m[0][3] * m[2][1] * m[3][0] - m[0][1] * m[2][3] * m[3][0] -
               m[0][3] * m[2][0] * m[3][1] + m[0][0] * m[2][3] * m[3][1] +
               m[0][1] * m[2][0] * m[3][3] - m[0][0] * m[2][1] * m[3][3]) * scale;
  out[2][2] = (m[0][1] * m[1][3] * m[3][0] - m[0][3] * m[1][1] * m[3][0] +
               m[0][3] * m[1][0] * m[3][1] - m[0][0] * m[1][3] * m[3][1] -
               m[0][1] * m[1][0] * m[3][3] + m[0][0] * m[1][1] * m[3][3]) * scale;
  out[2][3] = (m[0][3] * m[1][1] * m[2][0] - m[0][1] * m[1][3] * m[2][0] -
               m[0][3] * m[1][0] * m[2][1] + m[0][0] * m[1][3] * m[2][1] +
               m[0][1] * m[1][0] * m[2][3] - m[0][0] * m[1][1] * m[2][3]) * scale;
  out[3][0] = (m[1][2] * m[2][1] * m[3][0] - m[1][1] * m[2][2] * m[3][0] -
               m[1][2] * m[2][0] * m[3][1] + m[1][0] * m[2][2] * m[3][1] +
               m[1][1] * m[2][0] * m[3][2] - m[1][0] * m[2][1] * m[3][2]) * scale;
  out[3][1] = (m[0][1] * m[2][2] * m[3][0] - m[0][2] * m[2][1] * m[3][0] +
               m[0][2] * m[2][0] * m[3][1] - m[0][0] * m[2][2] * m[3][1] -
               m[0][1] * m[2][0] * m[3][2] + m[0][0] * m[2][1] * m[3][2]) * scale;
  out[3][2] = (m[0][2] * m[1][1] * m[3][0] - m[0][1] * m[1][2] * m[3][0] -
               m[0][2] * m[1][0] * m[3][1] + m[0][0] * m[1][2] * m[3][1] +
               m[0][1] * m[1][0] * m[3][2] - m[0][0] * m[1][1] * m[3][2]) * scale;
  out[3][3] = (m[0][1] * m[1][2] * m[2][0] - m[0][2] * m[1][1] * m[2][0] +
               m[0][2] * m[1][0] * m[2][1] - m[0][0] * m[1][2] * m[2][1] -
               m[0][1] * m[1][0] * m[2][2] + m[0][0] * m[1][1] * m[2][2]) * scale;
}
#endif


bool
ghid_unproject_to_z_plane (int ex, int ey, int vz, int *vx, int *vy)
{
  double mat[2][2];
  double inv_mat[2][2];
  double x, y;
  double fvz;
  double vpx, vpy;
  double fvx, fvy;
  GtkWidget *widget = gport->drawing_area;

  /* FIXME: Dirty kludge.. I know what our view parameters are here */
  double aspect = (double)widget->allocation.width / (double)widget->allocation.height;
  double width = 2. * aspect;
  double height = 2.;
  double near = 1.;
  // double far = 24.;

  /* This is nasty beyond words, but I'm lazy and translating directly
   * from some untested maths I derived which used this notation */
  double A, B, C, D, E, F, G, H, I, J, K, L;

  /* NB: last_modelview_matrix is transposed in memory! */
  A = last_modelview_matrix[0][0];
  B = last_modelview_matrix[1][0];
  C = last_modelview_matrix[2][0];
  D = last_modelview_matrix[3][0];
  E = last_modelview_matrix[0][1];
  F = last_modelview_matrix[1][1];
  G = last_modelview_matrix[2][1];
  H = last_modelview_matrix[3][1];
  I = last_modelview_matrix[0][2];
  J = last_modelview_matrix[1][2];
  K = last_modelview_matrix[2][2];
  L = last_modelview_matrix[3][2];
  /* I could assert that the last row is (as assumed) [0 0 0 1], but again.. I'm lazy */

  /* Convert from event coordinates to viewport coordinates */
  vpx = (float)ex / (float)widget->allocation.width * 2. - 1.;
  vpy = (float)ey / (float)widget->allocation.height * 2. - 1.;

  /* Convert our model space Z plane coordinte to float for convenience */
  fvz = (float)vz;

  /* This isn't really X and Y? */
  x = (C * fvz + D) * 2. / width  * near + vpx * (K * fvz + L);
  y = (G * fvz + H) * 2. / height * near + vpy * (K * fvz + L);

  mat[0][0] = -vpx * I - A * 2 / width / near;
  mat[0][1] = -vpx * J - B * 2 / width / near;
  mat[1][0] = -vpy * I - E * 2 / height / near;
  mat[1][1] = -vpy * J - F * 2 / height / near;

//  if (fabs (determinant_2x2 (mat)) < 0.000000000001)
//    printf ("Determinant is quite small\n");

  invert_2x2 (mat, inv_mat);

  fvx = (inv_mat[0][0] * x + inv_mat[0][1] * y);
  fvy = (inv_mat[1][0] * x + inv_mat[1][1] * y);

//  if (fvx == NAN) printf ("fvx is NAN\n");
//  if (fvy == NAN) printf ("fvx is NAN\n");

//  if (fabs (fvx) == INFINITY) printf ("fvx is infinite %f\n", fvx);
//  if (fabs (fvy) == INFINITY) printf ("fvy is infinite %f\n", fvy);

//  if (fvx > (double)G_MAXINT/5.) {fvx = (double)G_MAXINT/5.; printf ("fvx overflow clamped\n"); }
//  if (fvy > (double)G_MAXINT/5.) {fvy = (double)G_MAXINT/5.; printf ("fvy overflow clamped\n"); }

//  if (fvx < (double)-G_MAXINT/5.) {fvx = (double)-G_MAXINT/5.; printf ("fvx underflow clamped\n"); }
//  if (fvy < (double)-G_MAXINT/5.) {fvy = (double)-G_MAXINT/5.; printf ("fvy underflow clamped\n"); }

  *vx = (int)fvx;
  *vy = (int)fvy;

  {
    float ex, ey, ez;
    /* Reproject the computed board plane coordinates to eye space */
    ex = last_modelview_matrix[0][0] * fvx + last_modelview_matrix[1][0] * fvy + last_modelview_matrix[2][0] * fvz + last_modelview_matrix[3][0];
    ey = last_modelview_matrix[0][1] * fvx + last_modelview_matrix[1][1] * fvy + last_modelview_matrix[2][1] * fvz + last_modelview_matrix[3][1];
    ez = last_modelview_matrix[0][2] * fvx + last_modelview_matrix[1][2] * fvy + last_modelview_matrix[2][2] * fvz + last_modelview_matrix[3][2];
    /* We don't care about ew, as we don't use anything other than 1 for homogeneous coordinates at this stage */
    /* ew = last_modelview_matrix[0][3] * fvx + last_modelview_matrix[1][3] * fvy + last_modelview_matrix[2][3] * fvz + last_modelview_matrix[3][3]; */

#if 0
    if (-ez < near)
      printf ("ez is closer than the near clipping plane, ez = %f\n", ez);
    if (-ez > far)
      printf ("ez is further than the near clipping plane, ez = %f\n", ez);
#endif
    if (-ez < 0) {
      // printf ("EZ IS BEHIND THE CAMERA !! ez = %f\n", ez);
      return false;
    }

    return true;
  }
}


void
ghid_view_2d (void *ball, gboolean view_2d, gpointer userdata)
{
  global_view_2d = view_2d;
  ghid_invalidate_all ();
}

void
ghid_port_rotate (void *ball, float *quarternion, gpointer userdata)
{
#ifdef DEBUG_ROTATE
  int row, column;
#endif

  build_rotmatrix (view_matrix, quarternion);

#ifdef DEBUG_ROTATE
  for (row = 0; row < 4; row++) {
    printf ("[ %f", view_matrix[row][0]);
    for (column = 1; column < 4; column++) {
      printf (",\t%f", view_matrix[row][column]);
    }
    printf ("\t]\n");
  }
  printf ("\n");
#endif

  ghid_invalidate_all ();
}
