/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

#define XEQ_FOLLOW_MAXAHEAD_DEFAULT  3

typedef struct _xeq_follow
{
    t_hyphen    x_this;
    t_outlet   *x_flagout;  /* 1 if matched note, 0 if skipnote */
    t_outlet   *x_missout;  /* list: 1st interval, best interval, best ndx */
    t_outlet   *x_bangout;  /* end of score */
    int         x_pitch;
    int         x_aheadmax;
    int         x_aheadndx;
    int        *x_aheadwhat;
    int        *x_aheadatom;
} t_xeq_follow;

static t_class *xeq_follow_class;

/* SEQUENCE TRAVERSING HOOKS */

static void xeqithook_follow_autodelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    clock_delay(base->x_clock,
		base->x_clockdelay = it->i_playloc.l_delay * base->x_tempo);
    base->x_whenclockset = clock_getsystime();
}

static void xeqithook_follow_stepdelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_follow *x = (t_xeq_follow *)((t_hyphen *)base)->x_self;
    outlet_list(((t_object *)x)->ob_outlet, 0, argc, argv);
}

static void xeqithook_follow_playmessage(t_xeqit *it, t_symbol *target,
					 int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_follow *x = (t_xeq_follow *)((t_hyphen *)base)->x_self;
    t_pd *dest;
    if (it->i_status)
    {
	if (it->i_status == 144 && it->i_data2)
	    outlet_float(x->x_flagout, it->i_data1);
    }
    else if (dest = target->s_thing)
    {
	if (argv->a_type == A_FLOAT)
	    typedmess(dest, &s_list, argc, argv);
	else if (argv->a_type == A_SYMBOL && argc)
	    typedmess(dest, argv->a_w.w_symbol, argc-1, argv+1);
    }
}

static void xeqithook_follow_stepmessage(t_xeqit *it, t_symbol *target,
					 int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_follow *x = (t_xeq_follow *)((t_hyphen *)base)->x_self;
    t_pd *dest;
    if (it->i_status)
    {
	if (it->i_status == 144 && it->i_data2)
	    outlet_float(x->x_flagout, it->i_data1 == x->x_pitch ? 1 : 0);
    }
    else if (dest = target->s_thing)
    {
	if (argv->a_type == A_FLOAT)
	    typedmess(dest, &s_list, argc, argv);
	else if (argv->a_type == A_SYMBOL && argc)
	    typedmess(dest, argv->a_w.w_symbol, argc-1, argv+1);
    }
}

static void xeqithook_follow_finish(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_follow *x = (t_xeq_follow *)((t_hyphen *)base)->x_self;
    outlet_bang(x->x_bangout);
}

static void xeqithook_ahead_delay(t_xeqit *it, int argc, t_atom *argv)
{
}

static void xeqithook_ahead_message(t_xeqit *it, t_symbol *target,
				    int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_follow *x = (t_xeq_follow *)((t_hyphen *)base)->x_self;
    if (it->i_status == 144 && it->i_data2)
    {
#if 1
	post("ahead %d: pitch %d atom %d onset %f", x->x_aheadndx,
	     it->i_data1, it->i_playloc.l_atnext, it->i_playloc.l_when);
#endif
	x->x_aheadwhat[x->x_aheadndx] = it->i_data1;
	x->x_aheadatom[x->x_aheadndx] = it->i_playloc.l_atnext;
    }
}

static void xeqithook_ahead_finish(t_xeqit *it)
{
}

/* CLOCK HANDLER */

static void xeq_follow_tick(t_xeq *base)
{
    base->x_whenclockset = 0;
    xeqit_donext(&base->x_autoit);
}

/* HELPERS */

static void xeq_follow_reset(t_xeq_follow *x, int aheadmax)
{
    int i;
    xeqit_sethooks(&XEQ_BASE(x)->x_walkit, xeqithook_ahead_delay, 0,
		   xeqithook_ahead_message, xeqithook_ahead_finish, 0);
    xeqit_rewind(&XEQ_BASE(x)->x_walkit);

    if (x->x_aheadmax <= 0)
    {
	x->x_aheadmax = aheadmax > 0 ? aheadmax : XEQ_FOLLOW_MAXAHEAD_DEFAULT;
	x->x_aheadwhat = getbytes(x->x_aheadmax * sizeof(*x->x_aheadwhat));
	x->x_aheadatom = getbytes(x->x_aheadmax * sizeof(*x->x_aheadatom));
    }
    else if (aheadmax > 0 && aheadmax != x->x_aheadmax)
    {
	x->x_aheadwhat = resizebytes(x->x_aheadwhat,
				     x->x_aheadmax * sizeof(*x->x_aheadwhat),
				     aheadmax * sizeof(*x->x_aheadwhat));
	x->x_aheadatom = resizebytes(x->x_aheadatom,
				     x->x_aheadmax * sizeof(*x->x_aheadatom),
				     aheadmax * sizeof(*x->x_aheadatom));
	x->x_aheadmax = aheadmax;
    }
    for (i = 0; i < x->x_aheadmax; i++)
	x->x_aheadwhat[i] = -1;
}

/* CREATION/DESTRUCTION */

static void *xeq_follow_new(t_symbol *seqname, t_symbol *refname)
{
    t_xeq_follow *x =
	(t_xeq_follow *)xeq_derived_new(xeq_follow_class, 1, seqname, refname,
					(t_method)xeq_follow_tick);
    if (!x) return (0);

    /* fill in our `vtbl' */
    xeqit_sethooks(&XEQ_BASE(x)->x_autoit, xeqithook_follow_autodelay, 0,
		   xeqithook_follow_playmessage, xeqithook_follow_finish, 0);
    xeqit_sethooks(&XEQ_BASE(x)->x_stepit, xeqithook_follow_stepdelay, 0,
		   xeqithook_follow_stepmessage, xeqithook_follow_finish, 0);

    outlet_new((t_object *)x, &s_list);
    x->x_flagout = outlet_new((t_object *)x, &s_float);
    x->x_missout = outlet_new((t_object *)x, &s_list);
    x->x_bangout = outlet_new((t_object *)x, &s_bang);

    x->x_aheadmax = 0;
    x->x_aheadwhat = 0;
    x->x_aheadatom = 0;

    xeq_follow_reset(x, XEQ_FOLLOW_MAXAHEAD_DEFAULT);
    x->x_pitch = -1;
    return (x);
}

static void xeq_follow_free(t_xeq_follow *x)
{
    xeq_derived_free((t_hyphen *)x);
    freebytes(x->x_aheadwhat, x->x_aheadmax * sizeof(*x->x_aheadwhat));
    freebytes(x->x_aheadatom, x->x_aheadmax * sizeof(*x->x_aheadatom));
}

static void xeq_follow_host(t_xeq_follow *x, t_symbol *seqname)
{
    xeq_derived_reembed((t_hyphen *)x, seqname);
}

/* PLAYBACK PARAMETERS METHODS */

static void xeq_follow_tracks(t_xeq_follow *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_tracks(XEQ_BASE(x), s, ac, av);
}

static void xeq_follow_transpo(t_xeq_follow *x, t_floatarg f)
{
    xeq_transpo(XEQ_BASE(x), f);
}

static void xeq_follow_tempo(t_xeq_follow *x, t_floatarg f)
{
    xeq_tempo(XEQ_BASE(x), f);
}

/* PLAYBACK CONTROL METHODS */

static void xeq_follow_rewind(t_xeq_follow *x)
{
    xeq_rewind(XEQ_BASE(x));
}

static void xeq_follow_stop(t_xeq_follow *x)
{
    xeq_stop(XEQ_BASE(x));
}

static void xeq_follow_next(t_xeq_follow *x, t_floatarg drop)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	t_xeqit *it = &base->x_stepit;
	xeqit_sethooks(it, xeqithook_follow_stepdelay, 0,
		       drop ? 0 : xeqithook_follow_stepmessage,
		       xeqithook_follow_finish, 0);
	xeqit_donext(it);
    }
}

static void xeq_follow_nextnote(t_xeq_follow *x, t_floatarg drop)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	t_xeqit *it = &base->x_stepit;
	xeqit_sethooks(it, xeqithook_follow_stepdelay, 0,
		       drop ? 0 : xeqithook_follow_stepmessage,
		       xeqithook_follow_finish, 0);
	while (!it->i_finish)
	{
	    xeqit_donext(it);
	    if (it->i_status == 144 && it->i_data2) break;
	}
    }
}

static void xeq_follow_bang(t_xeq_follow *x)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	xeqit_sethooks(&base->x_autoit, xeqithook_follow_autodelay, 0,
		       xeqithook_follow_playmessage,
		       xeqithook_follow_finish, 0);
	xeq_start(base);
    }
}

static void xeq_follow_lookahead(t_xeq_follow *x)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	t_xeqit *stepit = &base->x_stepit;
	t_xeqit *aheadit = &base->x_walkit;
	xeqit_settoit(aheadit, stepit);
	for (x->x_aheadndx = 0; x->x_aheadndx < x->x_aheadmax; x->x_aheadndx++)
	{
	    while (!aheadit->i_finish)
	    {
		xeqit_donext(aheadit);
		if (x->x_aheadwhat[x->x_aheadndx] >= 0) break;
	    }
	    if (aheadit->i_finish) break;
	}
    }	
}

static void xeq_follow_follow(t_xeq_follow *x, t_floatarg f)
{
    int i = f > 0 ? (int)f : 0;
    xeq_follow_reset(x, i);
    xeq_follow_lookahead(x);
}

/* Here we have two reentrancy troubles.  One is that since donext flagouts
   may be used as cues for relocating/refollowing the sequence, intermediate
   nonhits will mess things up.  The other is more subtle: if xeqit_donext()
   pushes its playhead _after_ calling messagehook, any attempt to relocate
   from within messagehook will fail anyway, and we will get a corrupt
   locator -- ndx will bypass relocation, while other fields stay relocated.
   But pushing playhead _before_ calling messagehook does not help, because
   donext will then loop over with relocated locator.

   One solution is to set it->i_relocated (or loc->l_locked) flag in any of
   the methods causing relocation, check this flag immediately upon return
   from messagehook, and if set, return from donext (without pushing the
   playhead).

   Second solution is to queue relocation requests.

   Third solution is to design a locking scheme over locators.

   Fourth solution is to lock locator before calling messagehook, and simply
   ban relocation of locked locators.
*/
static void xeq_follow_float(t_xeq_follow *x, t_floatarg f)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	t_xeqit *it = &base->x_stepit;
	t_atom at[3];
	int currint, bestint = 0x7fffffff, bestndx = 0;
	int i;
	if ((x->x_pitch = (int)f) >= 0)
	{
	    for (i = 0; i < x->x_aheadmax; i++)
	    {
		if ((currint = x->x_pitch - x->x_aheadwhat[i]) == 0)
		{
		    while (it->i_playloc.l_atnext <= x->x_aheadatom[i])
			xeqit_donext(it);
		    xeq_follow_follow(x, 0);
		    x->x_pitch = -1;
		    return;
		}
		if (abs(currint) < abs(bestint))
		    bestint = currint, bestndx = i;
	    }
	    /* miss */
	    SETFLOAT(&at[0], x->x_pitch - x->x_aheadwhat[0]);
	    SETFLOAT(&at[1], bestint);
	    SETFLOAT(&at[2], bestndx);
	    outlet_list(x->x_missout, 0, 3, at);
	}
    }
    x->x_pitch = -1;
}

static void xeq_follow_locate(t_xeq_follow *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_locate(XEQ_BASE(x), s, ac, av);
}

static void xeq_follow_find(t_xeq_follow *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_find(XEQ_BASE(x), s, ac, av);
}

static void *xeq_follow_newhost(t_symbol *s, int ac, t_atom *av)
{
    if (!ac) s = &s_;
    else if (av->a_type == A_SYMBOL) s = av->a_w.w_symbol;
    else return (0);
    return (xeq_follow_new(&s_, s));
}

void xeq_follow_dosetup(void)
{
    xeq_follow_class = class_new(gensym("xeq_follow"),
				 (t_newmethod)xeq_follow_new,
				 (t_method)xeq_follow_free,
				 sizeof(t_xeq_follow),
				 0, A_DEFSYM, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_follow_new,
		     gensym("xeq-follow"), A_DEFSYM, A_DEFSYM, 0);

    xeq_host_enable(xeq_follow_class, (t_newmethod)xeq_follow_newhost);

    class_addmethod(xeq_follow_class, (t_method)xeq_follow_host,
		    gensym("host"), A_DEFSYM, 0);

    class_addmethod(xeq_follow_class, (t_method)xeq_follow_tracks,
		    gensym("tracks"), A_GIMME, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_transpo,
		    gensym("transpo"), A_DEFFLOAT, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_tempo,
		    gensym("tempo"), A_DEFFLOAT, 0);

    class_addfloat(xeq_follow_class, xeq_follow_float);
    class_addbang(xeq_follow_class, xeq_follow_bang);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_next,
		    gensym("next"), A_DEFFLOAT, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_nextnote,
		    gensym("nextnote"), A_DEFFLOAT, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_rewind,
		    gensym("rewind"), 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_stop,
		    gensym("stop"), 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_follow,
		    gensym("follow"), A_DEFFLOAT, 0);

    class_addmethod(xeq_follow_class, (t_method)xeq_follow_locate,
		    gensym("locate"), A_GIMME, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_locate,
		    gensym("locafter"), A_GIMME, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_locate,
		    gensym("skipnotes"), A_GIMME, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_find,
		    gensym("find"), A_GIMME, 0);
    class_addmethod(xeq_follow_class, (t_method)xeq_follow_find,
		    gensym("event"), A_GIMME, 0);
}

void xeq_follow_setup(void)
{
    xeq_setup();
}
