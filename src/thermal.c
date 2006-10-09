/* $Id: thermal.c,v 1.2 2006-10-09 00:35:25 danmc Exp $ */

/*
 *                            COPYRIGHT
 *
 *  PCB, interactive printed circuit board design
 *  Copyright (C) 1994,1995,1996,2004,2006 Thomas Nau
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

/* this file, thermal.c was written by and is
 * (C) Copyright 2006, harry eaton
 */

/* negative thermal finger polygons
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdarg.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <assert.h>
#include <setjmp.h>
#include <memory.h>
#include <ctype.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include "global.h"

#include "create.h"
#include "data.h"
#include "draw.h"
#include "error.h"
#include "misc.h"
#include "move.h"
#include "output.h"
#include "polygon.h"
#include "rtree.h"
#include "thermal.h"
#include "undo.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

RCSID ("$Id: thermal.c,v 1.2 2006-10-09 00:35:25 danmc Exp $");

struct cent
{
  LocationType x, y;
  BDimension s, c;
  char style;
  POLYAREA *p;
};

static POLYAREA *
diag_line (LocationType X, LocationType Y, BDimension l, BDimension w,
	   Boolean rt)
{
  PLINE *c;
  Vector v;
  BDimension x1, x2, y1, y2;

  if (rt)
    {
      x1 = (l - w) * M_SQRT1_2;
      x2 = (l + w) * M_SQRT1_2;
      y1 = x1;
      y2 = x2;
    }
  else
    {
      x2 = -(l - w) * M_SQRT1_2;
      x1 = -(l + w) * M_SQRT1_2;
      y1 = -x1;
      y2 = -x2;
    }

  v[0] = X + x1;
  v[1] = Y + y2;
  if ((c = poly_NewContour (v)) == NULL)
    return NULL;
  v[0] = X - x2;
  v[1] = Y - y1;
  poly_InclVertex (c->head.prev, poly_CreateNode (v));
  v[0] = X - x1;
  v[1] = Y - y2;
  poly_InclVertex (c->head.prev, poly_CreateNode (v));
  v[0] = X + x2;
  v[1] = Y + y1;
  poly_InclVertex (c->head.prev, poly_CreateNode (v));
  return ContourToPoly (c);
}

static POLYAREA *
square_therm (PinTypePtr pin, Cardinal style)
{
  POLYAREA *p, *p2;
  PLINE *c;
  Vector v;
  BDimension d, in, out;

  switch (style)
    {
    case 1:
      d = PCB->ThermScale * pin->Clearance * M_SQRT1_2;
      out = (pin->Thickness + pin->Clearance) / 2;
      in = pin->Thickness / 2;
      /* top (actually bottom since +y is down) */
      v[0] = pin->X - in + d;
      v[1] = pin->Y + in;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[0] = pin->X + in - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + out - d;
      v[1] = pin->Y + out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - out + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p = ContourToPoly (c);
      /* right */
      v[0] = pin->X + in;
      v[1] = pin->Y + in - d;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[1] = pin->Y - in + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + out;
      v[1] = pin->Y - out + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y + out - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f = p2;
      p2->b = p;
      /* left */
      v[0] = pin->X - in;
      v[1] = pin->Y - in + d;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[1] = pin->Y + in - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - out;
      v[1] = pin->Y + out - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y - out + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f->f = p2;
      p2->b = p->f;
      /* bottom (actually top since +y is down) */
      v[0] = pin->X + in - d;
      v[1] = pin->Y - in;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[0] = pin->X - in + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - out + d;
      v[1] = pin->Y - out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + out - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f->f->f = p2;
      p2->f = p;
      p2->b = p->f->f;
      p->b = p2;
      return p;
    case 4:
      {
	LineType l;
	d = pin->Thickness / 2 - PCB->ThermScale * pin->Clearance;
	out = pin->Thickness / 2 + pin->Clearance / 4;
	in = pin->Clearance / 2;
	/* top */
	l.Point1.X = pin->X - d;
	l.Point2.Y = l.Point1.Y = pin->Y + out;
	l.Point2.X = pin->X + d;
	p = LinePoly (&l, in);
	/* right */
	l.Point1.X = l.Point2.X = pin->X + out;
	l.Point1.Y = pin->Y - d;
	l.Point2.Y = pin->Y + d;
	p2 = LinePoly (&l, in);
	p->f = p2;
	p2->b = p;
	/* bottom */
	l.Point1.X = pin->X - d;
	l.Point2.Y = l.Point1.Y = pin->Y - out;
	l.Point2.X = pin->X + d;
	p2 = LinePoly (&l, in);
	p->f->f = p2;
	p2->b = p->f;
	/* left */
	l.Point1.X = l.Point2.X = pin->X - out;
	l.Point1.Y = pin->Y - d;
	l.Point2.Y = pin->Y + d;
	p2 = LinePoly (&l, in);
	p->f->f->f = p2;
	p2->b = p->f->f;
	p->b = p2;
	p2->f = p;
	return p;
      }
    case 5:
      {
	POLYAREA *m;
	LineType l;
	in = pin->Clearance / 2;
	d = PCB->ThermScale * pin->Clearance;
	out = pin->Thickness / 2 + in / 2;
	/* top right */
	l.Point1.Y = l.Point2.Y = pin->Y + out;
	l.Point1.X = pin->X + d;
	l.Point2.X = pin->X + out;
	p = LinePoly (&l, in);
	/* right upper */
	l.Point1.Y = pin->Y + d;
	l.Point1.X = l.Point2.X;
	p2 = LinePoly (&l, in);
	poly_Boolean_free (p, p2, &m, PBO_UNITE);
	/* right lower */
	l.Point1.Y = pin->Y - d;
	l.Point2.Y = pin->Y - out;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, m, &p2, PBO_UNITE);
	/* bottom right */
	l.Point1.Y = l.Point2.Y;
	l.Point1.X = pin->X + d;
	l.Point2.X = pin->X + out;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, p2, &m, PBO_UNITE);
	/* bottom left */
	l.Point1.X = pin->X - d;
	l.Point2.X = pin->X - out;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, m, &p2, PBO_UNITE);
	/* left lower */
	l.Point1.Y = pin->Y - d;
	l.Point1.X = l.Point2.X;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, p2, &m, PBO_UNITE);
	/* left upper */
	l.Point1.Y = pin->Y + d;
	l.Point2.Y = pin->Y + out;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, m, &p2, PBO_UNITE);
	/* top left */
	l.Point1.Y = l.Point2.Y;
	l.Point1.X = pin->X - d;
	p = LinePoly (&l, in);
	poly_Boolean_free (p, p2, &m, PBO_UNITE);
	return m;
      }
    default:
      d = 0.5 * PCB->ThermScale * pin->Clearance;
      out = (pin->Thickness + pin->Clearance) / 2;
      in = pin->Thickness / 2;
      /* topright */
      v[0] = pin->X + d;
      v[1] = pin->Y + in;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[0] = pin->X + in;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y + out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p = ContourToPoly (c);
      /* bottom right */
      v[0] = pin->X + in;
      v[1] = pin->Y - d;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[1] = pin->Y - in;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y - out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X + out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f = p2;
      p2->b = p;
      /* bottom left */
      v[0] = pin->X - d;
      v[1] = pin->Y - in;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[0] = pin->X - in;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y - out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f->f = p2;
      p2->b = p->f;
      /* top left */
      v[0] = pin->X - d;
      v[1] = pin->Y + out;
      if ((c = poly_NewContour (v)) == NULL)
	return NULL;
      v[0] = pin->X - out;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y + d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - in;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[1] = pin->Y + in;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      v[0] = pin->X - d;
      poly_InclVertex (c->head.prev, poly_CreateNode (v));
      p2 = ContourToPoly (c);
      p->f->f->f = p2;
      p2->f = p;
      p2->b = p->f->f;
      p->b = p2;
      return p;
    }
}

static POLYAREA *
oct_therm (PinTypePtr pin, Cardinal style)
{
  POLYAREA *p, *p2, *m;
  BDimension t = 0.5 * PCB->ThermScale * pin->Clearance;
  BDimension w = pin->Thickness + pin->Clearance;

  p = OctagonPoly (pin->X, pin->Y, w);
  p2 = OctagonPoly (pin->X, pin->Y, pin->Thickness);
  /* make full clearance ring */
  poly_Boolean_free (p, p2, &m, PBO_SUB);
  switch (style)
    {
    default:
    case 1:
      p = diag_line (pin->X, pin->Y, w, t, True);
      poly_Boolean_free (m, p, &p2, PBO_SUB);
      p = diag_line (pin->X, pin->Y, w, t, False);
      poly_Boolean_free (p2, p, &m, PBO_SUB);
      return m;
    case 2:
      p = RectPoly (pin->X - t, pin->X + t, pin->Y - w, pin->Y + w);
      poly_Boolean_free (m, p, &p2, PBO_SUB);
      p = RectPoly (pin->X - w, pin->X + w, pin->Y - t, pin->Y + t);
      poly_Boolean_free (p2, p, &m, PBO_SUB);
      return m;
      /* fix me add thermal style 4 */
    case 5:
      {
	BDimension t = pin->Thickness / 2;
	POLYAREA *q;
	/* cheat by using the square therm's rounded parts */
	p = square_therm (pin, style);
	q = RectPoly (pin->X - t, pin->X + t, pin->Y - t, pin->Y + t);
	poly_Boolean_free (p, q, &p2, PBO_UNITE);
	poly_Boolean_free (m, p2, &p, PBO_ISECT);
	return p;
      }
    }
}

/* ThermPoly returns a POLYAREA having all of the clearance that when
 * subtracted from the plane create the desired thermal fingers.
 * Usually this is 4 disjoint regions.
 *
 * since calculating the POLYAREA can be expensive, the most recent several
 * are saved in a small cache
 */
POLYAREA *
ThermPoly (PinTypePtr pin, Cardinal laynum)
{
  ArcType a;
  POLYAREA *pa, *arc;
  Cardinal style = GET_THERM (laynum, pin);

  if (style == 3)
    return NULL;		/* solid connection no clearance */
  if (TEST_FLAG (SQUAREFLAG, pin))
    return square_therm (pin, style);
  if (TEST_FLAG (OCTAGONFLAG, pin))
    return oct_therm (pin, style);
  /* must be circular */
  switch (style)
    {
    case 1:
    case 2:
      {
	POLYAREA *m;
	BDimension t = (pin->Thickness + pin->Clearance) / 2;
	BDimension w = 0.5 * PCB->ThermScale * pin->Clearance;
	pa = CirclePoly (pin->X, pin->Y, t);
	arc = CirclePoly (pin->X, pin->Y, pin->Thickness / 2);
	/* create a thin ring */
	poly_Boolean_free (pa, arc, &m, PBO_SUB);
	/* fix me needs error checking */
	if (style == 2)
	  {
	    pa = RectPoly (pin->X - t, pin->X + t, pin->Y - w, pin->Y + w);
	    poly_Boolean_free (m, pa, &arc, PBO_SUB);
	    pa = RectPoly (pin->X - w, pin->X + w, pin->Y - t, pin->Y + t);
	  }
	else
	  {
	    pa = diag_line (pin->X, pin->Y, t, w, True);
	    poly_Boolean_free (m, pa, &arc, PBO_SUB);
	    pa = diag_line (pin->X, pin->Y, t, w, False);
	  }
	poly_Boolean_free (arc, pa, &m, PBO_SUB);
	return m;
      }


    default:
      a.X = pin->X;
      a.Y = pin->Y;
      a.Height = a.Width = pin->Thickness / 2 + pin->Clearance / 4;
      a.Thickness = 1;
      a.Clearance = pin->Clearance / 2;
      a.Flags = NoFlags ();
      a.Delta =
	90 -
	(a.Clearance * (1. + 2. * PCB->ThermScale) * 180) / (M_PI * a.Width);
      a.StartAngle = 90 - a.Delta / 2 + (style == 4 ? 0 : 45);
      pa = ArcPoly (&a, a.Clearance);
      if (!pa)
	return NULL;
      a.StartAngle += 90;
      arc = ArcPoly (&a, a.Clearance);
      if (!arc)
	return NULL;
      pa->f = arc;
      arc->b = pa;
      a.StartAngle += 90;
      arc = ArcPoly (&a, a.Clearance);
      if (!arc)
	return NULL;
      pa->f->f = arc;
      arc->b = pa->f;
      a.StartAngle += 90;
      arc = ArcPoly (&a, a.Clearance);
      if (!arc)
	return NULL;
      pa->b = arc;
      pa->f->f->f = arc;
      arc->b = pa->f->f;
      arc->f = pa;
      pa->b = arc;
      return pa;
    }
}
