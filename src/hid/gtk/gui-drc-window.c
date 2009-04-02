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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "error.h"
#include "search.h"
#include "draw.h"
#include "undo.h"
#include "set.h"
#include "gui.h"
#include "gui-drc-window.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

RCSID ("$Id$");

static GtkWidget *drc_window, *drc_list;
static GtkListStore *drc_list_model = NULL;
static int num_violations = 0;

/* Remember user window resizes. */
static gint
drc_window_configure_event_cb (GtkWidget * widget,
			       GdkEventConfigure * ev, gpointer data)
{
  ghidgui->drc_window_width = widget->allocation.width;
  ghidgui->drc_window_height = widget->allocation.height;
  ghidgui->config_modified = TRUE;

  return FALSE;
}

static void
drc_close_cb (gpointer data)
{
  gtk_widget_destroy (drc_window);
  drc_window = NULL;
}

static void
drc_destroy_cb (GtkWidget * widget, gpointer data)
{
  drc_window = NULL;
}

enum {
  DRC_VIOLATION_NUM_COL = 0,
  DRC_VIOLATION_OBJ_COL,
  DRC_OBJECT_COUNT_COL,
  DRC_OBJECT_ID_LIST_COL,
  DRC_OBJECT_TYPE_LIST_COL,
  NUM_DRC_COLUMNS
};


#warning NASTY, DOESNT USE UNDO PROPERLY
static void
unset_found_flags (int AndDraw)
{
  int flag = FOUNDFLAG;
  int change = 0;

  VIA_LOOP (PCB->Data);
  {
    if (TEST_FLAG (flag, via))
      {
	if (AndDraw)
	  AddObjectToFlagUndoList (VIA_TYPE, via, via, via);
	CLEAR_FLAG (flag, via);
	if (AndDraw)
	  DrawVia (via, 0);
	change = True;
      }
  }
  END_LOOP;
  ELEMENT_LOOP (PCB->Data);
  {
    PIN_LOOP (element);
    {
      if (TEST_FLAG (flag, pin))
	{
	  if (AndDraw)
	    AddObjectToFlagUndoList (PIN_TYPE, element, pin, pin);
	  CLEAR_FLAG (flag, pin);
	  if (AndDraw)
	    DrawPin (pin, 0);
	  change = True;
	}
    }
    END_LOOP;
    PAD_LOOP (element);
    {
      if (TEST_FLAG (flag, pad))
	{
	  if (AndDraw)
	    AddObjectToFlagUndoList (PAD_TYPE, element, pad, pad);
	  CLEAR_FLAG (flag, pad);
	  if (AndDraw)
	    DrawPad (pad, 0);
	  change = True;
	}
    }
    END_LOOP;
  }
  END_LOOP;
  RAT_LOOP (PCB->Data);
  {
    if (TEST_FLAG (flag, line))
      {
	if (AndDraw)
	  AddObjectToFlagUndoList (RATLINE_TYPE, line, line, line);
	CLEAR_FLAG (flag, line);
	if (AndDraw)
	  DrawRat (line, 0);
	change = True;
      }
  }
  END_LOOP;
  COPPERLINE_LOOP (PCB->Data);
  {
    if (TEST_FLAG (flag, line))
      {
	if (AndDraw)
	  AddObjectToFlagUndoList (LINE_TYPE, layer, line, line);
	CLEAR_FLAG (flag, line);
	if (AndDraw)
	  DrawLine (layer, line, 0);
	change = True;
      }
  }
  ENDALL_LOOP;
  COPPERARC_LOOP (PCB->Data);
  {
    if (TEST_FLAG (flag, arc))
      {
	if (AndDraw)
	  AddObjectToFlagUndoList (ARC_TYPE, layer, arc, arc);
	CLEAR_FLAG (flag, arc);
	if (AndDraw)
	  DrawArc (layer, arc, 0);
	change = True;
      }
  }
  ENDALL_LOOP;
  COPPERPOLYGON_LOOP (PCB->Data);
  {
    if (TEST_FLAG (flag, polygon))
      {
	if (AndDraw)
	  AddObjectToFlagUndoList (POLYGON_TYPE, layer, polygon, polygon);
	CLEAR_FLAG (flag, polygon);
	if (AndDraw)
	  DrawPolygon (layer, polygon, 0);
	change = True;
      }
  }
  ENDALL_LOOP;
  if (change)
    {
      SetChangedFlag (True);
      if (AndDraw)
	{
	  IncrementUndoSerialNumber ();
	  Draw ();
	}
    }
}

static void
selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GhidDrcViolation *violation;
  int object_count;
#if 0
  long int *object_id_list;
  int *object_type_list;
#else
  long int object_id;
  int object_type;
#endif
  int i;

#warning CHEATING HERE!
  /* I happen to know I'm only putting 0 or 1 items in the lists of objects.
     To do this properly requires some memory allocation and tracking to
     avoid leaks! */

  /* Check there is anything selected, if not; return */
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      /* Unflag objects */
      unset_found_flags (True);
      return;
    }

  /* Check the selected node has children, if so; return. */
  if (gtk_tree_model_iter_has_child (model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
		      DRC_VIOLATION_OBJ_COL,    &violation,
		      DRC_OBJECT_COUNT_COL,     &object_count,
#if 0
		      DRC_OBJECT_ID_LIST_COL,   &object_id_list,
		      DRC_OBJECT_TYPE_LIST_COL, &object_type_list,
#else
		      DRC_OBJECT_ID_LIST_COL,   &object_id,
		      DRC_OBJECT_TYPE_LIST_COL, &object_type,
#endif
		      -1);

  unset_found_flags (False);

  if (violation == NULL)
    return;

  /* Flag the objects listed against this DRC violation */
  for (i = 0; i < object_count; i++)
    {
      int found_type;
      void *ptr1, *ptr2, *ptr3;

      found_type = SearchObjectByID (PCB->Data, &ptr1, &ptr2, &ptr3,
				     object_id, object_type);
      if (found_type == NO_TYPE)
	{
	  Message (_("Object ID %i identified during DRC was not found. Stale DRC window?\n"),
		   object_id);
	  continue;
	}
      SET_FLAG (FOUNDFLAG, (AnyObjectType *)ptr2);
      switch (object_type)
	{
	case LINE_TYPE:
	case ARC_TYPE:
	case POLYGON_TYPE:
	  ChangeGroupVisibility (GetLayerNumber (PCB->Data, (LayerTypePtr) ptr1), True, True);
	}
      DrawObject (object_type, ptr1, ptr2, 0);
    }
  Draw();
  CenterDisplay (violation->x_coord, violation->y_coord, False);
}


enum
{
  PROP_TITLE = 1,
  PROP_EXPLANATION,
  PROP_X_COORD,
  PROP_Y_COORD,
  PROP_ANGLE,
  PROP_HAVE_MEASURED,
  PROP_MEASURED_VALUE,
  PROP_REQUIRED_VALUE,
  PROP_VALUE_DIGITS,
  PROP_VALUE_UNITS
};


static GObjectClass *ghid_drc_violation_parent_class = NULL;


/*! \brief GObject finalise handler
 *
 *  \par Function Description
 *  Just before the GhidDrcViolation GObject is finalized, free our
 *  allocated data, and then chain up to the parent's finalize handler.
 *
 *  \param [in] widget  The GObject being finalized.
 */
static void
ghid_drc_violation_finalize (GObject * object)
{
  GhidDrcViolation *violation = GHID_DRC_VIOLATION (object);

  g_free (violation->title);
  g_free (violation->explanation);
  g_free (violation->value_units);

  G_OBJECT_CLASS (ghid_drc_violation_parent_class)->finalize (object);
}

/*! \brief GObject property setter function
 *
 *  \par Function Description
 *  Setter function for GhidDrcViolation's GObject properties,
 *  "settings-name" and "toplevel"
 *
 *  \param [in]  object       The GObject whose properties we are setting
 *  \param [in]  property_id  The numeric id. under which the property was
 *                            registered with g_object_class_install_property()
 *  \param [in]  value        The GValue the property is being set from
 *  \param [in]  pspec        A GParamSpec describing the property being set
 */
static void
ghid_drc_violation_set_property (GObject * object, guint property_id,
				  const GValue * value, GParamSpec * pspec)
{
  GhidDrcViolation *violation = GHID_DRC_VIOLATION (object);

  switch (property_id)
    {
    case PROP_TITLE:
      g_free (violation->title);
      violation->title = g_value_dup_string (value);
      break;
    case PROP_EXPLANATION:
      g_free (violation->explanation);
      violation->explanation = g_value_dup_string (value);
      break;
    case PROP_X_COORD:
      violation->x_coord = g_value_get_int (value);
      break;
    case PROP_Y_COORD:
      violation->y_coord = g_value_get_int (value);
      break;
    case PROP_ANGLE:
      violation->angle = g_value_get_int (value);
      break;
    case PROP_HAVE_MEASURED:
      violation->have_measured = g_value_get_int (value);
      break;
    case PROP_MEASURED_VALUE:
      violation->measured_value = g_value_get_double (value);
      break;
    case PROP_REQUIRED_VALUE:
      violation->required_value = g_value_get_double (value);
      break;
    case PROP_VALUE_DIGITS:
      violation->value_digits = g_value_get_int (value);
      break;
    case PROP_VALUE_UNITS:
      g_free (violation->value_units);
      violation->value_units = g_value_dup_string (value);
      break;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      return;
    }
}


/*! \brief GObject property getter function
 *
 *  \par Function Description
 *  Getter function for GhidDrcViolation's GObject properties,
 *  "settings-name" and "toplevel".
 *
 *  \param [in]  object       The GObject whose properties we are getting
 *  \param [in]  property_id  The numeric id. under which the property was
 *                            registered with g_object_class_install_property()
 *  \param [out] value        The GValue in which to return the value of the property
 *  \param [in]  pspec        A GParamSpec describing the property being got
 */
static void
ghid_drc_violation_get_property (GObject * object, guint property_id,
				  GValue * value, GParamSpec * pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }

}


/*! \brief GType class initialiser for GhidDrcViolation
 *
 *  \par Function Description
 *  GType class initialiser for GhidDrcViolation. We override our parent
 *  virtual class methods as needed and register our GObject properties.
 *
 *  \param [in]  klass       The GhidDrcViolationClass we are initialising
 */
static void
ghid_drc_violation_class_init (GhidViolationRendererClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = ghid_drc_violation_finalize;
  gobject_class->set_property = ghid_drc_violation_set_property;
  gobject_class->get_property = ghid_drc_violation_get_property;

  ghid_drc_violation_parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (gobject_class, PROP_TITLE,
				   g_param_spec_string ("title",
							"",
							"",
							"",
							G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_EXPLANATION,
				   g_param_spec_string ("explanation",
							"",
							"",
							"",
							G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_X_COORD,
				   g_param_spec_int ("x-coord",
						     "",
						     "",
						     G_MININT,
						     G_MAXINT,
						     0,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_Y_COORD,
				   g_param_spec_int ("y-coord",
						     "",
						     "",
						     G_MININT,
						     G_MAXINT,
						     0,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_ANGLE,
				   g_param_spec_int ("angle",
						     "",
						     "",
						     G_MININT,
						     G_MAXINT,
						     0,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_HAVE_MEASURED,
				   g_param_spec_int ("have-measured",
						     "",
						     "",
						     G_MININT,
						     G_MAXINT,
						     0,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_MEASURED_VALUE,
				   g_param_spec_double ("measured-value",
						     "",
						     "",
						     -G_MAXDOUBLE,
						     G_MAXDOUBLE,
						     0.,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_REQUIRED_VALUE,
				   g_param_spec_double ("required-value",
						     "",
						     "",
						     -G_MINDOUBLE,
						     G_MAXDOUBLE,
						     0.,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_VALUE_DIGITS,
				   g_param_spec_int ("value-digits",
						     "",
						     "",
						     0,
						     G_MAXINT,
						     0,
						     G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_VALUE_UNITS,
				   g_param_spec_string ("value-units",
							"",
							"",
							"",
							G_PARAM_WRITABLE));
}



/*! \brief Function to retrieve GhidViolationRenderer's GType identifier.
 *
 *  \par Function Description
 *  Function to retrieve GhidViolationRenderer's GType identifier.
 *  Upon first call, this registers the GhidViolationRenderer in the GType system.
 *  Subsequently it returns the saved value from its first execution.
 *
 *  \return the GType identifier associated with GhidViolationRenderer.
 */
GType
ghid_drc_violation_get_type ()
{
  static GType ghid_drc_violation_type = 0;

  if (!ghid_drc_violation_type)
    {
      static const GTypeInfo ghid_drc_violation_info = {
	sizeof (GhidDrcViolationClass),
	NULL,			/* base_init */
	NULL,			/* base_finalize */
	(GClassInitFunc) ghid_drc_violation_class_init,
	NULL,			/* class_finalize */
	NULL,			/* class_data */
	sizeof (GhidDrcViolation),
	0,			/* n_preallocs */
	NULL,			/* instance_init */
      };

      ghid_drc_violation_type =
	g_type_register_static (G_TYPE_OBJECT, "GhidDrcViolation",
				&ghid_drc_violation_info, 0);
    }

  return ghid_drc_violation_type;
}


GhidDrcViolation *ghid_drc_violation_new (char *title,
					  char *explanation,
					  int x_coord,
					  int y_coord,
					  int angle,
					  int have_measured,
					  double measured_value,
					  double required_value,
					  int value_digits,
					  const char *value_units,
					  int object_count,
					  long int object_id,
					  int object_type)
{
  GhidDrcViolation *violation = g_object_new (GHID_TYPE_DRC_VIOLATION,
					     "title",          title,
					     "explanation",    explanation,
					     "x-coord",        x_coord,
					     "y-coord",        y_coord,
					     "angle",          angle,
					     "have-measured",  have_measured,
					     "measured-value", measured_value,
					     "required-value", required_value,
					     "value-digits",   value_digits,
					     "value-units",    value_units,
					     NULL);
//  violation->object_count     = object_count;
//  violation->object_id        = object_id;
//  violation->object_type      = object_type;
  return violation;
}

enum
{
  PROP_VIOLATION = 1,
};


static GObjectClass *ghid_violation_renderer_parent_class = NULL;


/*! \brief GObject finalise handler
 *
 *  \par Function Description
 *  Just before the GhidViolationRenderer GObject is finalized, free our
 *  allocated data, and then chain up to the parent's finalize handler.
 *
 *  \param [in] widget  The GObject being finalized.
 */
static void
ghid_violation_renderer_finalize (GObject * object)
{
  GhidViolationRenderer *renderer = GHID_VIOLATION_RENDERER (object);

  if (renderer->violation != NULL)
    g_object_unref (renderer->violation);

  G_OBJECT_CLASS (ghid_violation_renderer_parent_class)->finalize (object);
}


/*! \brief GObject property setter function
 *
 *  \par Function Description
 *  Setter function for GhidViolationRenderer's GObject properties,
 *  "settings-name" and "toplevel".
 *
 *  \param [in]  object       The GObject whose properties we are setting
 *  \param [in]  property_id  The numeric id. under which the property was
 *                            registered with g_object_class_install_property()
 *  \param [in]  value        The GValue the property is being set from
 *  \param [in]  pspec        A GParamSpec describing the property being set
 */
static void
ghid_violation_renderer_set_property (GObject * object, guint property_id,
				  const GValue * value, GParamSpec * pspec)
{
  GhidViolationRenderer *renderer = GHID_VIOLATION_RENDERER (object);
  char *markup;

  switch (property_id)
    {
    case PROP_VIOLATION:
      if (renderer->violation != NULL)
	g_object_unref (renderer->violation);
      renderer->violation = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      return;
    }

  if (renderer->violation == NULL)
    return;

  if (renderer->violation->have_measured)
    {
      markup = g_strdup_printf ("<b>%s (%.*f %s)</b>\n"
				"<span font_size='1024'> </span>\n"
				"<small>"
				  "<i>%s</i>\n"
				  "<span font_size='5120'> </span>\n"
				  "Required: %.*f %s"
				"</small>",
				renderer->violation->title,
				renderer->violation->value_digits,
				renderer->violation->measured_value,
				renderer->violation->value_units,
				renderer->violation->explanation,
				renderer->violation->value_digits,
				renderer->violation->required_value,
				renderer->violation->value_units);
    }
  else
    {
      markup = g_strdup_printf ("<b>%s</b>\n"
				"<span font_size='1024'> </span>\n"
				"<small>"
				  "<i>%s</i>\n"
				  "<span font_size='5120'> </span>\n"
				  "Required: %.*f %s"
				"</small>",
				renderer->violation->title,
				renderer->violation->explanation,
				renderer->violation->value_digits,
				renderer->violation->required_value,
				renderer->violation->value_units);
    }

  g_object_set (object, "markup", markup, NULL);
  g_free (markup);
}


/*! \brief GObject property getter function
 *
 *  \par Function Description
 *  Getter function for GhidViolationRenderer's GObject properties.
 *
 *  \param [in]  object       The GObject whose properties we are getting
 *  \param [in]  property_id  The numeric id. under which the property was
 *                            registered with g_object_class_install_property()
 *  \param [out] value        The GValue in which to return the value of the property
 *  \param [in]  pspec        A GParamSpec describing the property being got
 */
static void
ghid_violation_renderer_get_property (GObject * object, guint property_id,
				  GValue * value, GParamSpec * pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }

}


/*! \brief GType class initialiser for GhidViolationRenderer
 *
 *  \par Function Description
 *  GType class initialiser for GhidViolationRenderer. We override our parent
 *  virtual class methods as needed and register our GObject properties.
 *
 *  \param [in]  klass       The GhidViolationRendererClass we are initialising
 */
static void
ghid_violation_renderer_class_init (GhidViolationRendererClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = ghid_violation_renderer_finalize;
  gobject_class->set_property = ghid_violation_renderer_set_property;
  gobject_class->get_property = ghid_violation_renderer_get_property;

  ghid_violation_renderer_parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (gobject_class, PROP_VIOLATION,
				   g_param_spec_object ("violation",
							"",
							"",
							GHID_TYPE_DRC_VIOLATION,
							G_PARAM_WRITABLE));
}


/*! \brief Function to retrieve GhidViolationRenderer's GType identifier.
 *
 *  \par Function Description
 *  Function to retrieve GhidViolationRenderer's GType identifier.
 *  Upon first call, this registers the GhidViolationRenderer in the GType system.
 *  Subsequently it returns the saved value from its first execution.
 *
 *  \return the GType identifier associated with GhidViolationRenderer.
 */
GType
ghid_violation_renderer_get_type ()
{
  static GType ghid_violation_renderer_type = 0;

  if (!ghid_violation_renderer_type)
    {
      static const GTypeInfo ghid_violation_renderer_info = {
	sizeof (GhidViolationRendererClass),
	NULL,			/* base_init */
	NULL,			/* base_finalize */
	(GClassInitFunc) ghid_violation_renderer_class_init,
	NULL,			/* class_finalize */
	NULL,			/* class_data */
	sizeof (GhidViolationRenderer),
	0,			/* n_preallocs */
	NULL,			/* instance_init */
      };

      ghid_violation_renderer_type =
	g_type_register_static (GTK_TYPE_CELL_RENDERER_TEXT, "GhidViolationRenderer",
				&ghid_violation_renderer_info, 0);
    }

  return ghid_violation_renderer_type;
}


/*! \brief Convenience function to create a new violation renderer
 *
 *  \par Function Description
 *  Convenience function which creates a GhidViolationRenderer.
 *
 *  \return  The GhidViolationRenderer created.
 */
GtkCellRenderer *
ghid_violation_renderer_new (void)
{
  GhidViolationRenderer *renderer;

  renderer = g_object_new (GHID_TYPE_VIOLATION_RENDERER,
			   "ypad", 6,
			   NULL);

  return GTK_CELL_RENDERER (renderer);
}


void
ghid_drc_window_show (gboolean raise)
{
  GtkWidget *vbox, *hbox, *button, *scrolled_window;
  GtkCellRenderer *violation_renderer;

  if (drc_window)
    {
      if (raise)
	gtk_window_present(GTK_WINDOW(drc_window));
      return;
    }

 drc_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (drc_window), "destroy",
		    G_CALLBACK (drc_destroy_cb), NULL);
  g_signal_connect (G_OBJECT (drc_window), "configure_event",
		    G_CALLBACK (drc_window_configure_event_cb), NULL);
  gtk_window_set_title (GTK_WINDOW (drc_window), _("PCB DRC"));
  gtk_window_set_wmclass (GTK_WINDOW (drc_window), "PCB_DRC", "PCB");
  gtk_window_set_default_size (GTK_WINDOW (drc_window),
			       ghidgui->drc_window_width,
			       ghidgui->drc_window_height);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_container_add (GTK_CONTAINER (drc_window), vbox);

  drc_list_model = gtk_list_store_new (NUM_DRC_COLUMNS,
				       G_TYPE_INT,      /* DRC_VIOLATION_NUM_COL    */
				       G_TYPE_OBJECT,   /* DRC_VIOLATION_OBJ_COL    */
				       G_TYPE_INT,      /* DRC_OBJECT_COUNT_COL     */
#warning CHEATING HERE!
  /* I happen to know I'm only putting 0 or 1 items in the lists of objects.
     To do this properly requires some memory allocation and tracking to
     avoid leaks! */
#if 0
				       G_TYPE_POINTER,  /* DRC_OBJECT_ID_LIST_COL   */
				       G_TYPE_POINTER); /* DRC_OBJECT_TYPE_LIST_COL */
#else
				       G_TYPE_LONG,     /* DRC_OBJECT_ID_LIST_COL   */
				       G_TYPE_INT);     /* DRC_OBJECT_TYPE_LIST_COL */
#endif

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_window,
		      TRUE /* EXPAND */, TRUE /* FILL */, 0 /* PADDING */);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  drc_list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (drc_list_model));
  gtk_container_add (GTK_CONTAINER (scrolled_window), drc_list);

  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (drc_list), TRUE);
  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (drc_list)), "changed",
		    G_CALLBACK (selection_changed_cb), NULL);

  violation_renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (drc_list),
					       -1, /* APPEND */
					       _("No."), /* TITLE */
					       violation_renderer,
					       "text", DRC_VIOLATION_NUM_COL,
					       NULL);

  violation_renderer = ghid_violation_renderer_new ();
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (drc_list),
					       -1, /* APPEND */
					       _("Violation details"), /* TITLE */
					       violation_renderer,
					       "violation", DRC_VIOLATION_OBJ_COL,
					       NULL);

  hbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (hbox), GTK_BUTTONBOX_END);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  button = gtk_button_new_from_stock (GTK_STOCK_CLOSE);
  g_signal_connect (G_OBJECT (button), "clicked",
		    G_CALLBACK (drc_close_cb), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_widget_realize (drc_window);
  if (Settings.AutoPlace)
    gtk_widget_set_uposition (GTK_WIDGET (drc_window), 10, 10);
  gtk_widget_show_all (drc_window);
}

#define UNIT1(value) (Settings.grid_units_mm ? ((value) / 100000.0 * 25.4) : ((value) / 100.0))
#define UNIT(value) UNIT1(value) , (Settings.grid_units_mm ? "mm" : "mils")

void ghid_drc_window_append_violation (DRC_VIOLATION *violation)
{
  GhidDrcViolation *violation_obj;
  GtkTreeIter iter;
  long int ID = 0;
  int type = NO_TYPE;

  /* Ensure the required structures are setup */
  ghid_drc_window_show (FALSE);

  num_violations++;

  /* Copy the violating object lists */
#warning CHEATING HERE!
  /* I happen to know I'm only putting 0 or 1 items in the lists of objects.
     To do this properly requires some memory allocation and tracking to
     avoid leaks! */
  if (violation->object_count > 0)
    {
      ID = violation->object_id_list[0];
      type = violation->object_type_list[0];
    }

  violation_obj = ghid_drc_violation_new (violation->title,
					  violation->explanation,
					  violation->x,
					  violation->y,
					  violation->angle,
					  violation->have_measured,
					  violation->measured_value,
					  violation->required_value,
					  violation->value_digits,
					  violation->value_units,
					  violation->object_count,
					  ID,    /* violation->object_id_list */
					  type); /* violation->object_type_list */

  gtk_list_store_append (drc_list_model, &iter);
  gtk_list_store_set (drc_list_model, &iter,
		      DRC_VIOLATION_NUM_COL,    num_violations,
		      DRC_VIOLATION_OBJ_COL,    violation_obj,
		      DRC_OBJECT_COUNT_COL,     violation->object_count,
		      DRC_OBJECT_ID_LIST_COL,   ID,   /* violation->object_id_list, */
		      DRC_OBJECT_TYPE_LIST_COL, type, /* violation->object_type_list, */
		      -1);

  g_object_unref (violation_obj);
}

void ghid_drc_window_reset_message (void)
{
  if (drc_list_model != NULL)
    gtk_list_store_clear (drc_list_model);
  num_violations = 0;
}

int ghid_drc_window_throw_dialog (void)
{
  ghid_drc_window_show (TRUE);
  return 1;
}

