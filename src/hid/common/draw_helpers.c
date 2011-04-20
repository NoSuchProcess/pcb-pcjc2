#include "global.h"
#include "hid.h"
#include "polygon.h"

static void
fill_contour (hidGC gc, PLINE *pl)
{
  int *x, *y, n, i = 0;
  VNODE *v;

  n = pl->Count;
  x = (int *)malloc (n * sizeof (int));
  y = (int *)malloc (n * sizeof (int));

  for (v = &pl->head; i < n; v = v->next)
    {
      x[i] = v->point[0];
      y[i++] = v->point[1];
    }

  gui->fill_polygon (gc, n, x, y);

  free (x);
  free (y);
}

static void
thindraw_contour (hidGC gc, PLINE *pl)
{
  VNODE *v;
  int last_x, last_y;
  int this_x, this_y;

  gui->set_line_width (gc, 0);
  gui->set_line_cap (gc, Round_Cap);

  /* If the contour is round, use an arc drawing routine. */
  if (pl->is_round)
    {
      gui->draw_arc (gc, pl->cx, pl->cy, pl->radius, pl->radius, 0, 360);
      return;
    }

  /* Need at least two points in the contour */
  if (pl->head.next == NULL)
    return;

  last_x = pl->head.point[0];
  last_y = pl->head.point[1];
  v = pl->head.next;

  do
    {
      this_x = v->point[0];
      this_y = v->point[1];

      gui->draw_line (gc, last_x, last_y, this_x, this_y);
      // gui->fill_circle (gc, this_x, this_y, 30);

      last_x = this_x;
      last_y = this_y;
    }
  while ((v = v->next) != pl->head.next);
}

static void
fill_contour_cb (PLINE *pl, void *user_data)
{
  hidGC gc = (hidGC)user_data;
  PLINE *local_pl = pl;

  fill_contour (gc, pl);
  poly_FreeContours (&local_pl);
}

static void
fill_clipped_contour (hidGC gc, PLINE *pl, const BoxType *clip_box)
{
  PLINE *pl_copy;
  POLYAREA *clip_poly;
  POLYAREA *piece_poly;
  POLYAREA *clipped_pieces;
  POLYAREA *draw_piece;
  int x;

  clip_poly = RectPoly (clip_box->X1, clip_box->X2,
                        clip_box->Y1, clip_box->Y2);
  poly_CopyContour (&pl_copy, pl);
  piece_poly = poly_Create ();
  poly_InclContour (piece_poly, pl_copy);
  x = poly_Boolean_free (piece_poly, clip_poly,
                         &clipped_pieces, PBO_ISECT);
  if (x != err_ok || clipped_pieces == NULL)
    return;

  draw_piece = clipped_pieces;
  do
    {
      /* NB: The polygon won't have any holes in it */
      fill_contour (gc, draw_piece->contours);
    }
  while ((draw_piece = draw_piece->f) != clipped_pieces);
  poly_Free (&clipped_pieces);
}

/* If at least 50% of the bounding box of the polygon is on the screen,
 * lets compute the complete no-holes polygon.
 */
#define BOUNDS_INSIDE_CLIP_THRESHOLD 0.5
static int
should_compute_no_holes (PolygonType *poly, const BoxType *clip_box)
{
  int x1, x2, y1, y2;
  float poly_bounding_area;
  float clipped_poly_area;

  /* If there is no passed clip box, compute the whole thing */
  if (clip_box == NULL)
    return 1;

  x1 = MAX (poly->BoundingBox.X1, clip_box->X1);
  x2 = MIN (poly->BoundingBox.X2, clip_box->X2);
  y1 = MAX (poly->BoundingBox.Y1, clip_box->Y1);
  y2 = MIN (poly->BoundingBox.Y2, clip_box->Y2);

  /* Check if the polygon is outside the clip box */
  if ((x2 <= x1) || (y2 <= y1))
    return 0;

  poly_bounding_area = (float)(poly->BoundingBox.X2 - poly->BoundingBox.X1) *
                       (float)(poly->BoundingBox.Y2 - poly->BoundingBox.Y1);

  clipped_poly_area = (float)(x2 - x1) * (float)(y2 - y1);

  if (clipped_poly_area / poly_bounding_area >= BOUNDS_INSIDE_CLIP_THRESHOLD)
    return 1;

  return 0;
}
#undef BOUNDS_INSIDE_CLIP_THRESHOLD

void
common_fill_pcb_polygon (hidGC gc, PolygonType *poly, const BoxType *clip_box)
{
  /* FIXME: We aren't checking the gui's dicer flag..
            we are dicing for every case. Some GUIs
            rely on this, and need their flags fixing. */

  if (!poly->NoHolesValid)
    {
      /* If enough of the polygon is on-screen, compute the entire
       * NoHoles version and cache it for later rendering, otherwise
       * just compute what we need to render now.
       */
      if (should_compute_no_holes (poly, clip_box))
        ComputeNoHoles (poly);
      else
        NoHolesPolygonDicer (poly, clip_box, fill_contour_cb, gc);
    }
  if (poly->NoHolesValid && poly->NoHoles)
    {
      PLINE *pl;

      for (pl = poly->NoHoles; pl != NULL; pl = pl->next)
        {
          if (clip_box == NULL)
            fill_contour (gc, pl);
          else
            fill_clipped_contour (gc, pl, clip_box);
        }
    }

  /* Draw other parts of the polygon if fullpoly flag is set */
  /* NB: No "NoHoles" cache for these */
  if (TEST_FLAG (FULLPOLYFLAG, poly))
    {
      PolygonType p = *poly;

      for (p.Clipped = poly->Clipped->f;
           p.Clipped != poly->Clipped;
           p.Clipped = p.Clipped->f)
        NoHolesPolygonDicer (&p, clip_box, fill_contour_cb, gc);
    }
}

static int
thindraw_hole_cb (PLINE *pl, void *user_data)
{
  hidGC gc = (hidGC)user_data;
  thindraw_contour (gc, pl);
  return 0;
}

void
common_thindraw_pcb_polygon (hidGC gc, PolygonType *poly,
                             const BoxType *clip_box)
{
  thindraw_contour (gc, poly->Clipped->contours);
  PolygonHoles (poly, clip_box, thindraw_hole_cb, gc);
}

void
common_thindraw_pcb_pad (hidGC gc, PadType *pad, bool clear, bool mask)
{
  int w = clear ? (mask ? pad->Mask
                        : pad->Thickness + pad->Clearance)
                : pad->Thickness;
  int x1, y1, x2, y2;
  int t = w / 2;
  x1 = pad->Point1.X;
  y1 = pad->Point1.Y;
  x2 = pad->Point2.X;
  y2 = pad->Point2.Y;
  if (x1 > x2 || y1 > y2)
    {
      int temp_x = x1;
      int temp_y = y1;
      x1 = x2; x2 = temp_x;
      y1 = y2; y2 = temp_y;
    }
  gui->set_line_cap (gc, Round_Cap);
  gui->set_line_width (gc, 0);
  if (TEST_FLAG (SQUAREFLAG, pad))
    {
      /* slanted square pad */
      float tx, ty, theta;

      if (x1 == x2 || y1 == y2)
        theta = 0;
      else
        theta = atan2 (y2 - y1, x2 - x1);

      /* T is a vector half a thickness long, in the direction of
         one of the corners.  */
      tx = t * cos (theta + M_PI / 4) * sqrt (2.0);
      ty = t * sin (theta + M_PI / 4) * sqrt (2.0);

      gui->draw_line (gc, x1 - tx, y1 - ty, x2 + ty, y2 - tx);
      gui->draw_line (gc, x2 + ty, y2 - tx, x2 + tx, y2 + ty);
      gui->draw_line (gc, x2 + tx, y2 + ty, x1 - ty, y1 + tx);
      gui->draw_line (gc, x1 - ty, y1 + tx, x1 - tx, y1 - ty);
    }
  else if (x1 == x2 && y1 == y2)
    {
      gui->draw_arc (gc, x1, y1, t, t, 0, 360);
    }
  else
    {
      /* Slanted round-end pads.  */
      LocationType dx, dy, ox, oy;
      float h;

      dx = x2 - x1;
      dy = y2 - y1;
      h = t / sqrt (SQUARE (dx) + SQUARE (dy));
      ox = dy * h + 0.5 * SGN (dy);
      oy = -(dx * h + 0.5 * SGN (dx));

      gui->draw_line (gc, x1 + ox, y1 + oy, x2 + ox, y2 + oy);

      if (abs (ox) >= pixel_slop || abs (oy) >= pixel_slop)
        {
          LocationType angle = atan2 (dx, dy) * 57.295779;
          gui->draw_line (gc, x1 - ox, y1 - oy, x2 - ox, y2 - oy);
          gui->draw_arc (gc, x1, y1, t, t, angle - 180, 180);
          gui->draw_arc (gc, x2, y2, t, t, angle, 180);
        }
    }
}

void
common_fill_pcb_pad (hidGC gc, PadType *pad, bool clear, bool mask)
{
  int w = clear ? (mask ? pad->Mask
                        : pad->Thickness + pad->Clearance)
                : pad->Thickness;

  if (pad->Point1.X == pad->Point2.X &&
      pad->Point1.Y == pad->Point2.Y)
    {
      if (TEST_FLAG (SQUAREFLAG, pad))
        {
          int l, r, t, b;
          l = pad->Point1.X - w / 2;
          b = pad->Point1.Y - w / 2;
          r = l + w;
          t = b + w;
          gui->fill_rect (gc, l, b, r, t);
        }
      else
        {
          gui->fill_circle (gc, pad->Point1.X, pad->Point1.Y, w / 2);
        }
    }
  else
    {
      gui->set_line_cap (gc, TEST_FLAG (SQUAREFLAG, pad) ?
                               Square_Cap : Round_Cap);
      gui->set_line_width (gc, w);

      gui->draw_line (gc, pad->Point1.X, pad->Point1.Y,
                          pad->Point2.X, pad->Point2.Y);
    }
}

void
common_draw_helpers_init (HID *hid)
{
  hid->fill_pcb_polygon     = common_fill_pcb_polygon;
  hid->thindraw_pcb_polygon = common_thindraw_pcb_polygon;
  hid->fill_pcb_pad         = common_fill_pcb_pad;
  hid->thindraw_pcb_pad     = common_thindraw_pcb_pad;
}
