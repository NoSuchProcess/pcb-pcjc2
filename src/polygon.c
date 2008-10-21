/* $Id$ */

/*
 *                            COPYRIGHT
 *
 *  PCB, interactive printed circuit board design
 *  Copyright (C) 1994,1995,1996 Thomas Nau
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Contact addresses for paper mail and Email:
 *  Thomas Nau, Schlehenweg 15, 88471 Baustetten, Germany
 *  Thomas.Nau@rz.uni-ulm.de
 *
 */


/* special polygon editing routines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <setjmp.h>

#include "global.h"
#include "box.h"
#include "create.h"
#include "crosshair.h"
#include "data.h"
#include "draw.h"
#include "error.h"
#include "find.h"
#include "misc.h"
#include "move.h"
#include "polygon.h"
#include "remove.h"
#include "rtree.h"
#include "search.h"
#include "set.h"
#include "thermal.h"
#include "undo.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

RCSID ("$Id$");

#define ROUND(x) ((long)(((x) >= 0 ? (x) + 0.5  : (x) - 0.5)))

/* ---------------------------------------------------------------------------
 * local prototypes
 */

#define CIRC_SEGS 36
static double circleVerticies[] = {
  1.0, 0.0,
  0.98480775301221, 0.17364817766693,
};

static void
add_noholes_polyarea (PolygonType *noholes_poly, void *user_data)
{
  PolygonType *poly = user_data;
  PLINE *pline;
  POLYAREA *new_area;

  new_area = malloc (sizeof (POLYAREA) * 1);
  new_area->contour_tree = r_create_tree (NULL, 0, 0);

  /* Allocate a new PLINE, COPY the PLINE from the passed polygon */
  poly_CopyContour (&pline, noholes_poly->Clipped->contours);
  new_area->contours = pline;
  r_insert_entry (new_area->contour_tree, (BoxType *)pline, 0);

  /* Link the new POLYAREA into the NoHoles circularaly linked list */
  if (poly->NoHoles)
    {
      new_area->f = poly->NoHoles;
      new_area->b = poly->NoHoles->b;
      poly->NoHoles->b->f = new_area;
      poly->NoHoles->b = new_area;
    }
  else
    {
      new_area->f = new_area;
      new_area->b = new_area;
    }

  poly->NoHoles = new_area;
}

void
ComputeNoHoles (PolygonType *poly)
{
  if (poly->NoHoles)
    poly_Free (&poly->NoHoles);
  poly->NoHoles = NULL;
  if (poly->Clipped)
    NoHolesPolygonDicer (poly, add_noholes_polyarea, poly, NULL);
  else
    printf ("Compute_noholes caught poly->Clipped = NULL\n");
  poly->NoHolesValid = 1;
}

#if 0
static POLYAREA *
biggest (POLYAREA * p)
{
  POLYAREA *n, *top = NULL;
  PLINE *pl;
  double big = -1;
  if (!p)
    return NULL;
  n = p;
  do
    {
#if 0
      if (n->contours->area < PCB->IsleArea)
        {
          n->b->f = n->f;
          n->f->b = n->b;
          poly_DelContour (&n->contours);
          if (n == p)
            p = n->f;
          n = n->f;
          if (!n->contours)
            {
              free (n);
              return NULL;
            }
        }
#endif
      if (n->contours->area > big)
        {
          top = n;
          big = n->contours->area;
        }
    }
  while ((n = n->f) != p);
  assert (top);
  if (top == p)
    return p;
  pl = top->contours;
  top->contours = p->contours;
  p->contours = pl;
  assert (pl);
  assert (p->f);
  assert (p->b);
  return p;
}
#endif

POLYAREA *
ContourToPoly (PLINE * contour)
{
  POLYAREA *p;
  poly_PreContour (contour, TRUE);
  poly_ChkContour (contour);
  assert (contour->Flags.orient == PLF_DIR);
  if (!(p = poly_Create ()))
    return NULL;
  poly_InclContour (p, contour);
  assert (poly_Valid (p));
  return p;
}

#warning FIXME Later
#if 0
static POLYAREA *
original_poly (PourType * p)
{
  return NULL;
  PLINE *contour = NULL;
  POLYAREA *np = NULL;
  Vector v;

  /* first make initial pour contour */
  POLYGONPOINT_LOOP (p);
  {
    v[0] = point->X;
    v[1] = point->Y;
    if (contour == NULL)
      {
        if ((contour = poly_NewContour (v)) == NULL)
          return NULL;
      }
    else
      {
        poly_InclVertex (contour->head.prev, poly_CreateNode (v));
      }
  }
  END_LOOP;
  if (contour == NULL)
    {
      printf ("How did that escape - did the loop iterate zero times??\n");
      POLYGONPOINT_LOOP (p);
        {
          printf ("Hello\n");
        }
      END_LOOP;
      return NULL;
    }
  poly_PreContour (contour, TRUE);
  /* make sure it is a positive contour */
  if ((contour->Flags.orient) != PLF_DIR)
    poly_InvContour (contour);
  assert ((contour->Flags.orient) == PLF_DIR);
  if ((np = poly_Create ()) == NULL)
    return NULL;
  poly_InclContour (np, contour);
  assert (poly_Valid (np));
  return biggest (np);
}
#endif

#if 0
static int
ClipOriginal (PourType * poly)
{
  POLYAREA *p, *result;
  int r;

  p = original_poly (poly);
  r = poly_Boolean_free (poly->Clipped, p, &result, PBO_ISECT);
  if (r != err_ok)
    {
      fprintf (stderr, "Error while clipping PBO_ISECT: %d\n", r);
      poly_Free (&result);
      poly->Clipped = NULL;
      if (poly->NoHoles) printf ("Just leaked in ClipOriginal\n");
      poly->NoHoles = NULL;
      return 0;
    }
#warning FIXME Later
//  poly->Clipped = biggest (result);
  assert (!poly->Clipped || poly_Valid (poly->Clipped));
  return 1;
}
#endif

POLYAREA *
RectPoly (LocationType x1, LocationType x2, LocationType y1, LocationType y2)
{
  PLINE *contour = NULL;
  Vector v;

  assert (x2 > x1);
  assert (y2 > y1);
  v[0] = x1;
  v[1] = y1;
  if ((contour = poly_NewContour (v)) == NULL)
    return NULL;
  v[0] = x2;
  v[1] = y1;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x2;
  v[1] = y2;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x1;
  v[1] = y2;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  return ContourToPoly (contour);
}

POLYAREA *
OctagonPoly (LocationType x, LocationType y, BDimension radius)
{
  PLINE *contour = NULL;
  Vector v;

  v[0] = x + ROUND (radius * 0.5);
  v[1] = y + ROUND (radius * TAN_22_5_DEGREE_2);
  if ((contour = poly_NewContour (v)) == NULL)
    return NULL;
  v[0] = x + ROUND (radius * TAN_22_5_DEGREE_2);
  v[1] = y + ROUND (radius * 0.5);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x - (v[0] - x);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x - ROUND (radius * 0.5);
  v[1] = y + ROUND (radius * TAN_22_5_DEGREE_2);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[1] = y - (v[1] - y);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x - ROUND (radius * TAN_22_5_DEGREE_2);
  v[1] = y - ROUND (radius * 0.5);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x - (v[0] - x);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = x + ROUND (radius * 0.5);
  v[1] = y - ROUND (radius * TAN_22_5_DEGREE_2);
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  return ContourToPoly (contour);
}

/* add verticies in a fractional-circle starting from v 
 * centered at X, Y and going counter-clockwise
 * does not include the first point
 * last argument is 1 for a full circle
 * 2 for a half circle
 * or 4 for a quarter circle
 */
void
frac_circle (PLINE * c, LocationType X, LocationType Y, Vector v, int range)
{
  double e1, e2, t1;
  int i;

  poly_InclVertex (c->head.prev, poly_CreateNode (v));
  /* move vector to origin */
  e1 = v[0] - X;
  e2 = v[1] - Y;

  range = range == 1 ? CIRC_SEGS-1 : (CIRC_SEGS / range);
  for (i = 0; i < range; i++)
    {
      /* rotate the vector */
      t1 = e1 * circleVerticies[2] - e2 * circleVerticies[3];
      e2 = e1 * circleVerticies[3] + e2 * circleVerticies[2];
      e1 = t1;
      v[0] = X + ROUND (e1);
      v[1] = Y + ROUND (e2);
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
    }
}

#define COARSE_CIRCLE 0
/* create a 35-vertex circle approximation */
POLYAREA *
CirclePoly (LocationType x, LocationType y, BDimension radius)
{
  PLINE *contour;
  Vector v;

  if (radius <= 0)
    return NULL;
  v[0] = x + radius;
  v[1] = y;
  if ((contour = poly_NewContour (v)) == NULL)
    return NULL;
  frac_circle (contour, x, y, v, 1);
  return ContourToPoly (contour);
}

/* make a rounded-corner rectangle with radius t beyond x1,x2,y1,y2 rectangle */
POLYAREA *
RoundRect (LocationType x1, LocationType x2, LocationType y1, LocationType y2,
           BDimension t)
{
  PLINE *contour = NULL;
  Vector v;

  assert (x2 > x1);
  assert (y2 > y1);
  v[0] = x1 - t;
  v[1] = y1;
  if ((contour = poly_NewContour (v)) == NULL)
    return NULL;
  frac_circle (contour, x1, y1, v, 4);
  v[0] = x2;
  v[1] = y1 - t;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, x2, y1, v, 4);
  v[0] = x2 + t;
  v[1] = y2;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, x2, y2, v, 4);
  v[0] = x1;
  v[1] = y2 + t;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, x1, y2, v, 4);
  return ContourToPoly (contour);
}

#define ARC_ANGLE 5
POLYAREA *
ArcPoly (ArcType * a, BDimension thick)
{
  PLINE *contour = NULL;
  POLYAREA *np = NULL;
  Vector v;
  BoxType *ends;
  int i, segs;
  double ang, da, rx, ry;
  long half;

  if (thick <= 0)
    return NULL;
  if (a->Delta < 0)
    {
      a->StartAngle += a->Delta;
      a->Delta = -a->Delta;
    }
  half = (thick + 1) / 2;
  ends = GetArcEnds (a);
  /* start with inner radius */
  rx = MAX (a->Width - half, 0);
  ry = MAX (a->Height - half, 0);
  segs = a->Delta / ARC_ANGLE;
  ang = a->StartAngle;
  da = (1.0 * a->Delta) / segs;
  v[0] = a->X - rx * cos (ang * M180);
  v[1] = a->Y + ry * sin (ang * M180);
  if ((contour = poly_NewContour (v)) == NULL)
    return 0;
  for (i = 0; i < segs - 1; i++)
    {
      ang += da;
      v[0] = a->X - rx * cos (ang * M180);
      v[1] = a->Y + ry * sin (ang * M180);
      poly_InclVertex (contour->head.prev, poly_CreateNode (v));
    }
  /* find last point */
  ang = a->StartAngle + a->Delta;
  v[0] = a->X - rx * cos (ang * M180);
  v[1] = a->Y + ry * sin (ang * M180);
  /* add the round cap at the end */
  frac_circle (contour, ends->X2, ends->Y2, v, 2);
  /* and now do the outer arc (going backwards) */
  rx = a->Width + half;
  ry = a->Width + half;
  da = -da;
  for (i = 0; i < segs; i++)
    {
      v[0] = a->X - rx * cos (ang * M180);
      v[1] = a->Y + ry * sin (ang * M180);
      poly_InclVertex (contour->head.prev, poly_CreateNode (v));
      ang += da;
    }
  /* now add other round cap */
  ang = a->StartAngle;
  v[0] = a->X - rx * cos (ang * M180);
  v[1] = a->Y + ry * sin (ang * M180);
  frac_circle (contour, ends->X1, ends->Y1, v, 2);
  /* now we have the whole contour */
  if (!(np = ContourToPoly (contour)))
    return NULL;
  return np;
}

POLYAREA *
LinePoly (LineType * L, BDimension thick)
{
  PLINE *contour = NULL;
  POLYAREA *np = NULL;
  Vector v;
  double d, dx, dy;
  long half;LineType _l=*L,*l=&_l;

  if (thick <= 0)
    return NULL;
  half = (thick + 1) / 2;
  d =
    sqrt (SQUARE (l->Point1.X - l->Point2.X) +
          SQUARE (l->Point1.Y - l->Point2.Y));
  if (!TEST_FLAG (SQUAREFLAG,l))
    if (d == 0)                   /* line is a point */
      return CirclePoly (l->Point1.X, l->Point1.Y, half);
  if (d != 0)
    {
      d = half / d;
      dx = (l->Point1.Y - l->Point2.Y) * d;
      dy = (l->Point2.X - l->Point1.X) * d;
    }
  else
    {
      dx = half;
      dy = 0;
    }
  if (TEST_FLAG (SQUAREFLAG,l))/* take into account the ends */
    {
      l->Point1.X -= dy;
      l->Point1.Y += dx;
      l->Point2.X += dy;
      l->Point2.Y -= dx;
    }
  v[0] = l->Point1.X - dx;
  v[1] = l->Point1.Y - dy;
  if ((contour = poly_NewContour (v)) == NULL)
    return 0;
  v[0] = l->Point2.X - dx;
  v[1] = l->Point2.Y - dy;
  if (TEST_FLAG (SQUAREFLAG,l))
    poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  else
    frac_circle (contour, l->Point2.X, l->Point2.Y, v, 2);
  v[0] = l->Point2.X + dx;
  v[1] = l->Point2.Y + dy;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  v[0] = l->Point1.X + dx;
  v[1] = l->Point1.Y + dy;
  if (TEST_FLAG (SQUAREFLAG,l))
    poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  else
    frac_circle (contour, l->Point1.X, l->Point1.Y, v, 2);
  /* now we have the line contour */
  if (!(np = ContourToPoly (contour)))
    return NULL;
  return np;
}

/* make a rounded-corner rectangle */
POLYAREA *
SquarePadPoly (PadType * pad, BDimension clear)
{
  PLINE *contour = NULL;
  POLYAREA *np = NULL;
  Vector v;
  double d;
  double tx, ty;
  double cx, cy;
  PadType _t=*pad,*t=&_t;
  PadType _c=*pad,*c=&_c;
  int halfthick = (pad->Thickness + 1) / 2;
  int halfclear = (clear + 1) / 2;

  d =
    sqrt (SQUARE (pad->Point1.X - pad->Point2.X) +
          SQUARE (pad->Point1.Y - pad->Point2.Y));
  if (d != 0)
    {
      double a = halfthick / d;
      tx = (t->Point1.Y - t->Point2.Y) * a;
      ty = (t->Point2.X - t->Point1.X) * a;
      a = halfclear / d;
      cx = (c->Point1.Y - c->Point2.Y) * a;
      cy = (c->Point2.X - c->Point1.X) * a;

      t->Point1.X -= ty;
      t->Point1.Y += tx;
      t->Point2.X += ty;
      t->Point2.Y -= tx;
      c->Point1.X -= cy;
      c->Point1.Y += cx;
      c->Point2.X += cy;
      c->Point2.Y -= cx;
    }
  else
    {
      tx = halfthick;
      ty = 0;
      cx = halfclear;
      cy = 0;

      t->Point1.Y += tx;
      t->Point2.Y -= tx;
      c->Point1.Y += cx;
      c->Point2.Y -= cx;
    }

  v[0] = c->Point1.X - tx;
  v[1] = c->Point1.Y - ty;
  if ((contour = poly_NewContour (v)) == NULL)
    return 0;
  frac_circle (contour, (t->Point1.X - tx), (t->Point1.Y - ty), v, 4);

  v[0] = t->Point2.X - cx;
  v[1] = t->Point2.Y - cy;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, (t->Point2.X - tx), (t->Point2.Y - ty), v, 4);

  v[0] = c->Point2.X + tx;
  v[1] = c->Point2.Y + ty;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, (t->Point2.X + tx), (t->Point2.Y + ty), v, 4);

  v[0] = t->Point1.X + cx;
  v[1] = t->Point1.Y + cy;
  poly_InclVertex (contour->head.prev, poly_CreateNode (v));
  frac_circle (contour, (t->Point1.X + tx), (t->Point1.Y + ty), v, 4);

  /* now we have the line contour */
  if (!(np = ContourToPoly (contour)))
    return NULL;
  return np;
}

#warning FIXME Later
#if 0
/* clear np1 from the polygon */
static int
Subtract (POLYAREA * np1, PolygonType * p, Boolean fnp)
{
  POLYAREA *merged = NULL, *np = np1;
  int x;
  assert (np);
  assert (p);
  if (!p->Clipped)
    {
      if (fnp)
        poly_Free (&np);
      return 1;
    }
  assert (poly_Valid (p->Clipped));
  assert (poly_Valid (np));
  if (fnp)
    x = poly_Boolean_free (p->Clipped, np, &merged, PBO_SUB);
  else
    {
      x = poly_Boolean (p->Clipped, np, &merged, PBO_SUB);
      poly_Free (&p->Clipped);
    }
  assert (!merged || poly_Valid (merged));
  if (x != err_ok)
    {
      fprintf (stderr, "Error while clipping PBO_SUB: %d\n", x);
      poly_Free (&merged);
      p->Clipped = NULL;
      if (p->NoHoles) printf ("Just leaked in Subtract\n");
      p->NoHoles = NULL;
      return -1;
    }
#warning FIXME Later
//  p->Clipped = biggest (merged);
  p->Clipped = merged;
  assert (!p->Clipped || poly_Valid (p->Clipped));
  if (!p->Clipped)
    Message ("Polygon cleared out of existence near (%d, %d)\n",
             (p->BoundingBox.X1 + p->BoundingBox.X2) / 2,
             (p->BoundingBox.Y1 + p->BoundingBox.Y2) / 2);
  return 1;
}
#endif

/* create a polygon of the pin clearance */
POLYAREA *
PinPoly (PinType * pin, BDimension thick, BDimension clear)
{
  int size;

  if (TEST_FLAG (SQUAREFLAG, pin))
    {
      size = (thick + 1) / 2;
      return RoundRect (pin->X - size, pin->X + size, pin->Y - size,
                        pin->Y + size, (clear + 1) / 2);
    }
  else
    {
      size = (thick + clear + 1) / 2;
      if (TEST_FLAG (OCTAGONFLAG, pin))
        {
          return OctagonPoly (pin->X, pin->Y, size + size);
        }
    }
  return CirclePoly (pin->X, pin->Y, size);
}

POLYAREA *
BoxPolyBloated (BoxType *box, BDimension bloat)
{
  return RectPoly (box->X1 - bloat, box->X2 + bloat,
                   box->Y1 - bloat, box->Y2 + bloat);
}

#warning FIXME Later
//static Boolean inhibit = False;

int
InitClip (DataTypePtr Data, LayerTypePtr layer, PolygonType * p)
{
  /* NOP */
  printf ("Someone called InitClip, bad someone.\n");
  return 0;
#warning FIXME Later
#if 0
  if (inhibit)
    return 0;
  if (p->Clipped)
    poly_Free (&p->Clipped);
  p->Clipped = original_poly (p);
  if (p->NoHoles)
    poly_Free (&p->NoHoles);
  p->NoHoles = NULL;
  if (!p->Clipped)
    return 0;
  assert (poly_Valid (p->Clipped));
  if (TEST_FLAG (CLEARPOLYFLAG, p))
    clearPoly (Data, layer, p, NULL, 0);
  else
    p->NoHolesValid = 0;
  return 1;
#endif
}

/* find polygon holes in range, then call the callback function for
 * each hole. If the callback returns non-zero, stop
 * the search.
 */
int
PolygonHoles (const BoxType * range, LayerTypePtr layer,
              PolygonTypePtr polygon, int (*any_call) (PLINE * contour,
                                                       LayerTypePtr lay,
                                                       PolygonTypePtr poly))
{
  POLYAREA *pa = polygon->Clipped;
  PLINE *pl;
  /* If this hole is so big the polygon doesn't exist, then it's not
   * really a hole.
   */
  if (!pa)
    return 0;
  for (pl = pa->contours->next; pl; pl = pl->next)
    {
      if (pl->xmin > range->X2 || pl->xmax < range->X1 ||
          pl->ymin > range->Y2 || pl->ymax < range->Y1)
        continue;
      if (any_call (pl, layer, polygon))
        {
          return 1;
        }
    }
  return 0;
}

struct plow_info
{
  int type;
  void *ptr1, *ptr2;
  LayerTypePtr layer;
  DataTypePtr data;
  int (*callback) (DataTypePtr, LayerTypePtr, PolygonTypePtr, int, void *,
                   void *);
};

static int
plow_callback_2 (const BoxType * b, void *cl)
{
  struct plow_info *plow = (struct plow_info *) cl;
  PolygonTypePtr polygon = (PolygonTypePtr) b;

  if (TEST_FLAG (CLEARPOLYFLAG, polygon))
    return plow->callback (plow->data, plow->layer, polygon, plow->type,
                           plow->ptr1, plow->ptr2);
  return 0;
}

static int
plow_callback (const BoxType * b, void *cl)
{
  struct plow_info *plow = (struct plow_info *) cl;
  PourTypePtr pour = (PourTypePtr) b;
  BoxType *sb = &((PinTypePtr) plow->ptr2)->BoundingBox;

  return r_search (pour->polygon_tree, sb, NULL, plow_callback_2, plow);
}

int
PlowsPolygon (DataType * Data, int type, void *ptr1, void *ptr2,
              int (*call_back) (DataTypePtr data, LayerTypePtr lay,
                                PolygonTypePtr poly, int type, void *ptr1,
                                void *ptr2))
{
  BoxType sb = ((PinTypePtr) ptr2)->BoundingBox;
  int r = 0;
  struct plow_info info;

  info.type = type;
  info.ptr1 = ptr1;
  info.ptr2 = ptr2;
  info.data = Data;
  info.callback = call_back;
  switch (type)
    {
    case PIN_TYPE:
    case VIA_TYPE:
      if (type == PIN_TYPE || ptr1 == ptr2 || ptr1 == NULL)
        {
          LAYER_LOOP (Data, max_layer);
          {
            info.layer = layer;
            r += r_search (layer->pour_tree, &sb, NULL, plow_callback, &info);
          }
          END_LOOP;
        }
      else
        {
          GROUP_LOOP (Data, GetLayerGroupNumberByNumber (GetLayerNumber (Data,
                                                                         ((LayerTypePtr) ptr1))));
          {
            info.layer = layer;
            r += r_search (layer->pour_tree, &sb, NULL, plow_callback, &info);
          }
          END_LOOP;
        }
      break;
    case LINE_TYPE:
    case ARC_TYPE:
    case TEXT_TYPE:
    case POLYGON_TYPE:
      /* the cast works equally well for lines and arcs */
      if (!TEST_FLAG (CLEARLINEFLAG, (LineTypePtr) ptr2))
        return 0;
      /* silk doesn't plow */
      if (GetLayerNumber (Data, ptr1) >= max_layer)
        return 0;
      GROUP_LOOP (Data, GetLayerGroupNumberByNumber (GetLayerNumber (Data,
                                                                     ((LayerTypePtr) ptr1))));
      {
        info.layer = layer;
        r += r_search (layer->pour_tree, &sb, NULL, plow_callback, &info);
      }
      END_LOOP;
      break;
    case PAD_TYPE:
      {
        Cardinal group = TEST_FLAG (ONSOLDERFLAG,
                                    (PadType *) ptr2) ? SOLDER_LAYER :
          COMPONENT_LAYER;
        group = GetLayerGroupNumberByNumber (max_layer + group);
        GROUP_LOOP (Data, group);
        {
          info.layer = layer;
          r +=
            r_search (layer->pour_tree, &sb, NULL, plow_callback, &info);
        }
        END_LOOP;
      }
      break;

    case ELEMENT_TYPE:
      {
        PIN_LOOP ((ElementType *) ptr1);
        {
          PlowsPolygon (Data, PIN_TYPE, ptr1, pin, call_back);
        }
        END_LOOP;
        PAD_LOOP ((ElementType *) ptr1);
        {
          PlowsPolygon (Data, PAD_TYPE, ptr1, pad, call_back);
        }
        END_LOOP;
      }
      break;
    }
  return r;
}


Boolean
isects (POLYAREA * a, PolygonTypePtr p, Boolean fr)
{
  POLYAREA *x;
  Boolean ans;
  ans = Touching (a, p->Clipped);
  /* argument may be register, so we must copy it */
  x = a;
  if (fr)
    poly_Free (&x);
  return ans;
}


Boolean
IsPointInPolygon (LocationType X, LocationType Y, BDimension r,
                  PolygonTypePtr p)
{
  POLYAREA *c;
  Vector v;
  v[0] = X;
  v[1] = Y;
  if (poly_CheckInside (p->Clipped, v))
    return True;
  if (r < 1)
    return False;
  if (!(c = CirclePoly (X, Y, r)))
    return False;
  return isects (c, p, True);
}


Boolean
IsPointInPolygonIgnoreHoles (LocationType X, LocationType Y, PolygonTypePtr p)
{
  Vector v;
  v[0] = X;
  v[1] = Y;
  return poly_InsideContour (p->Clipped->contours, v);
}

Boolean
IsRectangleInPolygon (LocationType X1, LocationType Y1, LocationType X2,
                      LocationType Y2, PolygonTypePtr p)
{
  POLYAREA *s;
  if (!
      (s = RectPoly (min (X1, X2), max (X1, X2), min (Y1, Y2), max (Y1, Y2))))
    return False;
  return isects (s, p, True);
}

static void
r_NoHolesPolygonDicer (POLYAREA * pa, void (*emit) (PolygonTypePtr, void *), void *user_data)
{
  PLINE *p = pa->contours;
#if 0
  POLYAREA *pa;

  pa = (POLYAREA *) malloc (sizeof (*pa));
#endif
#warning DO WE NEED TO SAVE THIS POINTER?
  pa->b = pa->f = pa;
#if 0
  pa->contours = inp->contours;
  pa->contour_tree = r_create_tree (NULL, 0, 0);
#endif


  if (!pa->contours->next)                 /* no holes */
    {
      PolygonType poly;
//      PointType pts[4];

      poly.BoundingBox.X1 = p->xmin;
      poly.BoundingBox.X2 = p->xmax;
      poly.BoundingBox.Y1 = p->ymin;
      poly.BoundingBox.Y2 = p->ymax;
      poly.Clipped = pa;
      poly.NoHoles = NULL;
#warning FIXME Later
#if 0
      poly.PointN = poly.PointMax = 4;
      poly.Points = pts;
      pts[0].X = pts[0].X2 = p->xmin;
      pts[0].Y = pts[0].Y2 = p->ymin;
      pts[1].X = pts[1].X2 = p->xmax;
      pts[1].Y = pts[1].Y2 = p->ymin;
      pts[2].X = pts[2].X2 = p->xmax;
      pts[2].Y = pts[2].Y2 = p->ymax;
      pts[3].X = pts[3].X2 = p->xmin;
      pts[3].Y = pts[3].Y2 = p->ymax;
#endif
      poly.Flags = MakeFlags (CLEARPOLYFLAG);
      emit (&poly, user_data);
      poly_Free (&pa);
      return;
    }
  else
    {
      POLYAREA *poly2, *left, *right;

      /* make a rectangle of the left region slicing through the middle of the first hole */
      poly2 =
        RectPoly (p->xmin, (p->next->xmin + p->next->xmax) / 2, p->ymin,
                  p->ymax);
      poly_AndSubtract_free (pa, poly2, &left, &right);
      if (left)
        {
          POLYAREA *cur, *next;
          cur = left;
          do
            {
              next = cur->f;
//              PLINE *pl = x->contours;
              r_NoHolesPolygonDicer (cur, emit, user_data);
//              y = x->f;
              /* the pline was already freed by its use int he recursive dicer */
//              free (x);
            }
          while ((cur = next) != left);
//          while ((x = y) != left);
        }
      if (right)
        {
          POLYAREA *cur, *next;
          cur = right;
          do
            {
              next = cur->f;
//              PLINE *pl = x->contours;
              r_NoHolesPolygonDicer (cur, emit, user_data);
//              y = x->f;
//              free (x);
            }
          while ((cur = next) != right);
        }
    }
}

void
NoHolesPolygonDicer (PolygonTypePtr p, void (*emit) (PolygonTypePtr, void *),
                     void *user_data, const BoxType * clip)
{
  POLYAREA *save, *ans;

  ans = save = poly_Create ();
  /* copy the main poly only */
  poly_Copy1 (save, p->Clipped);
  /* clip to the bounding box */
  if (clip)
    {
      POLYAREA *cbox = RectPoly (clip->X1, clip->X2, clip->Y1, clip->Y2);
      if (cbox)
        {
          int r = poly_Boolean_free (save, cbox, &ans, PBO_ISECT);
          save = ans;
          if (r != err_ok)
            save = NULL;
        }
    }
  if (!save)
    return;
  /* now dice it up */
  do
    {
      POLYAREA *next;
      next = save->f;

      r_NoHolesPolygonDicer (save, emit, user_data);
      /* go to next poly (could be one because of clip) */

      save = next;
      /* free the previouse POLYAREA. Note the contour was consumed in the dicer */
//      free (prev);
    }
  while (save != ans);
}

/* make a polygon split into multiple parts into multiple polygons */
Boolean
MorphPolygon (LayerTypePtr layer, PolygonTypePtr poly)
{
  return 0;
#warning FIXME Later
#if 0
  POLYAREA *p, *start;
  Boolean many = False;
  FlagType flags;

  if (!poly->Clipped || TEST_FLAG (LOCKFLAG, poly))
    return False;
  if (poly->Clipped->f == poly->Clipped)
    return False;
  ErasePolygon (poly);
  start = p = poly->Clipped;
  /* This is ugly. The creation of the new polygons can cause
   * all of the polygon pointers (including the one we're called
   * with to change if there is a realloc in GetPolygonMemory().
   * That will invalidate our original "poly" argument, potentially
   * inside the loop. We need to steal the Clipped pointer and
   * hide it from the Remove call so that it still exists while
   * we do this dirty work.
   */
  poly->Clipped = NULL;
  if (poly->NoHoles) printf ("Just leaked in MorpyPolygon\n");
  poly->NoHoles = NULL;
  flags = poly->Flags;
  RemovePolygon (layer, poly);
  inhibit = True;
  do
    {
      VNODE *v;
      PolygonTypePtr new;

      if (p->contours->area > PCB->IsleArea)
        {
          new = CreateNewPolygon (layer, flags);
          if (!new)
            return False;
          many = True;
          v = &p->contours->head;
          CreateNewPointInPolygon (new, v->point[0], v->point[1]);
          for (v = v->next; v != &p->contours->head; v = v->next)
            CreateNewPointInPolygon (new, v->point[0], v->point[1]);
          new->BoundingBox.X1 = p->contours->xmin;
          new->BoundingBox.X2 = p->contours->xmax + 1;
          new->BoundingBox.Y1 = p->contours->ymin;
          new->BoundingBox.Y2 = p->contours->ymax + 1;
          AddObjectToCreateUndoList (POLYGON_TYPE, layer, new, new);
          new->Clipped = p;
          p = p->f;             /* go to next pline */
          new->Clipped->b = new->Clipped->f = new->Clipped;     /* unlink from others */
          r_insert_entry (layer->polygon_tree, (BoxType *) new, 0);
          DrawPolygon (layer, new, 0);
        }
      else
        {
          POLYAREA *t = p;

          p = p->f;
          poly_DelContour (&t->contours);
          free (t);
        }
    }
  while (p != start);
  inhibit = False;
  IncrementUndoSerialNumber ();
  return many;
#endif
}
