#!/usr/bin/awk -f
#
# $Id: gen_geda_list.awk,v 1.1 2003-08-30 00:11:14 danmc Exp $
#
# Script to regenerate geda.list from geda.inc
#
# Usage:
#
#  awk -f gen_geda_list.awk geda.inc > geda.list
#

BEGIN {
}

/^[ \t]*\#[ \t]*\$Id: gen_geda_list.awk,v 1.1 2003-08-30 00:11:14 danmc Exp $]*\$/ {
	id = substr($0, index($0, "Id:"));
	ind = index(id, "Exp $");
	id = substr(id, 1, ind+3);
	printf("#\n");
	printf("# NOTE: Auto-generated. Do not change.\n");
	printf("# Generated from:\n");
	printf("#  %s\n", id);
	next;
}

/^\#\#/ {
	printf("#\n");
	next;
}


/^[ \t]*define/ {
	pkg = $1;
	ind = index(pkg, "PKG");
	pkg = substr(pkg, ind+4);
	ind = index(pkg, "'");
	pkg = substr(pkg, 1, ind-1);
	printf("geda_%s:%s:%s\n", pkg, pkg, pkg);
}

