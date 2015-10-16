/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* TODO:
   - import spy_linejust(), spy_lineformat(), and spy_fprintpar()
*/

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "text.h"

char *text_symbolname(t_symbol *s, char *nulname)
{
    if (s && s != &s_) return (s->s_name);
    else return (nulname);
}

char *text_ordinal(int n)
{
    static char buf[16];  /* assuming 10-digit INT_MAX */
    sprintf(buf, "%dth", n);
    if (n < 0) n = -n;
    n %= 100;
    if (n > 20) n %= 10;
    if (n && n <= 3)
    {
	char *ptr = buf + strlen(buf) - 2;
	switch (n)
	{
	case 1: strcpy(ptr, "st"); break;
	case 2: strcpy(ptr, "nd"); break;
	case 3: strcpy(ptr, "rd"); break;
	}
    }
    return (buf);
}
