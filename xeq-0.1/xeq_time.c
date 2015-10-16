/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* TODO:
   - real time reporting (relative to start of sequence)
   - user time reporting (tempo handling)
   - mode setting methods:  `logical', `real', `user'
   - event index:  use separate outlet or a default switchable to atom index
*/

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

typedef struct _xeq_time
{
    t_hyphen   x_this;
    t_outlet  *x_lastout;
    t_outlet  *x_nextout;
    t_outlet  *x_indxout;
} t_xeq_time;

static t_class *xeq_time_class;

static void *xeq_time_new(t_symbol *name)
{
    t_xeq_time *x = (t_xeq_time *)hyphen_new(xeq_time_class, "xeq");
    hyphen_attach((t_hyphen *)x, name);
    outlet_new((t_object *)x, &s_float);
    x->x_lastout = outlet_new((t_object *)x, &s_float);
    x->x_nextout = outlet_new((t_object *)x, &s_float);
    x->x_indxout = outlet_new((t_object *)x, &s_float);
    return (x);
}

static void xeq_time_free(t_xeq_time *x)
{
    hyphen_detach((t_hyphen *)x);
}

static void xeq_time_host(t_xeq_time *x, t_symbol *name)
{
    hyphen_attach((t_hyphen *)x, name);
}

static void xeq_time_bang(t_xeq_time *x)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	t_xeqit *hostit = &host->x_autoit;
	float lasttime = hostit->i_playloc.l_when;
	float nexttime = lasttime + hostit->i_playloc.l_delay;
	float curtime = lasttime;
	outlet_float(x->x_indxout, hostit->i_playloc.l_atnext);
	outlet_float(x->x_nextout, nexttime);
	outlet_float(x->x_lastout, lasttime);
	if (host->x_whenclockset != 0)
	{
	     curtime +=
		 clock_gettimesince(host->x_whenclockset) * host->x_tempo;
	     if (curtime > nexttime) curtime = nexttime;
	    
	}
	outlet_float(((t_object *)x)->ob_outlet, curtime);
    }
}

static void xeq_time_index(t_xeq_time *x, t_floatarg f)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	int ndx = (int)f;
	t_xeqlocator *loc = &host->x_beditloc;
	xeqlocator_settoindex(loc, ndx);
	outlet_float(((t_object *)x)->ob_outlet, loc->l_when);
    }
}

void xeq_time_dosetup(void)
{
    xeq_time_class = class_new(gensym("xeq_time"), (t_newmethod)xeq_time_new,
			       (t_method)xeq_time_free, sizeof(t_xeq_time),
			       0, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_time_new,
		     gensym("xeq-time"), A_DEFSYM, 0);
    class_addmethod(xeq_time_class, (t_method)xeq_time_host,
		    gensym("host"), A_DEFSYM, 0);
    class_addbang(xeq_time_class, xeq_time_bang);
    class_addmethod(xeq_time_class, (t_method)xeq_time_index,
		    gensym("index"), A_DEFFLOAT, 0);
}

void xeq_time_setup(void)
{
    xeq_setup();
}
