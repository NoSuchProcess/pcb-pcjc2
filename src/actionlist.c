/* This file is generated by gather-actions.  DO NOT EDIT.  */

#include "global.h"
#include "X11/Intrinsic.h"

extern void AboutDialog(Widget,XEvent *,String *, Cardinal *);
extern void ActionActiveWhen(Widget,XEvent *,String *, Cardinal *);
extern void ActionCheckWhen(Widget,XEvent *,String *, Cardinal *);
extern void ActionCommandHistory(Widget,XEvent *,String *, Cardinal *);
extern void ActionDoWindows(Widget,XEvent *,String *, Cardinal *);
extern void ActionExecuteFile(Widget,XEvent *,String *, Cardinal *);
extern void ActionGetLocation(Widget,XEvent *,String *, Cardinal *);
extern void ActionSizesLabel(Widget,XEvent *,String *, Cardinal *);
extern void djopt_set_auto_only(Widget,XEvent *,String *, Cardinal *);

XtActionsRec ActionList[] = {
  {"About", AboutDialog},
  {"ActiveWhen", ActionActiveWhen},
  {"CheckWhen", ActionCheckWhen},
  {"CommandHistory", ActionCommandHistory},
  {"DoWindows", ActionDoWindows},
  {"ExecuteFile", ActionExecuteFile},
  {"GetXY", ActionGetLocation},
  {"OptAutoOnly", djopt_set_auto_only},
  {"SizesLabel", ActionSizesLabel},
  {0,0}
};

struct { char *name; int type; } ActionTypeList[] = {
  {"About", 0},
  {"ActiveWhen", 'p'},
  {"CheckWhen", 'p'},
  {"CommandHistory", 0},
  {"DoWindows", 0},
  {"ExecuteFile", 0},
  {"GetXY", 0},
  {"OptAutoOnly", 0},
  {"SizesLabel", 'p'},
  {0,0}
};

int ActionListSize = 9;
extern int FlagCurrentStyle(int);
extern int FlagElementName(int);
extern int FlagGrid(int);
extern int FlagGridFactor(int);
extern int FlagIsDataEmpty(int);
extern int FlagIsDataEmpty(int);
extern int FlagNetlist(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagSETTINGS(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagTESTFLAG(int);
extern int FlagZoom(int);
extern int djopt_get_auto_only(int);

struct {
  char *name;
  int (*func)(int);
  int parm;
} FlagFuncList[] = {
  {"DataEmpty", FlagIsDataEmpty, 0 },
  {"DataNonEmpty", FlagIsDataEmpty, 1 },
  {"OptAutoOnly", djopt_get_auto_only, 0 },
  {"alldirection", FlagTESTFLAG, ALLDIRECTIONFLAG },
  {"alldirectionlines", FlagSETTINGS, XtOffsetOf(SettingType,AllDirectionLines) },
  {"autodrc", FlagTESTFLAG, AUTODRCFLAG },
  {"checkplanes", FlagTESTFLAG, CHECKPLANESFLAG },
  {"clearline", FlagSETTINGS, XtOffsetOf(SettingType,ClearLine) },
  {"clearnew", FlagTESTFLAG, CLEARNEWFLAG },
  {"description", FlagTESTFLAG, DESCRIPTIONFLAG },
  {"drawgrid", FlagSETTINGS, XtOffsetOf(SettingType,DrawGrid) },
  {"elementname", FlagElementName, 0 },
  {"grid", FlagGrid, 0 },
  {"gridfactor", FlagGridFactor, 0 },
  {"liveroute", FlagSETTINGS, XtOffsetOf(SettingType,liveRouting) },
  {"liveroute", FlagTESTFLAG, LIVEROUTEFLAG },
  {"localref", FlagTESTFLAG, LOCALREFFLAG },
  {"nameonpcb", FlagTESTFLAG, NAMEONPCBFLAG },
  {"netlist", FlagNetlist, 0 },
  {"orthomove", FlagTESTFLAG, ORTHOMOVEFLAG },
  {"raiselogwindow", FlagSETTINGS, XtOffsetOf(SettingType,RaiseLogWindow) },
  {"ratwarn", FlagSETTINGS, XtOffsetOf(SettingType,RatWarn) },
  {"resetafterelement", FlagSETTINGS, XtOffsetOf(SettingType,ResetAfterElement) },
  {"ringbellwhenfinished", FlagSETTINGS, XtOffsetOf(SettingType,RingBellWhenFinished) },
  {"rubberband", FlagTESTFLAG, RUBBERBANDFLAG },
  {"rubberbandmode", FlagSETTINGS, XtOffsetOf(SettingType,RubberBandMode) },
  {"saveintmp", FlagSETTINGS, XtOffsetOf(SettingType,SaveInTMP) },
  {"savelastcommand", FlagSETTINGS, XtOffsetOf(SettingType,SaveLastCommand) },
  {"showdrc", FlagSETTINGS, XtOffsetOf(SettingType,ShowDRC) },
  {"showdrc", FlagTESTFLAG, SHOWDRCFLAG },
  {"showmask", FlagTESTFLAG, SHOWMASKFLAG },
  {"shownumber", FlagTESTFLAG, SHOWNUMBERFLAG },
  {"showsolderside", FlagSETTINGS, XtOffsetOf(SettingType,ShowSolderSide) },
  {"snappin", FlagTESTFLAG, SNAPPINFLAG },
  {"stipplepolygons", FlagSETTINGS, XtOffsetOf(SettingType,StipplePolygons) },
  {"style", FlagCurrentStyle, 0 },
  {"swapstartdir", FlagTESTFLAG, SWAPSTARTDIRFLAG },
  {"swapstartdirection", FlagSETTINGS, XtOffsetOf(SettingType,SwapStartDirection) },
  {"thindraw", FlagTESTFLAG, THINDRAWFLAG },
  {"uniquename", FlagTESTFLAG, UNIQUENAMEFLAG },
  {"uniquenames", FlagSETTINGS, XtOffsetOf(SettingType,UniqueNames) },
  {"uselogwindow", FlagSETTINGS, XtOffsetOf(SettingType,UseLogWindow) },
  {"zoom", FlagZoom, 0 },
  {0,0,0}
};

int FlagFuncListSize = 43;
struct Resource;
extern void SizesMenuInclude(struct Resource *);

struct {
  char *name;
  void (*func)(struct Resource *);
} MenuFuncList[] = {
  {"sizes", SizesMenuInclude },
  {0,0}
};

int MenuFuncListSize = 1;
