/* $Id: lesstif.h,v 1.7 2006-10-10 10:49:12 danmc Exp $ */

#define app_context lesstif_app_context
#define appwidget lesstif_appwidget
#define display lesstif_display
#define screen_s lesstif_screen_s
#define screen lesstif_screen
#define mainwind lesstif_mainwind
#define work_area lesstif_work_area
#define messages lesstif_messages
#define command lesstif_command
#define hscroll lesstif_hscroll
#define vscroll lesstif_vscroll
#define m_click lesstif_message_click

extern XtAppContext app_context;
extern Widget appwidget;
extern Display *display;
extern Screen *screen_s;
extern int screen;

Widget mainwind, work_area, command, hscroll, vscroll;
Widget m_click;

#define M_Shift 1
#define M_Ctrl 2
#define M_Alt 4
#define M_Multi 8

extern Widget lesstif_menu (Widget, char *, Arg *, int);
extern int lesstif_key_event (XKeyEvent *);
extern int lesstif_button_event (Widget w, XEvent * e);

/* Returns TRUE if the point mapped to the PCB region, FALSE (=0) if
   we're off-board.  Note that *pcbxy is always written to, even if
   out of range.  */
extern int lesstif_winxy_to_pcbxy (int winx, int winy, int *pcbx, int *pcby);

/* Returns TRUE if the point is in the window, FALSE (=0) otherwise. */
extern int lesstif_pcbxy_to_winxy (int pcbx, int pcby, int *winx, int *winy);

extern void lesstif_need_idle_proc (void);
extern void lesstif_show_crosshair (int);
extern void lesstif_invalidate_all (void);
extern void lesstif_coords_to_pcb (int, int, int *, int *);
extern void lesstif_get_xy (const char *msg);
extern void lesstif_update_widget_flags (void);
extern int lesstif_call_action (const char *, int, char **);
extern void lesstif_sizes_reset (void);
extern void lesstif_pan_fixup (void);
extern void lesstif_show_library (void);
extern void lesstif_show_netlist (void);
extern Pixel lesstif_parse_color (char *value);
extern void lesstif_insert_style_buttons (Widget menu);
extern void lesstif_styles_update_values ();
extern void lesstif_update_layer_groups ();
extern void lesstif_update_status_line ();
extern char *lesstif_prompt_for (char *, char *);
extern void lesstif_log (const char *fmt, ...);
#ifdef __RESOURCE_INCLUDED__
extern void lesstif_note_mouse_resource(Resource *res);
#endif

#define need_idle_proc lesstif_need_idle_proc
#define show_crosshair lesstif_show_crosshair

#define PCB_TO_MIL(n) ((n) / 100.0)
#define PCB_TO_MM(n) ((n) / 100000.0 * 25.4)
#define MIL_TO_PCB(n) (int)((n) * 100.0 + 0.2)
#define MM_TO_PCB(n) (int)((n) / 25.4 * 100000.0 + 0.2)
