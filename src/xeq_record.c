/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* sequence recorder (a host) */

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

typedef struct _xeq_record
{
    t_hyphen   x_this;  /* (using outlet pointers of the base) */
    t_symbol  *x_trackname;
    double     x_prevtime;
} t_xeq_record;

static t_class *xeq_record_class;

/* CREATION/DESTRUCTION */

static void *xeq_record_new(t_symbol *name)
{
    t_xeq_record *x =
	(t_xeq_record *)xeq_derived_new(xeq_record_class, 1, &s_, name,
					(t_method)xeq_tick);
    if (x)
    {
	xeq_derived_clone((t_hyphen *)x);
	x->x_trackname = gensym("Track-1");
	x->x_prevtime = 0;
    }
    return (x);
}

static void xeq_record_free(t_xeq_record *x)
{
    xeq_derived_free((t_hyphen *)x);
}

static void xeq_record_record(t_xeq_record *x)
{
    t_xeq *base = XEQ_BASE(x);
    xeq_rewind(base);
    binbuf_clear(base->x_binbuf);
    x->x_prevtime = clock_getsystime();
}

static void xeq_record_restop(t_xeq_record *x)
{
    x->x_prevtime = 0;
}

static void xeq_record_retrack(t_xeq_record *x, t_symbol *s)
{
    if (s && s != &s_) x->x_trackname = s;
}

static void xeq_record_readd(t_xeq_record *x, t_symbol *s, int ac, t_atom *av)
{
    if (x->x_prevtime != 0)
    {
	t_binbuf *bb = XEQ_BASE(x)->x_binbuf;
	t_atom at[2];
    	float elapsed = clock_gettimesince(x->x_prevtime);
	SETFLOAT(&at[0], elapsed);
	SETSYMBOL(&at[1], x->x_trackname);
	binbuf_add(bb, 2, at);
	binbuf_add(bb, ac, av);
	SETSEMI(&at[0]);
	binbuf_add(bb, 1, at);
	x->x_prevtime = clock_getsystime();
    }
}

void xeq_record_dosetup(void)
{
    xeq_record_class = class_new(gensym("xeq_record"),
				 (t_newmethod)xeq_record_new,
				 (t_method)xeq_record_free,
				 sizeof(t_xeq_record),
				 0, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_record_new,
		     gensym("xeq-record"), A_DEFSYM, 0);

    class_addanything(xeq_record_class, xeq_host_anything);

    class_addmethod(xeq_record_class, (t_method)xeq_record_record,
		    gensym("record"), 0);
    class_addmethod(xeq_record_class, (t_method)xeq_record_restop,
		    gensym("restop"), 0);
    class_addmethod(xeq_record_class, (t_method)xeq_record_retrack,
		    gensym("retrack"), A_DEFSYM, 0);
    class_addmethod(xeq_record_class, (t_method)xeq_record_readd,
		    gensym("readd"), A_GIMME, 0);
}

void xeq_record_setup(void)
{
    xeq_setup();
}
