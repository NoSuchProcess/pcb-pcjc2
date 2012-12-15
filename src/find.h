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

/* prototypes connection search routines
 */

#ifndef	PCB_FIND_H
#define	PCB_FIND_H

#include <stdio.h>		/* needed to define 'FILE *' */
#include "global.h"

/* ---------------------------------------------------------------------------
 * some local defines
 */
#define LOOKUP_FIRST	\
	(PIN_TYPE | PAD_TYPE)
#define LOOKUP_MORE	\
	(VIA_TYPE | LINE_TYPE | RATLINE_TYPE | POLYGON_TYPE | ARC_TYPE)
#define SILK_TYPE	\
	(LINE_TYPE | ARC_TYPE | POLYGON_TYPE)

bool LineLineIntersect (LineType *, LineType *);
bool LineArcIntersect (LineType *, ArcType *);
bool PinLineIntersect (PinType *, LineType *);
bool LinePadIntersect (LineType *, PadType *);
/* NOT USED EXTERNALLY */// bool ArcPadIntersect (ArcType *, PadType *);
void LookupElementConnections (ElementType *, FILE *);
void LookupConnectionsToAllElements (FILE *);
void LookupConnection (Coord, Coord, bool, Coord, int, bool AndRats);
void LookupUnusedPins (FILE *);
bool ResetFoundLinesAndPolygons (bool, int flag);
bool ResetFoundPinsViasAndPads (bool, int flag);
bool ResetConnections (bool, int flag);
void InitConnectionLookup (void);
/* NOT USED EXTERNALLY */// void InitComponentLookup (void);
/* NOT USED EXTERNALLY */// void InitLayoutLookup (void);
void FreeConnectionLookupMemory (void);
/* NOT USED EXTERNALLY */// void FreeComponentLookupMemory (void);
/* NOT USED EXTERNALLY */// void FreeLayoutLookupMemory (void);
void RatFindHook (int, void *, void *, void *, bool, bool);
void SaveFindFlag (int);
void RestoreFindFlag (void);
int DRCAll (void);
/* NOT USED EXTERNALLY */// bool IsArcInPolygon (ArcType *, PolygonType *);
/* NOT USED EXTERNALLY */// bool IsLineInPolygon (LineType *, PolygonType *);
/* NOT USED EXTERNALLY */// bool IsPadInPolygon (PadType *, PolygonType *);
/* NOT USED EXTERNALLY */// bool IsPolygonInPolygon (PolygonType *, PolygonType *);

#endif
