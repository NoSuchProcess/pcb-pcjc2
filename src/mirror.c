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

static char *rcsid = "$Id: mirror.c,v 1.3 2003-12-30 02:18:51 haceaton Exp $";

/* functions used to change the mirror flag of an object
 *
 * an undo operation is not implemented because it's easy to
 * recover an object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "global.h"

#include "data.h"
#include "draw.h"
#include "mirror.h"
#include "misc.h"
#include "search.h"
#include "select.h"
#include "set.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

/* ---------------------------------------------------------------------------
 * mirrors the coordinates of an element
 * an additional offset is passed
 */
void
MirrorElementCoordinates (ElementTypePtr Element, Position yoff)
{
  ELEMENTLINE_LOOP (Element, 
    {
      line->Point1.X = SWAP_X (line->Point1.X);
      line->Point1.Y = SWAP_Y (line->Point1.Y) + yoff;
      line->Point2.X = SWAP_X (line->Point2.X);
      line->Point2.Y = SWAP_Y (line->Point2.Y) + yoff;
    }
  );
  PIN_LOOP (Element, 
    {
      pin->X = SWAP_X (pin->X);
      pin->Y = SWAP_Y (pin->Y) + yoff;
    }
  );
  PAD_LOOP (Element, 
    {
      pad->Point1.X = SWAP_X (pad->Point1.X);
      pad->Point1.Y = SWAP_Y (pad->Point1.Y) + yoff;
      pad->Point2.X = SWAP_X (pad->Point2.X);
      pad->Point2.Y = SWAP_Y (pad->Point2.Y) + yoff;
      TOGGLE_FLAG (ONSOLDERFLAG, pad);
    }
  );
  ARC_LOOP (Element, 
    {
      arc->X = SWAP_X (arc->X);
      arc->Y = SWAP_Y (arc->Y) + yoff;
      arc->StartAngle = SWAP_ANGLE (arc->StartAngle);
      arc->Delta = SWAP_DELTA (arc->Delta);
    }
  );
  ELEMENTTEXT_LOOP (Element, 
    {
      text->X = SWAP_X (text->X);
      text->Y = SWAP_Y (text->Y) + yoff;
      TOGGLE_FLAG (ONSOLDERFLAG, text);
    }
  );
  Element->MarkX = SWAP_X (Element->MarkX);
  Element->MarkY = SWAP_Y (Element->MarkY) + yoff;

  /* now toggle the solder-side flag */
  TOGGLE_FLAG (ONSOLDERFLAG, Element);
  SetElementBoundingBox (Element, &PCB->Font);
}
