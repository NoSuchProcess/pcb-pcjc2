/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Intrinsic.h>
#include <X11/X.h>
#include <X11/Xlib.h>

#include <Xm/Xm.h>
#include <Xm/FileSB.h>
#include <Xm/RowColumn.h>
#include <Xm/Form.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/Label.h>
#include <Xm/MessageB.h>
#include <Xm/Scale.h>
#include <Xm/Separator.h>

#include "compat.h"
#include "global.h"
#include "data.h"
#include "crosshair.h"
#include "misc.h"

#include "hid.h"
#include "../hidint.h"
#include "lesstif.h"

#ifdef HAVE_LIBDMALLOC
#include <dmalloc.h>
#endif

RCSID ("$Id$");

#define CRASH fprintf(stderr, "HID error: pcb called unimplemented GUI function %s\n", __FUNCTION__), abort()

static Arg args[30];
static int n;
#define stdarg(t,v) XtSetArg(args[n], t, v); n++

static int ok;

/* ------------------------------------------------------------ */

static void
dialog_callback (Widget w, void *v, void *cbs)
{
  ok = (int) v;
}

static int
wait_for_dialog (Widget w)
{
  ok = -1;
  XtManageChild (w);
  while (ok == -1 && XtIsManaged (w))
    {
      XEvent e;
      XtAppNextEvent (app_context, &e);
      XtDispatchEvent (&e);
    }
  XtUnmanageChild (w);
  return ok;
}

/* ------------------------------------------------------------ */

static Widget fsb = 0;
static XmString xms_pcb, xms_net, xms_vend, xms_all, xms_load, xms_loadv,
  xms_save;

static void
setup_fsb_dialog ()
{
  if (fsb)
    return;

  xms_pcb = XmStringCreateLocalized ("*.pcb");
  xms_net = XmStringCreateLocalized ("*.net");
  xms_vend = XmStringCreateLocalized ("*.vend");
  xms_all = XmStringCreateLocalized ("*");
  xms_load = XmStringCreateLocalized ("Load From");
  xms_loadv = XmStringCreateLocalized ("Load Vendor");
  xms_save = XmStringCreateLocalized ("Save As");

  n = 0;
  fsb = XmCreateFileSelectionDialog (mainwind, "file", args, n);

  XtAddCallback (fsb, XmNokCallback, (XtCallbackProc) dialog_callback,
		 (XtPointer) 1);
  XtAddCallback (fsb, XmNcancelCallback, (XtCallbackProc) dialog_callback,
		 (XtPointer) 0);
}

static int
Load (int argc, char **argv, int x, int y)
{
  char *function;
  char *name;
  XmString xmname, pattern;

  if (argc > 1)
    return hid_actionv ("LoadFrom", argc, argv);

  function = argc ? argv[0] : "Layout";

  setup_fsb_dialog ();

  if (strcasecmp (function, "Netlist") == 0)
    pattern = xms_net;
  else
    pattern = xms_pcb;

  n = 0;
  stdarg (XmNtitle, "Load From");
  XtSetValues (XtParent (fsb), args, n);

  n = 0;
  stdarg (XmNpattern, pattern);
  stdarg (XmNmustMatch, True);
  stdarg (XmNselectionLabelString, xms_load);
  XtSetValues (fsb, args, n);

  if (!wait_for_dialog (fsb))
    return 1;

  n = 0;
  stdarg (XmNdirSpec, &xmname);
  XtGetValues (fsb, args, n);

  XmStringGetLtoR (xmname, XmFONTLIST_DEFAULT_TAG, &name);

  hid_actionl ("LoadFrom", function, name, NULL);

  XtFree (name);

  return 0;
}

static int
LoadVendor (int argc, char **argv, int x, int y)
{
  char *name;
  XmString xmname, pattern;

  if (argc > 0)
    return hid_actionv ("LoadVendorFrom", argc, argv);

  setup_fsb_dialog ();

  pattern = xms_vend;

  n = 0;
  stdarg (XmNtitle, "Load Vendor");
  XtSetValues (XtParent (fsb), args, n);

  n = 0;
  stdarg (XmNpattern, pattern);
  stdarg (XmNmustMatch, True);
  stdarg (XmNselectionLabelString, xms_loadv);
  XtSetValues (fsb, args, n);

  if (!wait_for_dialog (fsb))
    return 1;

  n = 0;
  stdarg (XmNdirSpec, &xmname);
  XtGetValues (fsb, args, n);

  XmStringGetLtoR (xmname, XmFONTLIST_DEFAULT_TAG, &name);

  hid_actionl ("LoadVendorFrom", name, NULL);

  XtFree (name);

  return 0;
}

static int
Save (int argc, char **argv, int x, int y)
{
  char *function;
  char *name;
  XmString xmname, pattern;

  if (argc > 1)
    hid_actionv ("SaveTo", argc, argv);

  function = argc ? argv[0] : "Layout";
  
  if (strcasecmp (function, "Layout") == 0)
    if (PCB->Filename)
      return hid_actionl ("SaveTo", "Layout", PCB->Filename, NULL);

  setup_fsb_dialog ();

  pattern = xms_pcb;

  XtManageChild (fsb);

  n = 0;
  stdarg (XmNtitle, "Save As");
  XtSetValues (XtParent (fsb), args, n);

  n = 0;
  stdarg (XmNpattern, pattern);
  stdarg (XmNmustMatch, False);
  stdarg (XmNselectionLabelString, xms_save);
  XtSetValues (fsb, args, n);

  if (!wait_for_dialog (fsb))
    return 1;

  n = 0;
  stdarg (XmNdirSpec, &xmname);
  XtGetValues (fsb, args, n);

  XmStringGetLtoR (xmname, XmFONTLIST_DEFAULT_TAG, &name);

  if (strcasecmp (function, "PasteBuffer") == 0)
    hid_actionl ("PasteBuffer", "Save", name, NULL);
  else
    {
      /* 
       * if we got this far and the function is Layout, then
       * we really needed it to be a LayoutAs.  Otherwise 
       * ActionSaveTo() will ignore the new file name we
       * just obtained.
       */
      if (strcasecmp (function, "Layout") == 0)
	hid_actionl ("SaveTo", "LayoutAs", name, NULL);
      else
	hid_actionl ("SaveTo", function, name, NULL);
    }
  XtFree (name);

  return 0;
}

/* ------------------------------------------------------------ */

static Widget log_form, log_text;
static char *msg_buffer = 0;
static int msg_buffer_size = 0;
static int log_size = 0;
static int pending_newline = 0;

static void
log_clear (Widget w, void *up, void *cbp)
{
  XmTextSetString (log_text, "");
  log_size = 0;
  pending_newline = 0;
}

void
lesstif_logv (char *fmt, va_list ap)
{
  int i;
  char *bp;
  if (!mainwind)
    {
      vprintf (fmt, ap);
      return;
    }
  if (!log_form)
    {
      Widget clear_button;

      n = 0;
      stdarg (XmNautoUnmanage, False);
      stdarg (XmNwidth, 600);
      stdarg (XmNheight, 200);
      stdarg (XmNtitle, "PCB Log");
      log_form = XmCreateFormDialog (mainwind, "log", args, n);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_FORM);
      clear_button = XmCreatePushButton (log_form, "clear", args, n);
      XtManageChild (clear_button);
      XtAddCallback (clear_button, XmNactivateCallback,
		     (XtCallbackProc) log_clear, 0);

      n = 0;
      stdarg (XmNeditable, False);
      stdarg (XmNeditMode, XmMULTI_LINE_EDIT);
      stdarg (XmNcursorPositionVisible, True);
      stdarg (XmNtopAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNbottomWidget, clear_button);
      log_text = XmCreateScrolledText (log_form, "text", args, n);
      XtManageChild (log_text);

      msg_buffer = (char *) malloc (1002);
      msg_buffer_size = 1002;
      XtManageChild (log_form);
    }
  bp = msg_buffer;
  if (pending_newline)
    {
      *bp++ = '\n';
      pending_newline = 0;
    }
#ifdef HAVE_VSNPRINTF
  i = vsnprintf (bp, msg_buffer_size, fmt, ap);
  if (i >= msg_buffer_size)
    {
      msg_buffer_size = i + 100;
      msg_buffer = (char *) realloc (msg_buffer, msg_buffer_size + 2);
      vsnprintf (bp, msg_buffer_size, fmt, ap);
    }
#else
  vsprintf (bp, fmt, ap);
#endif
  bp = msg_buffer + strlen (msg_buffer) - 1;
  while (bp >= msg_buffer && bp[0] == '\n')
    {
      pending_newline++;
      *bp-- = 0;
    }
  XmTextInsert (log_text, log_size, msg_buffer);
  log_size += strlen (msg_buffer);
  XmTextSetCursorPosition (log_text, log_size);
}

void
lesstif_log (char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  lesstif_logv (fmt, ap);
  va_end (ap);
}

/* ------------------------------------------------------------ */

static Widget confirm_dialog = 0;
static Widget confirm_cancel, confirm_ok, confirm_label;

int
lesstif_confirm_dialog (char *msg, ...)
{
  char *cancelmsg, *okmsg;
  va_list ap;
  XmString xs;

  if (mainwind == 0)
    return 1;

  if (confirm_dialog == 0)
    {
      n = 0;
      stdarg (XmNdefaultButtonType, XmDIALOG_OK_BUTTON);
      stdarg (XmNtitle, "Confirm");
      confirm_dialog = XmCreateQuestionDialog (mainwind, "confirm", args, n);
      XtAddCallback (confirm_dialog, XmNcancelCallback,
		     (XtCallbackProc) dialog_callback, (XtPointer) 0);
      XtAddCallback (confirm_dialog, XmNokCallback,
		     (XtCallbackProc) dialog_callback, (XtPointer) 1);

      confirm_cancel =
	XmMessageBoxGetChild (confirm_dialog, XmDIALOG_CANCEL_BUTTON);
      confirm_ok = XmMessageBoxGetChild (confirm_dialog, XmDIALOG_OK_BUTTON);
      confirm_label =
	XmMessageBoxGetChild (confirm_dialog, XmDIALOG_MESSAGE_LABEL);
      XtUnmanageChild (XmMessageBoxGetChild
		       (confirm_dialog, XmDIALOG_HELP_BUTTON));
    }

  va_start (ap, msg);
  cancelmsg = va_arg (ap, char *);
  okmsg = va_arg (ap, char *);
  va_end (ap);

  if (!cancelmsg)
    {
      cancelmsg = "Cancel";
      okmsg = "Ok";
    }

  n = 0;
  xs = XmStringCreateLocalized (cancelmsg);

  if (okmsg)
    {
      stdarg (XmNcancelLabelString, xs);
      xs = XmStringCreateLocalized (okmsg);
      XtManageChild (confirm_cancel);
    }
  else
    XtUnmanageChild (confirm_cancel);

  stdarg (XmNokLabelString, xs);

  xs = XmStringCreateLocalized (msg);
  stdarg (XmNmessageString, xs);
  XtSetValues (confirm_dialog, args, n);

  wait_for_dialog (confirm_dialog);

  n = 0;
  stdarg (XmNdefaultPosition, False);
  XtSetValues (confirm_dialog, args, n);

  return ok;
}

static int
ConfirmAction (int argc, char **argv, int x, int y)
{
  int rv = lesstif_confirm_dialog (argc > 0 ? argv[0] : 0,
				   argc > 1 ? argv[1] : 0,
				   argc > 2 ? argv[2] : 0,
				   0);
  return rv;
}

/* ------------------------------------------------------------ */

static Widget report = 0, report_form;

void
lesstif_report_dialog (char *title, char *msg)
{
  if (!report)
    {
      if (mainwind == 0)
	return;

      n = 0;
      stdarg (XmNautoUnmanage, False);
      stdarg (XmNwidth, 600);
      stdarg (XmNheight, 200);
      stdarg (XmNtitle, title);
      report_form = XmCreateFormDialog (mainwind, "report", args, n);

      n = 0;
      stdarg (XmNeditable, False);
      stdarg (XmNeditMode, XmMULTI_LINE_EDIT);
      stdarg (XmNcursorPositionVisible, False);
      stdarg (XmNtopAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_FORM);
      report = XmCreateScrolledText (report_form, "text", args, n);
      XtManageChild (report);
    }
  n = 0;
  stdarg (XmNtitle, title);
  XtSetValues (report_form, args, n);
  XmTextSetString (report, msg);

  XtManageChild (report_form);
}

/* ------------------------------------------------------------ */

static Widget prompt_dialog = 0;
static Widget prompt_label, prompt_text;

char *
lesstif_prompt_for (char *msg, char *default_string)
{
  char *rv;
  XmString xs;
  if (prompt_dialog == 0)
    {
      n = 0;
      stdarg (XmNautoUnmanage, False);
      stdarg (XmNtitle, "PCB Prompt");
      prompt_dialog = XmCreateFormDialog (mainwind, "prompt", args, n);

      n = 0;
      stdarg (XmNtopAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNalignment, XmALIGNMENT_BEGINNING);
      prompt_label = XmCreateLabel (prompt_dialog, "label", args, n);
      XtManageChild (prompt_label);

      n = 0;
      stdarg (XmNtopAttachment, XmATTACH_WIDGET);
      stdarg (XmNtopWidget, prompt_label);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNeditable, True);
      prompt_text = XmCreateText (prompt_dialog, "text", args, n);
      XtManageChild (prompt_text);
      XtAddCallback (prompt_text, XmNactivateCallback,
		     (XtCallbackProc) dialog_callback, (XtPointer) 1);
    }
  if (!default_string)
    default_string = "";
  if (!msg)
    msg = "Enter text:";
  n = 0;
  xs = XmStringCreateLocalized (msg);
  stdarg (XmNlabelString, xs);
  XtSetValues (prompt_label, args, n);
  XmTextSetString (prompt_text, default_string);
  XmTextSetCursorPosition (prompt_text, strlen (default_string));
  wait_for_dialog (prompt_dialog);
  rv = XmTextGetString (prompt_text);
  return rv;
}

static int
PromptFor (int argc, char **argv, int x, int y)
{
  char *rv = lesstif_prompt_for (argc > 0 ? argv[0] : 0,
				 argc > 1 ? argv[1] : 0);
  printf ("rv = `%s'\n", rv);
  return 0;
}

/* ------------------------------------------------------------ */

static Widget
create_form_ok_dialog (char *name, int ok)
{
  Widget dialog, topform;
  n = 0;
  dialog = XmCreateQuestionDialog (mainwind, name, args, n);

  XtUnmanageChild (XmMessageBoxGetChild (dialog, XmDIALOG_SYMBOL_LABEL));
  XtUnmanageChild (XmMessageBoxGetChild (dialog, XmDIALOG_MESSAGE_LABEL));
  XtUnmanageChild (XmMessageBoxGetChild (dialog, XmDIALOG_HELP_BUTTON));
  XtAddCallback (dialog, XmNcancelCallback, (XtCallbackProc) dialog_callback,
		 (XtPointer) 0);
  if (ok)
    XtAddCallback (dialog, XmNokCallback, (XtCallbackProc) dialog_callback,
		   (XtPointer) 1);
  else
    XtUnmanageChild (XmMessageBoxGetChild (dialog, XmDIALOG_OK_BUTTON));

  n = 0;
  topform = XmCreateForm (dialog, "attributes", args, n);
  XtManageChild (topform);
  return topform;
}

int
lesstif_attribute_dialog (HID_Attribute * attrs,
			  int n_attrs, HID_Attr_Val * results)
{
  Widget dialog, topform, lform, form;
  Widget *wl;
  int i, rv;
  static XmString empty = 0;

  if (!empty)
    empty = XmStringCreateLocalized (" ");

  for (i = 0; i < n_attrs; i++)
    {
      results[i] = attrs[i].default_val;
      if (results[i].str_value)
	results[i].str_value = strdup (results[i].str_value);
    }

  wl = (Widget *) malloc (n_attrs * sizeof (Widget));

  topform = create_form_ok_dialog ("attributes", 1);
  dialog = XtParent (topform);

  n = 0;
  stdarg (XmNfractionBase, n_attrs);
  XtSetValues (topform, args, n);

  n = 0;
  stdarg (XmNtopAttachment, XmATTACH_FORM);
  stdarg (XmNbottomAttachment, XmATTACH_FORM);
  stdarg (XmNleftAttachment, XmATTACH_FORM);
  stdarg (XmNfractionBase, n_attrs);
  lform = XmCreateForm (topform, "attributes", args, n);
  XtManageChild (lform);

  n = 0;
  stdarg (XmNtopAttachment, XmATTACH_FORM);
  stdarg (XmNbottomAttachment, XmATTACH_FORM);
  stdarg (XmNleftAttachment, XmATTACH_WIDGET);
  stdarg (XmNleftWidget, lform);
  stdarg (XmNrightAttachment, XmATTACH_FORM);
  stdarg (XmNfractionBase, n_attrs);
  form = XmCreateForm (topform, "attributes", args, n);
  XtManageChild (form);

  for (i = 0; i < n_attrs; i++)
    {
      Widget w;

      n = 0;
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNtopAttachment, XmATTACH_POSITION);
      stdarg (XmNtopPosition, i);
      stdarg (XmNbottomAttachment, XmATTACH_POSITION);
      stdarg (XmNbottomPosition, i + 1);
      stdarg (XmNalignment, XmALIGNMENT_END);
      w = XmCreateLabel (lform, attrs[i].name, args, n);
      XtManageChild (w);
    }

  for (i = 0; i < n_attrs; i++)
    {
      static char buf[30];
      n = 0;
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNtopAttachment, XmATTACH_POSITION);
      stdarg (XmNtopPosition, i);
      stdarg (XmNbottomAttachment, XmATTACH_POSITION);
      stdarg (XmNbottomPosition, i + 1);
      stdarg (XmNalignment, XmALIGNMENT_END);

      switch (attrs[i].type)
	{
	case HID_Label:
	  stdarg (XmNlabelString, empty);
	  wl[i] = XmCreateLabel (form, attrs[i].name, args, n);
	  break;
	case HID_Boolean:
	  stdarg (XmNlabelString, empty);
	  stdarg (XmNset, results[i].int_value);
	  wl[i] = XmCreateToggleButton (form, attrs[i].name, args, n);
	  break;
	case HID_String:
	  stdarg (XmNcolumns, 40);
	  stdarg (XmNresizeWidth, True);
	  stdarg (XmNvalue, results[i].str_value);
	  wl[i] = XmCreateTextField (form, attrs[i].name, args, n);
	  break;
	case HID_Integer:
	  stdarg (XmNcolumns, 13);
	  stdarg (XmNresizeWidth, True);
	  sprintf (buf, "%d", results[i].int_value);
	  stdarg (XmNvalue, buf);
	  wl[i] = XmCreateTextField (form, attrs[i].name, args, n);
	  break;
	case HID_Real:
	  stdarg (XmNcolumns, 16);
	  stdarg (XmNresizeWidth, True);
	  sprintf (buf, "%g", results[i].real_value);
	  stdarg (XmNvalue, buf);
	  wl[i] = XmCreateTextField (form, attrs[i].name, args, n);
	  break;
	default:
	  wl[i] = XmCreateLabel (form, "UNIMPLEMENTED", args, n);
	  break;
	}

      XtManageChild (wl[i]);
    }

  rv = wait_for_dialog (dialog);

  for (i = 0; i < n_attrs; i++)
    {
      char *cp;
      switch (attrs[i].type)
	{
	case HID_Boolean:
	  results[i].int_value = XmToggleButtonGetState (wl[i]);
	  break;
	case HID_String:
	  results[i].str_value = XmTextGetString (wl[i]);
	  break;
	case HID_Integer:
	  cp = XmTextGetString (wl[i]);
	  sscanf (cp, "%d", &results[i].int_value);
	  break;
	case HID_Real:
	  cp = XmTextGetString (wl[i]);
	  sscanf (cp, "%lg", &results[i].real_value);
	  break;
	default:
	  break;
	}
    }

  free (wl);
  XtDestroyWidget (dialog);

  return rv ? 0 : 1;
}

/* ------------------------------------------------------------ */

static int
DoWindows (int argc, char **argv, int x, int y)
{
  char *a = argc == 1 ? argv[0] : "";
  if (strcmp (a, "1") == 0 || strcasecmp (a, "Layout") == 0)
    {
    }
  else if (strcmp (a, "2") == 0 || strcasecmp (a, "Library") == 0)
    {
      lesstif_show_library ();
    }
  else if (strcmp (a, "3") == 0 || strcasecmp (a, "Log") == 0)
    {
      if (log_form == 0)
	lesstif_log ("");
      XtManageChild (log_form);
    }
  else if (strcmp (a, "4") == 0 || strcasecmp (a, "Netlist") == 0)
    {
      lesstif_show_netlist ();
    }
  else
    {
      lesstif_log ("Usage: DoWindows(1|2|3|4|Layout|Library|Log|Netlist)");
      return 1;
    }
  return 0;
}

/* ------------------------------------------------------------ */
static const char about_syntax[] =
"About()";

static const char about_help[] =
"Tell the user about this version of PCB.";

/* %start-doc actions About

This just pops up a dialog telling the user which version of
@code{pcb} they're running.

%end-doc */


static int
About (int argc, char **argv, int x, int y)
{
  static Widget about = 0;
  if (!about)
    {
      static char *msg = "This is " PACKAGE " " VERSION "\n"
	"http://pcb.sourceforge.net";
      Cardinal n = 0;
      XmString xs = XmStringCreateLocalized (msg);
      stdarg (XmNmessageString, xs);
      stdarg (XmNtitle, "About PCB");
      about = XmCreateInformationDialog (mainwind, "about", args, n);
      XtUnmanageChild (XmMessageBoxGetChild (about, XmDIALOG_CANCEL_BUTTON));
      XtUnmanageChild (XmMessageBoxGetChild (about, XmDIALOG_HELP_BUTTON));
    }
  wait_for_dialog (about);
  return 0;
}

/* ------------------------------------------------------------ */

static int
Print (int argc, char **argv, int x, int y)
{
  HID_Attribute *opts;
  HID *printer;
  HID_Attr_Val *vals;
  int n;

  printer = hid_find_printer ();
  if (!printer)
    {
      lesstif_confirm_dialog ("No printer?", "Oh well", 0);
      return 1;
    }
  opts = printer->get_export_options (&n);
  vals = (HID_Attr_Val *) calloc (n, sizeof (HID_Attr_Val));
  if (lesstif_attribute_dialog (opts, n, vals))
    {
      free (vals);
      return 1;
    }
  printer->do_export (vals);
  free (vals);
  return 0;
}

static int
Export (int argc, char **argv, int x, int y)
{
  static Widget selector = 0;
  HID_Attribute *opts;
  HID *printer, **hids;
  HID_Attr_Val *vals;
  int n, i;
  Widget prev = 0;
  Widget w;

  hids = hid_enumerate ();

  if (!selector)
    {
      n = 0;
      stdarg (XmNtitle, "Export HIDs");
      selector = create_form_ok_dialog ("export", 0);
      for (i = 0; hids[i]; i++)
	{
	  if (hids[i]->exporter)
	    {
	      n = 0;
	      if (prev)
		{
		  stdarg (XmNtopAttachment, XmATTACH_WIDGET);
		  stdarg (XmNtopWidget, prev);
		}
	      else
		{
		  stdarg (XmNtopAttachment, XmATTACH_FORM);
		}
	      stdarg (XmNrightAttachment, XmATTACH_FORM);
	      stdarg (XmNleftAttachment, XmATTACH_FORM);
	      w =
		XmCreatePushButton (selector, (char *) hids[i]->name, args,
				    n);
	      XtManageChild (w);
	      XtAddCallback (w, XmNactivateCallback,
			     (XtCallbackProc) dialog_callback,
			     (XtPointer) (i + 1));
	      prev = w;
	    }
	}
      selector = XtParent (selector);
    }

  i = wait_for_dialog (selector);

  if (i <= 0)
    return 1;
  printer = hids[i - 1];

  opts = printer->get_export_options (&n);
  vals = (HID_Attr_Val *) calloc (n, sizeof (HID_Attr_Val));
  if (lesstif_attribute_dialog (opts, n, vals))
    {
      free (vals);
      return 1;
    }
  printer->do_export (vals);
  free (vals);
  return 0;
}

/* ------------------------------------------------------------ */

static Widget sizes_dialog = 0;
static Widget sz_pcb_w, sz_pcb_h, sz_bloat, sz_shrink, sz_drc_wid, sz_drc_slk,
  sz_drc_drill, sz_drc_ring;
static Widget sz_text;
static Widget sz_set, sz_reset, sz_units;

static int
sz_str2val (Widget w, int pcbu)
{
  double d;
  char *buf;
  buf = XmTextGetString (w);
  if (!pcbu)
    return atoi (buf);
  sscanf (buf, "%lf", &d);
  if (Settings.grid_units_mm)
    return MM_TO_PCB (d);
  else
    return MIL_TO_PCB (d);
}

static void
sz_val2str (Widget w, int u, int pcbu)
{
  double d;
  static char buf[40];
  if (pcbu)
    {
      if (Settings.grid_units_mm)
	d = PCB_TO_MM (u);
      else
	d = PCB_TO_MIL (u);
      sprintf (buf, "%.2f", d + 0.002);
    }
  else
    sprintf (buf, "%d %%", u);
  XmTextSetString (w, buf);
}

static void
sizes_set ()
{
  PCB->MaxWidth = sz_str2val (sz_pcb_w, 1);
  PCB->MaxHeight = sz_str2val (sz_pcb_h, 1);
  PCB->Bloat = sz_str2val (sz_bloat, 1);
  PCB->Shrink = sz_str2val (sz_shrink, 1);
  PCB->minWid = sz_str2val (sz_drc_wid, 1);
  PCB->minSlk = sz_str2val (sz_drc_slk, 1);
  PCB->minDrill = sz_str2val (sz_drc_drill, 1);
  PCB->minRing = sz_str2val (sz_drc_ring, 1);
  Settings.TextScale = sz_str2val (sz_text, 0);

  Settings.Bloat = PCB->Bloat;
  Settings.Shrink = PCB->Shrink;
  Settings.minWid = PCB->minWid;
  Settings.minSlk = PCB->minSlk;
  Settings.minDrill = PCB->minDrill;
  Settings.minRing = PCB->minRing;

  SetCrosshairRange (0, 0, PCB->MaxWidth, PCB->MaxHeight);
  lesstif_pan_fixup ();
}

void
lesstif_sizes_reset ()
{
  char *ls;
  if (!sizes_dialog)
    return;
  sz_val2str (sz_pcb_w, PCB->MaxWidth, 1);
  sz_val2str (sz_pcb_h, PCB->MaxHeight, 1);
  sz_val2str (sz_bloat, PCB->Bloat, 1);
  sz_val2str (sz_shrink, PCB->Shrink, 1);
  sz_val2str (sz_drc_wid, PCB->minWid, 1);
  sz_val2str (sz_drc_slk, PCB->minSlk, 1);
  sz_val2str (sz_drc_drill, PCB->minDrill, 1);
  sz_val2str (sz_drc_ring, PCB->minRing, 1);
  sz_val2str (sz_text, Settings.TextScale, 0);

  if (Settings.grid_units_mm)
    ls = "Units are MMs";
  else
    ls = "Units are Mils";
  n = 0;
  stdarg (XmNlabelString, XmStringCreateLocalized (ls));
  XtSetValues (sz_units, args, n);
}

static Widget
size_field (Widget parent, char *label, int posn)
{
  Widget w, l;
  n = 0;
  stdarg (XmNrightAttachment, XmATTACH_FORM);
  stdarg (XmNtopAttachment, XmATTACH_POSITION);
  stdarg (XmNtopPosition, posn);
  stdarg (XmNbottomAttachment, XmATTACH_POSITION);
  stdarg (XmNbottomPosition, posn + 1);
  stdarg (XmNcolumns, 10);
  w = XmCreateTextField (parent, "field", args, n);
  XtManageChild (w);

  n = 0;
  stdarg (XmNleftAttachment, XmATTACH_FORM);
  stdarg (XmNrightAttachment, XmATTACH_WIDGET);
  stdarg (XmNrightWidget, w);
  stdarg (XmNtopAttachment, XmATTACH_POSITION);
  stdarg (XmNtopPosition, posn);
  stdarg (XmNbottomAttachment, XmATTACH_POSITION);
  stdarg (XmNbottomPosition, posn + 1);
  stdarg (XmNlabelString, XmStringCreateLocalized (label));
  stdarg (XmNalignment, XmALIGNMENT_END);
  l = XmCreateLabel (parent, "label", args, n);
  XtManageChild (l);

  return w;
}

static int
AdjustSizes (int argc, char **argv, int x, int y)
{
  if (!sizes_dialog)
    {
      Widget inf, sep;

      n = 0;
      stdarg (XmNmarginWidth, 3);
      stdarg (XmNmarginHeight, 3);
      stdarg (XmNhorizontalSpacing, 3);
      stdarg (XmNverticalSpacing, 3);
      stdarg (XmNautoUnmanage, False);
      stdarg (XmNtitle, "Board Sizes");
      sizes_dialog = XmCreateFormDialog (mainwind, "sizes", args, n);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_FORM);
      sz_reset = XmCreatePushButton (sizes_dialog, "Reset", args, n);
      XtManageChild (sz_reset);
      XtAddCallback (sz_reset, XmNactivateCallback,
		     (XtCallbackProc) lesstif_sizes_reset, 0);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_WIDGET);
      stdarg (XmNrightWidget, sz_reset);
      stdarg (XmNbottomAttachment, XmATTACH_FORM);
      sz_set = XmCreatePushButton (sizes_dialog, "Set", args, n);
      XtManageChild (sz_set);
      XtAddCallback (sz_set, XmNactivateCallback, (XtCallbackProc) sizes_set,
		     0);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNbottomWidget, sz_reset);
      sep = XmCreateSeparator (sizes_dialog, "sep", args, n);
      XtManageChild (sep);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNbottomWidget, sep);
      sz_units = XmCreateLabel (sizes_dialog, "units", args, n);
      XtManageChild (sz_units);

      n = 0;
      stdarg (XmNrightAttachment, XmATTACH_FORM);
      stdarg (XmNleftAttachment, XmATTACH_FORM);
      stdarg (XmNtopAttachment, XmATTACH_FORM);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNbottomWidget, sz_units);
      stdarg (XmNfractionBase, 9);
      inf = XmCreateForm (sizes_dialog, "sizes", args, n);
      XtManageChild (inf);

      sz_pcb_w = size_field (inf, "PCB Width", 0);
      sz_pcb_h = size_field (inf, "PCB Height", 1);
      sz_bloat = size_field (inf, "Bloat", 2);
      sz_shrink = size_field (inf, "Shrink", 3);
      sz_drc_wid = size_field (inf, "DRC Min Wid", 4);
      sz_drc_slk = size_field (inf, "DRC Min Silk", 5);
      sz_drc_drill = size_field (inf, "DRC Min Drill", 6);
      sz_drc_ring = size_field (inf, "DRC Min Annular Ring", 7);
      sz_text = size_field (inf, "Text Scale", 8);
    }
  lesstif_sizes_reset ();
  XtManageChild (sizes_dialog);
  return 0;
}

/* ------------------------------------------------------------ */

static Widget layer_groups_form = 0;

static Widget lglabels[MAX_LAYER + 2];
static Widget lgbuttons[MAX_LAYER + 2][MAX_LAYER];

static void
lgbutton_cb (Widget w, int ij, void *cbs)
{
  int layer, group, k;

  layer = ij / MAX_LAYER;
  group = ij % MAX_LAYER;
  group = MoveLayerToGroup (layer, group);
  for (k = 0; k < MAX_LAYER; k++)
    {
      if (k == group)
	XmToggleButtonSetState (lgbuttons[layer][k], 1, 0);
      else
	XmToggleButtonSetState (lgbuttons[layer][k], 0, 0);
    }
}

void
lesstif_update_layer_groups ()
{
  int sets[MAX_LAYER + 2][MAX_LAYER];
  int i, j, n;
  LayerGroupType *l = &(PCB->LayerGroups);

  memset (sets, 0, sizeof (sets));

  for (i = 0; i < MAX_LAYER; i++)
    for (j = 0; j < l->Number[i]; j++)
      sets[l->Entries[i][j]][i] = 1;

  for (i = 0; i < MAX_LAYER + 2; i++)
    {
      char *name = "unknown";
      n = 0;
      if (i < MAX_LAYER)
	name = PCB->Data->Layer[i].Name;
      else if (i == MAX_LAYER)
	name = "solder";
      else if (i == MAX_LAYER + 1)
	name = "component";
      stdarg (XmNlabelString, XmStringCreateLocalized (name));
      XtSetValues (lglabels[i], args, n);
      for (j = 0; j < MAX_LAYER; j++)
	{
	  if (sets[i][j] != XmToggleButtonGetState (lgbuttons[i][j]))
	    {
	      XmToggleButtonSetState (lgbuttons[i][j], sets[i][j], 0);
	    }
	}
    }
}

static int
EditLayerGroups (int argc, char **argv, int x, int y)
{
  if (!layer_groups_form)
    {
      int i, j;
      Widget buttonform;

      n = 0;
      stdarg (XmNfractionBase, MAX_LAYER + 2);
      stdarg (XmNtitle, "Layer Groups");
      layer_groups_form = XmCreateFormDialog (mainwind, "layers", args, n);

      n = 0;
      stdarg (XmNtopAttachment, XmATTACH_WIDGET);
      stdarg (XmNbottomAttachment, XmATTACH_WIDGET);
      stdarg (XmNrightAttachment, XmATTACH_WIDGET);
      stdarg (XmNfractionBase, MAX_LAYER * (MAX_LAYER + 2));
      buttonform = XmCreateForm (layer_groups_form, "layers", args, n);
      XtManageChild (buttonform);

      for (i = 0; i < MAX_LAYER + 2; i++)
	{
	  n = 0;
	  stdarg (XmNleftAttachment, XmATTACH_FORM);
	  stdarg (XmNtopAttachment, XmATTACH_POSITION);
	  stdarg (XmNtopPosition, i);
	  stdarg (XmNbottomAttachment, XmATTACH_POSITION);
	  stdarg (XmNbottomPosition, i + 1);
	  stdarg (XmNrightAttachment, XmATTACH_WIDGET);
	  stdarg (XmNrightWidget, buttonform);
	  lglabels[i] = XmCreateLabel (layer_groups_form, "layer", args, n);
	  XtManageChild (lglabels[i]);

	  for (j = 0; j < MAX_LAYER; j++)
	    {
	      n = 0;
	      stdarg (XmNleftAttachment, XmATTACH_POSITION);
	      stdarg (XmNleftPosition, j * (MAX_LAYER + 2));
	      stdarg (XmNrightAttachment, XmATTACH_POSITION);
	      stdarg (XmNrightPosition, (j + 1) * (MAX_LAYER + 2));
	      stdarg (XmNtopAttachment, XmATTACH_POSITION);
	      stdarg (XmNtopPosition, i * MAX_LAYER);
	      stdarg (XmNbottomAttachment, XmATTACH_POSITION);
	      stdarg (XmNbottomPosition, (i + 1) * MAX_LAYER);
	      stdarg (XmNlabelString, XmStringCreateLocalized (" "));
	      stdarg (XmNspacing, 0);
	      stdarg (XmNvisibleWhenOff, True);
	      stdarg (XmNfillOnSelect, True);
	      stdarg (XmNshadowThickness, 0);
	      stdarg (XmNmarginWidth, 0);
	      stdarg (XmNmarginHeight, 0);
	      stdarg (XmNhighlightThickness, 0);
	      lgbuttons[i][j] =
		XmCreateToggleButton (buttonform, "label", args, n);
	      XtManageChild (lgbuttons[i][j]);

	      XtAddCallback (lgbuttons[i][j], XmNvalueChangedCallback,
			     (XtCallbackProc) lgbutton_cb,
			     (XtPointer) (i * MAX_LAYER + j));
	    }
	}
    }
  lesstif_update_layer_groups ();
  XtManageChild (layer_groups_form);
  return 1;
}

/* ------------------------------------------------------------ */

HID_Action lesstif_dialog_action_list[] = {
  {"Load", 0, Load},
  {"LoadVendor", 0, LoadVendor},
  {"Save", 0, Save},
  {"DoWindows", 0, DoWindows},
  {"PromptFor", 0, PromptFor},
  {"Confirm", 0, ConfirmAction},
  {"About", 0, About,
   about_help, about_syntax},
  {"Print", 0, Print},
  {"Export", 0, Export},
  {"AdjustSizes", 0, AdjustSizes},
  {"EditLayerGroups", 0, EditLayerGroups},
};

REGISTER_ACTIONS (lesstif_dialog_action_list)
