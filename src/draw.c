/*
 *                            COPYRIGHT
 *
 *  PCB, interactive printed circuit board design
 *  Copyright (C) 1994,1995,1996, 2003, 2004 Thomas Nau
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


/* drawing routines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "global.h"
#include "hid_draw.h"

/*#include "clip.h"*/
#include "compat.h"
#include "crosshair.h"
#include "data.h"
#include "draw.h"
#include "error.h"
#include "mymem.h"
#include "misc.h"
#include "rotate.h"
#include "rtree.h"
#include "search.h"
#include "select.h"
#include "print.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

#undef NDEBUG
#include <assert.h>

#ifndef MAXINT
#define MAXINT (((unsigned int)(~0))>>1)
#endif

#define	SMALL_SMALL_TEXT_SIZE	0
#define	SMALL_TEXT_SIZE			1
#define	NORMAL_TEXT_SIZE		2
#define	LARGE_TEXT_SIZE			3
#define	N_TEXT_SIZES			4


/* ---------------------------------------------------------------------------
 * some local identifiers
 */
static BoxType Block = {MAXINT, MAXINT, -MAXINT, -MAXINT};

static int doing_pinout = 0;
static bool doing_assy = false;

/* ---------------------------------------------------------------------------
 * some local prototypes
 */
static void DrawEverything (const BoxType *);
static void DrawPPV (int group, const BoxType *);
static void AddPart (void *);
static void DrawEMark (ElementType *, Coord, Coord, bool);
static void DrawRats (const BoxType *);

static void
set_object_color (AnyObjectType *obj,
                  char *warn_color, char *selected_color,
                  char *found_color, char *normal_color)
{
  char *color;

  if      (warn_color     != NULL && TEST_FLAG (WARNFLAG,     obj)) color = warn_color;
  else if (selected_color != NULL && TEST_FLAG (SELECTEDFLAG, obj)) color = selected_color;
  else if (found_color    != NULL && TEST_FLAG (FOUNDFLAG,    obj)) color = found_color;
  else                                                              color = normal_color;

  gui->graphics->set_color (Output.fgGC, color);
}

static void
set_layer_object_color (LayerType *layer, AnyObjectType *obj)
{
  set_object_color (obj, NULL, layer->SelectedColor, PCB->ConnectedColor, layer->Color);
}

/*---------------------------------------------------------------------------
 *  Adds the update rect to the update region
 */
static void
AddPart (void *b)
{
  BoxType *box = (BoxType *) b;

  Block.X1 = MIN (Block.X1, box->X1);
  Block.X2 = MAX (Block.X2, box->X2);
  Block.Y1 = MIN (Block.Y1, box->Y1);
  Block.Y2 = MAX (Block.Y2, box->Y2);
}

/*
 * initiate the actual redrawing of the updated area
 */
void
Draw (void)
{
  if (Block.X1 <= Block.X2 && Block.Y1 <= Block.Y2)
    gui->invalidate_lr (Block.X1, Block.X2, Block.Y1, Block.Y2);

  /* shrink the update block */
  Block.X1 = Block.Y1 =  MAXINT;
  Block.X2 = Block.Y2 = -MAXINT;
}

/* ---------------------------------------------------------------------- 
 * redraws all the data by the event handlers
 */
void
Redraw (void)
{
  gui->invalidate_all ();
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

  gui->graphics->set_color (Output.fgGC, PCB->PinNameColor);

  text.Flags = NoFlags ();
  /* Set font height to approx 56% of pin thickness */
  text.Scale = 56 * pv->Thickness / FONT_CAPHEIGHT;
  text.X = box.X1;
  text.Y = box.Y1;
  text.Direction = vert ? 1 : 0;

  if (gui->gui)
    doing_pinout++;
  gui->graphics->draw_pcb_text (Output.fgGC, &text, 0);
  if (gui->gui)
    doing_pinout--;
}

static void
_draw_pv (PinType *pv, bool draw_hole)
{
  if (TEST_FLAG (THINDRAWFLAG, PCB))
    gui->graphics->thindraw_pcb_pv (Output.fgGC, Output.fgGC, pv, draw_hole, false);
  else
    gui->graphics->fill_pcb_pv (Output.fgGC, Output.bgGC, pv, draw_hole, false);

  if (!TEST_FLAG (HOLEFLAG, pv) && TEST_FLAG (DISPLAYNAMEFLAG, pv))
    _draw_pv_name (pv);
}

static void
draw_pin (PinType *pin, bool draw_hole)
{
  if (doing_pinout)
    gui->graphics->set_color (Output.fgGC, PCB->PinColor);
  else
    set_object_color ((AnyObjectType *)pin,
                      PCB->WarnColor, PCB->PinSelectedColor,
                      PCB->ConnectedColor, PCB->PinColor);

  _draw_pv (pin, draw_hole);
}

static int
pin_callback (const BoxType * b, void *cl)
{
  draw_pin ((PinType *)b, false);
  return 1;
}

static void
draw_via (PinType *via, bool draw_hole)
{
  if (doing_pinout)
    gui->graphics->set_color (Output.fgGC, PCB->ViaColor);
  else
    set_object_color ((AnyObjectType *)via,
                      PCB->WarnColor, PCB->ViaSelectedColor,
                      PCB->ConnectedColor, PCB->ViaColor);

  _draw_pv (via, draw_hole);
}

static int
via_callback (const BoxType * b, void *cl)
{
  draw_via ((PinType *)b, false);
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

  gui->graphics->set_color (Output.fgGC, PCB->PinNameColor);

  text.Flags = NoFlags ();
  /* Set font height to approx 90% of pin thickness */
  text.Scale = 90 * pad->Thickness / FONT_CAPHEIGHT;
  text.X = box.X1;
  text.Y = box.Y1;
  text.Direction = vert ? 1 : 0;

  gui->graphics->draw_pcb_text (Output.fgGC, &text, 0);
}

static void
_draw_pad (hidGC gc, PadType *pad, bool clear, bool mask)
{
  if (clear && !mask && pad->Clearance <= 0)
    return;

  if (TEST_FLAG (THINDRAWFLAG, PCB) ||
      (clear && TEST_FLAG (THINDRAWPOLYFLAG, PCB)))
    gui->graphics->thindraw_pcb_pad (gc, pad, clear, mask);
  else
    gui->graphics->fill_pcb_pad (gc, pad, clear, mask);
}

static void
draw_pad (PadType *pad)
{
  if (doing_pinout)
    gui->graphics->set_color (Output.fgGC, PCB->PinColor);
  else
    set_object_color ((AnyObjectType *)pad, PCB->WarnColor,
                      PCB->PinSelectedColor, PCB->ConnectedColor,
                      FRONT (pad) ? PCB->PinColor : PCB->InvisibleObjectsColor);

  _draw_pad (Output.fgGC, pad, false, false);

  if (doing_pinout || TEST_FLAG (DISPLAYNAMEFLAG, pad))
    draw_pad_name (pad);
}

static int
pad_callback (const BoxType * b, void *cl)
{
  PadType *pad = (PadType *) b;
  int *side = cl;

  if (ON_SIDE (pad, *side))
    draw_pad (pad);
  return 1;
}

static void
draw_element_name (ElementType *element)
{
  if ((TEST_FLAG (HIDENAMESFLAG, PCB) && gui->gui) ||
      TEST_FLAG (HIDENAMEFLAG, element))
    return;
  if (doing_pinout || doing_assy)
    gui->graphics->set_color (Output.fgGC, PCB->ElementColor);
  else if (TEST_FLAG (SELECTEDFLAG, &ELEMENT_TEXT (PCB, element)))
    gui->graphics->set_color (Output.fgGC, PCB->ElementSelectedColor);
  else if (FRONT (element))
    gui->graphics->set_color (Output.fgGC, PCB->ElementColor);
  else
    gui->graphics->set_color (Output.fgGC, PCB->InvisibleObjectsColor);
  gui->graphics->draw_pcb_text (Output.fgGC, &ELEMENT_TEXT (PCB, element), PCB->minSlk);
}

static int
name_callback (const BoxType * b, void *cl)
{
  TextType *text = (TextType *) b;
  ElementType *element = (ElementType *) text->Element;
  int *side = cl;

  if (TEST_FLAG (HIDENAMEFLAG, element))
    return 0;

  if (ON_SIDE (element, *side))
    draw_element_name (element);
  return 0;
}

static void
draw_element_pins_and_pads (ElementType *element)
{
  PAD_LOOP (element);
  {
    if (doing_pinout || doing_assy || FRONT (pad) || PCB->InvisibleObjectsOn)
      draw_pad (pad);
  }
  END_LOOP;
  PIN_LOOP (element);
  {
    draw_pin (pin, true);
  }
  END_LOOP;
}

static int
EMark_callback (const BoxType * b, void *cl)
{
  ElementType *element = (ElementType *) b;

  DrawEMark (element, element->MarkX, element->MarkY, !FRONT (element));
  return 1;
}

static int
hole_callback (const BoxType * b, void *cl)
{
  PinType *pv = (PinType *) b;
  int plated = cl ? *(int *) cl : -1;

  if ((plated == 0 && !TEST_FLAG (HOLEFLAG, pv)) ||
      (plated == 1 &&  TEST_FLAG (HOLEFLAG, pv)))
    return 1;

  if (TEST_FLAG (THINDRAWFLAG, PCB))
    {
      if (!TEST_FLAG (HOLEFLAG, pv))
        {
          gui->graphics->set_line_cap (Output.fgGC, Round_Cap);
          gui->graphics->set_line_width (Output.fgGC, 0);
          gui->graphics->draw_arc (Output.fgGC,
                                   pv->X, pv->Y, pv->DrillingHole / 2,
                                   pv->DrillingHole / 2, 0, 360);
        }
    }
  else
    gui->graphics->fill_circle (Output.bgGC, pv->X, pv->Y, pv->DrillingHole / 2);

  if (TEST_FLAG (HOLEFLAG, pv))
    {
      set_object_color ((AnyObjectType *) pv,
                        PCB->WarnColor, PCB->ViaSelectedColor,
                        NULL, Settings.BlackColor);

      gui->graphics->set_line_cap (Output.fgGC, Round_Cap);
      gui->graphics->set_line_width (Output.fgGC, 0);
      gui->graphics->draw_arc (Output.fgGC,
                               pv->X, pv->Y, pv->DrillingHole / 2,
                               pv->DrillingHole / 2, 0, 360);
    }
  return 1;
}

void
DrawHoles (bool draw_plated, bool draw_unplated, const BoxType *drawn_area)
{
  int plated = -1;

  if ( draw_plated && !draw_unplated) plated = 1;
  if (!draw_plated &&  draw_unplated) plated = 0;

  r_search (PCB->Data->pin_tree, drawn_area, NULL, hole_callback, &plated);
  r_search (PCB->Data->via_tree, drawn_area, NULL, hole_callback, &plated);
}

static int
line_callback (const BoxType * b, void *cl)
{
  LayerType *layer = (LayerType *) cl;
  LineType *line = (LineType *) b;

  set_layer_object_color (layer, (AnyObjectType *) line);
  gui->graphics->draw_pcb_line (Output.fgGC, line);

  return 1;
}

static int
rat_callback (const BoxType * b, void *cl)
{
  RatType *rat = (RatType *)b;

  set_object_color ((AnyObjectType *) rat, NULL, PCB->RatSelectedColor,
                    PCB->ConnectedColor, PCB->RatColor);

  if (Settings.RatThickness < 100)
    rat->Thickness = pixel_slop * Settings.RatThickness;
  /* rats.c set VIAFLAG if this rat goes to a containing poly: draw a donut */
  if (TEST_FLAG(VIAFLAG, rat))
    {
      int w = rat->Thickness;

      if (TEST_FLAG (THINDRAWFLAG, PCB))
        gui->graphics->set_line_width (Output.fgGC, 0);
      else
        gui->graphics->set_line_width (Output.fgGC, w);
      gui->graphics->draw_arc (Output.fgGC, rat->Point1.X, rat->Point1.Y,
                               w * 2, w * 2, 0, 360);
    }
  else
    gui->graphics->draw_pcb_line (Output.fgGC, (LineType *) rat);
  return 1;
}

static int
arc_callback (const BoxType * b, void *cl)
{
  LayerType *layer = (LayerType *) cl;
  ArcType *arc =  (ArcType *) b;

  set_layer_object_color (layer, (AnyObjectType *) arc);
  gui->graphics->draw_pcb_arc (Output.fgGC, arc);

  return 1;
}

static void
draw_element_package (ElementType *element)
{
  /* set color and draw lines, arcs, text and pins */
  if (doing_pinout || doing_assy)
    gui->graphics->set_color (Output.fgGC, PCB->ElementColor);
  else if (TEST_FLAG (SELECTEDFLAG, element))
    gui->graphics->set_color (Output.fgGC, PCB->ElementSelectedColor);
  else if (FRONT (element))
    gui->graphics->set_color (Output.fgGC, PCB->ElementColor);
  else
    gui->graphics->set_color (Output.fgGC, PCB->InvisibleObjectsColor);

  /* draw lines, arcs, text and pins */
  ELEMENTLINE_LOOP (element);
  {
    gui->graphics->draw_pcb_line (Output.fgGC, line);
  }
  END_LOOP;
  ARC_LOOP (element);
  {
    gui->graphics->draw_pcb_arc (Output.fgGC, arc);
  }
  END_LOOP;
}

static int
element_callback (const BoxType * b, void *cl)
{
  ElementType *element = (ElementType *) b;
  int *side = cl;

  if (ON_SIDE (element, *side))
    draw_element_package (element);
  return 1;
}

/* ---------------------------------------------------------------------------
 * prints assembly drawing.
 */

void
PrintAssembly (int side, const BoxType * drawn_area)
{
  int side_group = GetLayerGroupNumberByNumber (max_copper_layer + side);

  doing_assy = true;
  gui->graphics->set_draw_faded (Output.fgGC, 1);
  DrawLayerGroup (side_group, drawn_area);
  gui->graphics->set_draw_faded (Output.fgGC, 0);

  /* draw package */
  DrawSilk (side, drawn_area);
  doing_assy = false;
}

/* ---------------------------------------------------------------------------
 * initializes some identifiers for a new zoom factor and redraws whole screen
 */
static void
DrawEverything (const BoxType *drawn_area)
{
  int i, ngroups, side;
  int component, solder;
  /* This is the list of layer groups we will draw.  */
  int do_group[MAX_LAYER];
  /* This is the reverse of the order in which we draw them.  */
  int drawn_groups[MAX_LAYER];
  int plated, unplated;
  bool paste_empty;

  PCB->Data->SILKLAYER.Color = PCB->ElementColor;
  PCB->Data->BACKSILKLAYER.Color = PCB->InvisibleObjectsColor;

  memset (do_group, 0, sizeof (do_group));
  for (ngroups = 0, i = 0; i < max_copper_layer; i++)
    {
      LayerType *l = LAYER_ON_STACK (i);
      int group = GetLayerGroupNumberByNumber (LayerStack[i]);
      if (l->On && !do_group[group])
	{
	  do_group[group] = 1;
	  drawn_groups[ngroups++] = group;
	}
    }

  component = GetLayerGroupNumberByNumber (component_silk_layer);
  solder = GetLayerGroupNumberByNumber (solder_silk_layer);

  /*
   * first draw all 'invisible' stuff
   */
  if (!TEST_FLAG (CHECKPLANESFLAG, PCB)
      && gui->set_layer ("invisible", SL (INVISIBLE, 0), 0))
    {
      side = SWAP_IDENT ? COMPONENT_LAYER : SOLDER_LAYER;
      if (PCB->ElementOn)
	{
	  r_search (PCB->Data->element_tree, drawn_area, NULL, element_callback, &side);
	  r_search (PCB->Data->name_tree[NAME_INDEX (PCB)], drawn_area, NULL, name_callback, &side);
	  DrawLayer (&(PCB->Data->Layer[max_copper_layer + side]), drawn_area);
	}
      r_search (PCB->Data->pad_tree, drawn_area, NULL, pad_callback, &side);
      gui->end_layer ();
    }

  /* draw all layers in layerstack order */
  for (i = ngroups - 1; i >= 0; i--)
    {
      int group = drawn_groups[i];

      if (gui->set_layer (0, group, 0))
        {
          DrawLayerGroup (group, drawn_area);
          gui->end_layer ();
        }
    }

  if (TEST_FLAG (CHECKPLANESFLAG, PCB) && gui->gui)
    return;

  /* Draw pins, pads, vias below silk */
  if (gui->gui)
    DrawPPV (SWAP_IDENT ? solder : component, drawn_area);
  else
    {
      CountHoles (&plated, &unplated, drawn_area);

      if (plated && gui->set_layer ("plated-drill", SL (PDRILL, 0), 0))
        {
          DrawHoles (true, false, drawn_area);
          gui->end_layer ();
        }

      if (unplated && gui->set_layer ("unplated-drill", SL (UDRILL, 0), 0))
        {
          DrawHoles (false, true, drawn_area);
          gui->end_layer ();
        }
    }

  /* Draw the solder mask if turned on */
  if (gui->set_layer ("componentmask", SL (MASK, TOP), 0))
    {
      DrawMask (COMPONENT_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("soldermask", SL (MASK, BOTTOM), 0))
    {
      DrawMask (SOLDER_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("topsilk", SL (SILK, TOP), 0))
    {
      DrawSilk (COMPONENT_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("bottomsilk", SL (SILK, BOTTOM), 0))
    {
      DrawSilk (SOLDER_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->gui)
    {
      /* Draw element Marks */
      if (PCB->PinOn)
	r_search (PCB->Data->element_tree, drawn_area, NULL, EMark_callback,
		  NULL);
      /* Draw rat lines on top */
      if (gui->set_layer ("rats", SL (RATS, 0), 0))
        {
          DrawRats(drawn_area);
          gui->end_layer ();
        }
    }

  paste_empty = IsPasteEmpty (COMPONENT_LAYER);
  if (gui->set_layer ("toppaste", SL (PASTE, TOP), paste_empty))
    {
      DrawPaste (COMPONENT_LAYER, drawn_area);
      gui->end_layer ();
    }

  paste_empty = IsPasteEmpty (SOLDER_LAYER);
  if (gui->set_layer ("bottompaste", SL (PASTE, BOTTOM), paste_empty))
    {
      DrawPaste (SOLDER_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("topassembly", SL (ASSY, TOP), 0))
    {
      PrintAssembly (COMPONENT_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("bottomassembly", SL (ASSY, BOTTOM), 0))
    {
      PrintAssembly (SOLDER_LAYER, drawn_area);
      gui->end_layer ();
    }

  if (gui->set_layer ("fab", SL (FAB, 0), 0))
    {
      PrintFab (Output.fgGC);
      gui->end_layer ();
    }
}

static void
DrawEMark (ElementType *e, Coord X, Coord Y, bool invisible)
{
  Coord mark_size = EMARK_SIZE;
  if (!PCB->InvisibleObjectsOn && invisible)
    return;

  if (e->Pin != NULL)
    {
      PinType *pin0 = e->Pin->data;
      if (TEST_FLAG (HOLEFLAG, pin0))
	mark_size = MIN (mark_size, pin0->DrillingHole / 2);
      else
	mark_size = MIN (mark_size, pin0->Thickness / 2);
    }

  if (e->Pad != NULL)
    {
      PadType *pad0 = e->Pad->data;
      mark_size = MIN (mark_size, pad0->Thickness / 2);
    }

  gui->graphics->set_color (Output.fgGC,
		  invisible ? PCB->InvisibleMarkColor : PCB->ElementColor);
  gui->graphics->set_line_cap (Output.fgGC, Trace_Cap);
  gui->graphics->set_line_width (Output.fgGC, 0);
  gui->graphics->draw_line (Output.fgGC, X - mark_size, Y, X, Y - mark_size);
  gui->graphics->draw_line (Output.fgGC, X + mark_size, Y, X, Y - mark_size);
  gui->graphics->draw_line (Output.fgGC, X - mark_size, Y, X, Y + mark_size);
  gui->graphics->draw_line (Output.fgGC, X + mark_size, Y, X, Y + mark_size);

  /*
   * If an element is locked, place a "L" on top of the "diamond".
   * This provides a nice visual indication that it is locked that
   * works even for color blind users.
   */
  if (TEST_FLAG (LOCKFLAG, e) )
    {
      gui->graphics->draw_line (Output.fgGC, X, Y, X + 2 * mark_size, Y);
      gui->graphics->draw_line (Output.fgGC, X, Y, X, Y - 4* mark_size);
    }
}

/* ---------------------------------------------------------------------------
 * Draws pins pads and vias - Always draws for non-gui HIDs,
 * otherwise drawing depends on PCB->PinOn and PCB->ViaOn
 */
static void
DrawPPV (int group, const BoxType *drawn_area)
{
  int component_group = GetLayerGroupNumberByNumber (component_silk_layer);
  int solder_group = GetLayerGroupNumberByNumber (solder_silk_layer);
  int side;

  if (PCB->PinOn || !gui->gui)
    {
      /* draw element pins */
      r_search (PCB->Data->pin_tree, drawn_area, NULL, pin_callback, NULL);

      /* draw element pads */
      if (group == component_group)
        {
          side = COMPONENT_LAYER;
          r_search (PCB->Data->pad_tree, drawn_area, NULL, pad_callback, &side);
        }

      if (group == solder_group)
        {
          side = SOLDER_LAYER;
          r_search (PCB->Data->pad_tree, drawn_area, NULL, pad_callback, &side);
        }
    }

  /* draw vias */
  if (PCB->ViaOn || !gui->gui)
    {
      r_search (PCB->Data->via_tree, drawn_area, NULL, via_callback, NULL);
      r_search (PCB->Data->via_tree, drawn_area, NULL, hole_callback, NULL);
    }
  if (PCB->PinOn || doing_assy)
    r_search (PCB->Data->pin_tree, drawn_area, NULL, hole_callback, NULL);
}

static int
clearPin_callback (const BoxType * b, void *cl)
{
  PinType *pin = (PinType *) b;
  if (TEST_FLAG (THINDRAWFLAG, PCB) || TEST_FLAG (THINDRAWPOLYFLAG, PCB))
    gui->graphics->thindraw_pcb_pv (Output.pmGC, Output.pmGC, pin, false, true);
  else
    gui->graphics->fill_pcb_pv (Output.pmGC, Output.pmGC, pin, false, true);
  return 1;
}

struct poly_info {
  const BoxType *drawn_area;
  LayerType *layer;
};

static int
poly_callback (const BoxType * b, void *cl)
{
  struct poly_info *i = cl;
  PolygonType *polygon = (PolygonType *)b;

  set_layer_object_color (i->layer, (AnyObjectType *) polygon);

  gui->graphics->draw_pcb_polygon (Output.fgGC, polygon, i->drawn_area);

  return 1;
}

static int
clearPad_callback (const BoxType * b, void *cl)
{
  PadType *pad = (PadType *) b;
  int *side = cl;
  if (ON_SIDE (pad, *side) && pad->Mask)
    _draw_pad (Output.pmGC, pad, true, true);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Draws silk layer.
 */

void
DrawSilk (int side, const BoxType * drawn_area)
{
#if 0
  /* This code is used when you want to mask silk to avoid exposed
     pins and pads.  We decided it was a bad idea to do this
     unconditionally, but the code remains.  */
#endif

#if 0
  if (gui->poly_before)
    {
      gui->graphics->use_mask (HID_MASK_BEFORE);
#endif
      DrawLayer (LAYER_PTR (max_copper_layer + side), drawn_area);
      /* draw package */
      r_search (PCB->Data->element_tree, drawn_area, NULL, element_callback, &side);
      r_search (PCB->Data->name_tree[NAME_INDEX (PCB)], drawn_area, NULL, name_callback, &side);
#if 0
    }

  gui->graphics->use_mask (HID_MASK_CLEAR);
  r_search (PCB->Data->pin_tree, drawn_area, NULL, clearPin_callback, NULL);
  r_search (PCB->Data->via_tree, drawn_area, NULL, clearPin_callback, NULL);
  r_search (PCB->Data->pad_tree, drawn_area, NULL, clearPad_callback, &side);

  if (gui->poly_after)
    {
      gui->graphics->use_mask (HID_MASK_AFTER);
      DrawLayer (LAYER_PTR (max_copper_layer + layer), drawn_area);
      /* draw package */
      r_search (PCB->Data->element_tree, drawn_area, NULL, element_callback, &side);
      r_search (PCB->Data->name_tree[NAME_INDEX (PCB)], drawn_area, NULL, name_callback, &side);
    }
  gui->graphics->use_mask (HID_MASK_OFF);
#endif
}


static void
DrawMaskBoardArea (int mask_type, const BoxType *drawn_area)
{
  /* Skip the mask drawing if the GUI doesn't want this type */
  if ((mask_type == HID_MASK_BEFORE && !gui->poly_before) ||
      (mask_type == HID_MASK_AFTER  && !gui->poly_after))
    return;

  gui->graphics->use_mask (mask_type);
  gui->graphics->set_color (Output.fgGC, PCB->MaskColor);
  if (drawn_area == NULL)
    gui->graphics->fill_rect (Output.fgGC, 0, 0, PCB->MaxWidth, PCB->MaxHeight);
  else
    gui->graphics->fill_rect (Output.fgGC, drawn_area->X1, drawn_area->Y1,
                                           drawn_area->X2, drawn_area->Y2);
}

/* ---------------------------------------------------------------------------
 * draws solder mask layer - this will cover nearly everything
 */
void
DrawMask (int side, const BoxType *screen)
{
  int thin = TEST_FLAG(THINDRAWFLAG, PCB) || TEST_FLAG(THINDRAWPOLYFLAG, PCB);

  if (thin)
    gui->graphics->set_color (Output.pmGC, PCB->MaskColor);
  else
    {
      DrawMaskBoardArea (HID_MASK_BEFORE, screen);
      gui->graphics->use_mask (HID_MASK_CLEAR);
    }

  r_search (PCB->Data->pin_tree, screen, NULL, clearPin_callback, NULL);
  r_search (PCB->Data->via_tree, screen, NULL, clearPin_callback, NULL);
  r_search (PCB->Data->pad_tree, screen, NULL, clearPad_callback, &side);

  if (thin)
    gui->graphics->set_color (Output.pmGC, "erase");
  else
    {
      DrawMaskBoardArea (HID_MASK_AFTER, screen);
      gui->graphics->use_mask (HID_MASK_OFF);
    }
}

/* ---------------------------------------------------------------------------
 * draws solder paste layer for a given side of the board
 */
void
DrawPaste (int side, const BoxType *drawn_area)
{
  gui->graphics->set_color (Output.fgGC, PCB->ElementColor);
  ALLPAD_LOOP (PCB->Data);
  {
    if (ON_SIDE (pad, side) && !TEST_FLAG (NOPASTEFLAG, pad) && pad->Mask > 0)
      {
        if (pad->Mask < pad->Thickness)
          _draw_pad (Output.fgGC, pad, true, true);
        else
          _draw_pad (Output.fgGC, pad, false, false);
      }
  }
  ENDALL_LOOP;
}

static void
DrawRats (const BoxType *drawn_area)
{
  /*
   * XXX lesstif allows positive AND negative drawing in HID_MASK_CLEAR.
   * XXX gtk only allows negative drawing.
   * XXX using the mask here is to get rat transparency
   */
  int can_mask = strcmp(gui->name, "lesstif") == 0;

  if (can_mask)
    gui->graphics->use_mask (HID_MASK_CLEAR);
  r_search (PCB->Data->rat_tree, drawn_area, NULL, rat_callback, NULL);
  if (can_mask)
    gui->graphics->use_mask (HID_MASK_OFF);
}

static int
text_callback (const BoxType * b, void *cl)
{
  LayerType *layer = cl;
  TextType *text = (TextType *)b;
  int min_silk_line;

  if (TEST_FLAG (SELECTEDFLAG, text))
    gui->graphics->set_color (Output.fgGC, layer->SelectedColor);
  else
    gui->graphics->set_color (Output.fgGC, layer->Color);
  if (layer == &PCB->Data->SILKLAYER ||
      layer == &PCB->Data->BACKSILKLAYER)
    min_silk_line = PCB->minSlk;
  else
    min_silk_line = PCB->minWid;
  gui->graphics->draw_pcb_text (Output.fgGC, text, min_silk_line);
  return 1;
}

void
DrawLayer (LayerType *Layer, const BoxType *screen)
{
  struct poly_info info = {screen, Layer};

  /* print the non-clearing polys */
  r_search (Layer->polygon_tree, screen, NULL, poly_callback, &info);

  if (TEST_FLAG (CHECKPLANESFLAG, PCB))
    return;

  /* draw all visible lines this layer */
  r_search (Layer->line_tree, screen, NULL, line_callback, Layer);

  /* draw the layer arcs on screen */
  r_search (Layer->arc_tree, screen, NULL, arc_callback, Layer);

  /* draw the layer text on screen */
  r_search (Layer->text_tree, screen, NULL, text_callback, Layer);

  /* We should check for gui->gui here, but it's kinda cool seeing the
     auto-outline magically disappear when you first add something to
     the "outline" layer.  */
  if (IsLayerEmpty (Layer)
      && (strcmp (Layer->Name, "outline") == 0
	  || strcmp (Layer->Name, "route") == 0))
    {
      gui->graphics->set_color (Output.fgGC, Layer->Color);
      gui->graphics->set_line_width (Output.fgGC, PCB->minWid);
      gui->graphics->draw_rect (Output.fgGC,
                                0, 0,
                                PCB->MaxWidth, PCB->MaxHeight);
    }
}

/* ---------------------------------------------------------------------------
 * draws one layer group.  If the exporter is not a GUI,
 * also draws the pins / pads / vias in this layer group.
 */
void
DrawLayerGroup (int group, const BoxType *drawn_area)
{
  int i, rv = 1;
  int layernum;
  LayerType *Layer;
  int n_entries = PCB->LayerGroups.Number[group];
  Cardinal *layers = PCB->LayerGroups.Entries[group];

  for (i = n_entries - 1; i >= 0; i--)
    {
      layernum = layers[i];
      Layer = PCB->Data->Layer + layers[i];
      if (strcmp (Layer->Name, "outline") == 0 ||
          strcmp (Layer->Name, "route") == 0)
        rv = 0;
      if (layernum < max_copper_layer && Layer->On)
        DrawLayer (Layer, drawn_area);
    }
  if (n_entries > 1)
    rv = 1;

  if (rv && !gui->gui)
    DrawPPV (group, drawn_area);
}

static void
GatherPVName (PinType *Ptr)
{
  BoxType box;
  bool vert = TEST_FLAG (EDGE2FLAG, Ptr);

  if (vert)
    {
      box.X1 = Ptr->X - Ptr->Thickness / 2 + Settings.PinoutTextOffsetY;
      box.Y1 = Ptr->Y - Ptr->DrillingHole / 2 - Settings.PinoutTextOffsetX;
    }
  else
    {
      box.X1 = Ptr->X + Ptr->DrillingHole / 2 + Settings.PinoutTextOffsetX;
      box.Y1 = Ptr->Y - Ptr->Thickness / 2 + Settings.PinoutTextOffsetY;
    }

  if (vert)
    {
      box.X2 = box.X1;
      box.Y2 = box.Y1;
    }
  else
    {
      box.X2 = box.X1;
      box.Y2 = box.Y1;
    }
  AddPart (&box);
}

static void
GatherPadName (PadType *Pad)
{
  BoxType box;
  bool vert;

  /* should text be vertical ? */
  vert = (Pad->Point1.X == Pad->Point2.X);

  if (vert)
    {
      box.X1 = Pad->Point1.X                      - Pad->Thickness / 2;
      box.Y1 = MAX (Pad->Point1.Y, Pad->Point2.Y) + Pad->Thickness / 2;
      box.X1 += Settings.PinoutTextOffsetY;
      box.Y1 -= Settings.PinoutTextOffsetX;
      box.X2 = box.X1;
      box.Y2 = box.Y1;
    }
  else
    {
      box.X1 = MIN (Pad->Point1.X, Pad->Point2.X) - Pad->Thickness / 2;
      box.Y1 = Pad->Point1.Y                      - Pad->Thickness / 2;
      box.X1 += Settings.PinoutTextOffsetX;
      box.Y1 += Settings.PinoutTextOffsetY;
      box.X2 = box.X1;
      box.Y2 = box.Y1;
    }

  AddPart (&box);
  return;
}

/* ---------------------------------------------------------------------------
 * draw a via object
 */
void
DrawVia (PinType *Via)
{
  AddPart (Via);
  if (!TEST_FLAG (HOLEFLAG, Via) && TEST_FLAG (DISPLAYNAMEFLAG, Via))
    DrawViaName (Via);
}

/* ---------------------------------------------------------------------------
 * draws the name of a via
 */
void
DrawViaName (PinType *Via)
{
  GatherPVName (Via);
}

/* ---------------------------------------------------------------------------
 * draw a pin object
 */
void
DrawPin (PinType *Pin)
{
  AddPart (Pin);
  if ((!TEST_FLAG (HOLEFLAG, Pin) && TEST_FLAG (DISPLAYNAMEFLAG, Pin))
      || doing_pinout)
    DrawPinName (Pin);
}

/* ---------------------------------------------------------------------------
 * draws the name of a pin
 */
void
DrawPinName (PinType *Pin)
{
  GatherPVName (Pin);
}

/* ---------------------------------------------------------------------------
 * draw a pad object
 */
void
DrawPad (PadType *Pad)
{
  AddPart (Pad);
  if (doing_pinout || TEST_FLAG (DISPLAYNAMEFLAG, Pad))
    DrawPadName (Pad);
}

/* ---------------------------------------------------------------------------
 * draws the name of a pad
 */
void
DrawPadName (PadType *Pad)
{
  GatherPadName (Pad);
}

/* ---------------------------------------------------------------------------
 * draws a line on a layer
 */
void
DrawLine (LayerType *Layer, LineType *Line)
{
  AddPart (Line);
}

/* ---------------------------------------------------------------------------
 * draws a ratline
 */
void
DrawRat (RatType *Rat)
{
  if (Settings.RatThickness < 100)
    Rat->Thickness = pixel_slop * Settings.RatThickness;
  /* rats.c set VIAFLAG if this rat goes to a containing poly: draw a donut */
  if (TEST_FLAG(VIAFLAG, Rat))
    {
      Coord w = Rat->Thickness;

      BoxType b;

      b.X1 = Rat->Point1.X - w * 2 - w / 2;
      b.X2 = Rat->Point1.X + w * 2 + w / 2;
      b.Y1 = Rat->Point1.Y - w * 2 - w / 2;
      b.Y2 = Rat->Point1.Y + w * 2 + w / 2;
      AddPart (&b);
    }
  else
    DrawLine (NULL, (LineType *)Rat);
}

/* ---------------------------------------------------------------------------
 * draws an arc on a layer
 */
void
DrawArc (LayerType *Layer, ArcType *Arc)
{
  AddPart (Arc);
}

/* ---------------------------------------------------------------------------
 * draws a text on a layer
 */
void
DrawText (LayerType *Layer, TextType *Text)
{
  AddPart (Text);
}


/* ---------------------------------------------------------------------------
 * draws a polygon on a layer
 */
void
DrawPolygon (LayerType *Layer, PolygonType *Polygon)
{
  AddPart (Polygon);
}

/* ---------------------------------------------------------------------------
 * draws an element
 */
void
DrawElement (ElementType *Element)
{
  DrawElementPackage (Element);
  DrawElementName (Element);
  DrawElementPinsAndPads (Element);
}

/* ---------------------------------------------------------------------------
 * draws the name of an element
 */
void
DrawElementName (ElementType *Element)
{
  if (TEST_FLAG (HIDENAMEFLAG, Element))
    return;
  DrawText (NULL, &ELEMENT_TEXT (PCB, Element));
}

/* ---------------------------------------------------------------------------
 * draws the package of an element
 */
void
DrawElementPackage (ElementType *Element)
{
  ELEMENTLINE_LOOP (Element);
  {
    DrawLine (NULL, line);
  }
  END_LOOP;
  ARC_LOOP (Element);
  {
    DrawArc (NULL, arc);
  }
  END_LOOP;
}

/* ---------------------------------------------------------------------------
 * draw pins of an element
 */
void
DrawElementPinsAndPads (ElementType *Element)
{
  PAD_LOOP (Element);
  {
    if (doing_pinout || doing_assy || FRONT (pad) || PCB->InvisibleObjectsOn)
      DrawPad (pad);
  }
  END_LOOP;
  PIN_LOOP (Element);
  {
    DrawPin (pin);
  }
  END_LOOP;
}

/* ---------------------------------------------------------------------------
 * erase a via
 */
void
EraseVia (PinType *Via)
{
  AddPart (Via);
  if (TEST_FLAG (DISPLAYNAMEFLAG, Via))
    EraseViaName (Via);
}

/* ---------------------------------------------------------------------------
 * erase a ratline
 */
void
EraseRat (RatType *Rat)
{
  if (TEST_FLAG(VIAFLAG, Rat))
    {
      Coord w = Rat->Thickness;

      BoxType b;

      b.X1 = Rat->Point1.X - w * 2 - w / 2;
      b.X2 = Rat->Point1.X + w * 2 + w / 2;
      b.Y1 = Rat->Point1.Y - w * 2 - w / 2;
      b.Y2 = Rat->Point1.Y + w * 2 + w / 2;
      AddPart (&b);
    }
  else
    EraseLine ((LineType *)Rat);
}


/* ---------------------------------------------------------------------------
 * erase a via name
 */
void
EraseViaName (PinType *Via)
{
  GatherPVName (Via);
}

/* ---------------------------------------------------------------------------
 * erase a pad object
 */
void
ErasePad (PadType *Pad)
{
  AddPart (Pad);
  if (TEST_FLAG (DISPLAYNAMEFLAG, Pad))
    ErasePadName (Pad);
}

/* ---------------------------------------------------------------------------
 * erase a pad name
 */
void
ErasePadName (PadType *Pad)
{
  GatherPadName (Pad);
}

/* ---------------------------------------------------------------------------
 * erase a pin object
 */
void
ErasePin (PinType *Pin)
{
  AddPart (Pin);
  if (TEST_FLAG (DISPLAYNAMEFLAG, Pin))
    ErasePinName (Pin);
}

/* ---------------------------------------------------------------------------
 * erase a pin name
 */
void
ErasePinName (PinType *Pin)
{
  GatherPVName (Pin);
}

/* ---------------------------------------------------------------------------
 * erases a line on a layer
 */
void
EraseLine (LineType *Line)
{
  AddPart (Line);
}

/* ---------------------------------------------------------------------------
 * erases an arc on a layer
 */
void
EraseArc (ArcType *Arc)
{
  if (!Arc->Thickness)
    return;
  AddPart (Arc);
}

/* ---------------------------------------------------------------------------
 * erases a text on a layer
 */
void
EraseText (LayerType *Layer, TextType *Text)
{
  AddPart (Text);
}

/* ---------------------------------------------------------------------------
 * erases a polygon on a layer
 */
void
ErasePolygon (PolygonType *Polygon)
{
  AddPart (Polygon);
}

/* ---------------------------------------------------------------------------
 * erases an element
 */
void
EraseElement (ElementType *Element)
{
  ELEMENTLINE_LOOP (Element);
  {
    EraseLine (line);
  }
  END_LOOP;
  ARC_LOOP (Element);
  {
    EraseArc (arc);
  }
  END_LOOP;
  EraseElementName (Element);
  EraseElementPinsAndPads (Element);
}

/* ---------------------------------------------------------------------------
 * erases all pins and pads of an element
 */
void
EraseElementPinsAndPads (ElementType *Element)
{
  PIN_LOOP (Element);
  {
    ErasePin (pin);
  }
  END_LOOP;
  PAD_LOOP (Element);
  {
    ErasePad (pad);
  }
  END_LOOP;
}

/* ---------------------------------------------------------------------------
 * erases the name of an element
 */
void
EraseElementName (ElementType *Element)
{
  if (TEST_FLAG (HIDENAMEFLAG, Element))
    return;
  DrawText (NULL, &ELEMENT_TEXT (PCB, Element));
}


void
EraseObject (int type, void *lptr, void *ptr)
{
  switch (type)
    {
    case VIA_TYPE:
    case PIN_TYPE:
      ErasePin ((PinType *) ptr);
      break;
    case TEXT_TYPE:
    case ELEMENTNAME_TYPE:
      EraseText ((LayerType *)lptr, (TextType *) ptr);
      break;
    case POLYGON_TYPE:
      ErasePolygon ((PolygonType *) ptr);
      break;
    case ELEMENT_TYPE:
      EraseElement ((ElementType *) ptr);
      break;
    case LINE_TYPE:
    case ELEMENTLINE_TYPE:
    case RATLINE_TYPE:
      EraseLine ((LineType *) ptr);
      break;
    case PAD_TYPE:
      ErasePad ((PadType *) ptr);
      break;
    case ARC_TYPE:
    case ELEMENTARC_TYPE:
      EraseArc ((ArcType *) ptr);
      break;
    default:
      Message ("hace: Internal ERROR, trying to erase an unknown type\n");
    }
}



void
DrawObject (int type, void *ptr1, void *ptr2)
{
  switch (type)
    {
    case VIA_TYPE:
      if (PCB->ViaOn)
	DrawVia ((PinType *) ptr2);
      break;
    case LINE_TYPE:
      if (((LayerType *) ptr1)->On)
	DrawLine ((LayerType *) ptr1, (LineType *) ptr2);
      break;
    case ARC_TYPE:
      if (((LayerType *) ptr1)->On)
	DrawArc ((LayerType *) ptr1, (ArcType *) ptr2);
      break;
    case TEXT_TYPE:
      if (((LayerType *) ptr1)->On)
	DrawText ((LayerType *) ptr1, (TextType *) ptr2);
      break;
    case POLYGON_TYPE:
      if (((LayerType *) ptr1)->On)
	DrawPolygon ((LayerType *) ptr1, (PolygonType *) ptr2);
      break;
    case ELEMENT_TYPE:
      if (PCB->ElementOn &&
	  (FRONT ((ElementType *) ptr2) || PCB->InvisibleObjectsOn))
	DrawElement ((ElementType *) ptr2);
      break;
    case RATLINE_TYPE:
      if (PCB->RatOn)
	DrawRat ((RatType *) ptr2);
      break;
    case PIN_TYPE:
      if (PCB->PinOn)
	DrawPin ((PinType *) ptr2);
      break;
    case PAD_TYPE:
      if (PCB->PinOn)
	DrawPad ((PadType *) ptr2);
      break;
    case ELEMENTNAME_TYPE:
      if (PCB->ElementOn &&
	  (FRONT ((ElementType *) ptr2) || PCB->InvisibleObjectsOn))
	DrawElementName ((ElementType *) ptr1);
      break;
    }
}

static void
draw_element (ElementType *element)
{
  draw_element_package (element);
  draw_element_name (element);
  draw_element_pins_and_pads (element);
}

/* ---------------------------------------------------------------------------
 * HID drawing callback.
 */

void
hid_expose_callback (HID * hid, BoxType * region, void *item)
{
  HID *old_gui = gui;

  gui = hid;
  Output.fgGC = gui->graphics->make_gc ();
  Output.bgGC = gui->graphics->make_gc ();
  Output.pmGC = gui->graphics->make_gc ();

  hid->graphics->set_color (Output.pmGC, "erase");
  hid->graphics->set_color (Output.bgGC, "drill");

  if (item)
    {
      doing_pinout = true;
      draw_element ((ElementType *)item);
      doing_pinout = false;
    }
  else
    DrawEverything (region);

  gui->graphics->destroy_gc (Output.fgGC);
  gui->graphics->destroy_gc (Output.bgGC);
  gui->graphics->destroy_gc (Output.pmGC);
  gui = old_gui;
}
