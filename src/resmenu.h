/* $Id: resmenu.h,v 1.5 2005-01-25 18:07:54 djdelorie Exp $ */

#ifndef __RESMENU_INCLUDED_
#define __RESMENU_INCLUDED_ 1

#include <stdio.h>

#include "resource.h"

#ifdef __cplusplus
extern "C" {
#endif

Widget MenuCreateFromResource(Widget menu, Resource *res,
			      Widget top, Widget left, int chain);

Widget MenuCreatePopup(Widget menu, Resource *res);

void MenuSetFlag(char *flag, int value);

void MenuSetAccelerators(Widget w);

#ifdef __cplusplus
}
#endif

#endif
