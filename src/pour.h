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
 *  RCS: $Id$
 */

/* prototypes for pour editing routines
 */

#ifndef	__POUR_INCLUDED__
#define	__POUR_INCLUDED__

#include "global.h"

Cardinal pour_point_idx (PourTypePtr, PointTypePtr);
Cardinal pour_point_contour (PourTypePtr, Cardinal);
Cardinal prev_contour_point (PourTypePtr, Cardinal);
Cardinal next_contour_point (PourTypePtr, Cardinal);
Cardinal GetLowestDistancePourPoint (PourTypePtr,
					LocationType, LocationType);
bool RemoveExcessPourPoints (LayerTypePtr, PourTypePtr);
void GoToPreviousPourPoint (void);
void ClosePour (void);
void CopyAttachedPourToLayer (void);

int InitPourClip(DataType *d, LayerType *l, PourType *p);
void RestoreToPours(DataType *, int, void *, void *);
void ClearFromPours(DataType *, int, void *, void *);

POLYAREA * PourToPoly (PourType *);
void PolyToPoursOnLayer (DataType *, LayerType *, POLYAREA *, FlagType);

#endif /* __POUR_INCLUDED__ */
