/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* This is a `midiparse' of xeq group */

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

typedef struct _xeq_parse
{
    t_hyphen   x_this;
    t_outlet  *x_polyout;
    t_outlet  *x_ctlout;
    t_outlet  *x_pgmout;
    t_outlet  *x_touchout;
    t_outlet  *x_bendout;
    t_outlet  *x_chanout;
    t_outlet  *x_bangout;
} t_xeq_parse;

static t_class *xeq_parse_class;

/* SEQUENCE TRAVERSING HOOKS */

static void xeqithook_parse_autodelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    clock_delay(base->x_clock,
		base->x_clockdelay = it->i_playloc.l_delay * base->x_tempo);
    base->x_whenclockset = clock_getsystime();
}

static void xeqithook_parse_stepdelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    post("parse step");
}

static void xeqithook_parse_message(t_xeqit *it,
				    t_symbol *target, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_parse *x = (t_xeq_parse *)((t_hyphen *)base)->x_self;
    t_pd *dest = target->s_thing;
    t_atom at[3];
    if (it->i_status)
    {
	outlet_float(x->x_chanout, it->i_channel + 1);
	SETFLOAT(&at[0], it->i_data1);
	if (it->i_data2 >= 0) SETFLOAT(&at[1], it->i_data2);
	switch (it->i_status)
	{
	case 0x80:
	    SETFLOAT(&at[1], 0);
	case 0x90:
	    outlet_list(((t_object *)x)->ob_outlet, 0, 2, at);
	    break;
	case 0xa0:
	    at[2] = at[0];
	    outlet_list(x->x_polyout, 0, 2, &at[1]);
	    break;
	case 0xb0:
	    at[2] = at[0];
	    outlet_list(x->x_ctlout, 0, 2, &at[1]);
	    break;
	case 0xc0:
	    outlet_float(x->x_pgmout, it->i_data1);
	    break;
	case 0xd0:
	    outlet_float(x->x_touchout, it->i_data1);
	    break;
	case 0xe0:
	    /* LATER check this */
	    outlet_float(x->x_bendout, (it->i_data1 << 7) + it->i_data2);
	    break;
	default:;
	}
    }
    else if (dest)
    {
	if (argv->a_type == A_FLOAT)
	    typedmess(dest, &s_list, argc, argv);
	else if (argv->a_type == A_SYMBOL)
	    typedmess(dest, argv->a_w.w_symbol, argc-1, argv+1);
    }
}

static void xeqithook_parse_finish(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_parse *x = (t_xeq_parse *)((t_hyphen *)base)->x_self;
    outlet_bang(x->x_bangout);
}

static void xeq_parse_flush(t_xeq_parse *x);
static void xeqithook_parse_loopover(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_parse *x = (t_xeq_parse *)((t_hyphen *)base)->x_self;
    if (it->i_loopover)
	outlet_bang(x->x_bangout);
    xeq_parse_flush(x);
}

/* CLOCK HANDLER */

static void xeq_parse_tick(t_xeq *base)
{
    base->x_whenclockset = 0;
    xeqit_donext(&base->x_autoit);
}

/* CREATION/DESTRUCTION */

static void *xeq_parse_new(t_symbol *seqname, t_symbol *refname)
{
    t_xeq_parse *x =
	(t_xeq_parse *)xeq_derived_new(xeq_parse_class, 1, seqname, refname,
				       (t_method)xeq_parse_tick);
    if (!x) return (0);

    /* fill in our `vtbl' */
    xeqit_sethooks(&XEQ_BASE(x)->x_autoit, xeqithook_parse_autodelay,
		   xeqithook_applypp, xeqithook_parse_message,
		   xeqithook_parse_finish, xeqithook_parse_loopover);
    xeqit_sethooks(&XEQ_BASE(x)->x_stepit, xeqithook_parse_stepdelay,
		   xeqithook_applypp, xeqithook_parse_message,
		   xeqithook_parse_finish, 0);

    outlet_new((t_object *)x, &s_list);
    x->x_polyout = outlet_new((t_object *)x, &s_list);
    x->x_ctlout = outlet_new((t_object *)x, &s_list);
    x->x_pgmout = outlet_new((t_object *)x, &s_float);
    x->x_touchout = outlet_new((t_object *)x, &s_float);
    x->x_bendout = outlet_new((t_object *)x, &s_float);
    x->x_chanout = outlet_new((t_object *)x, &s_float);
    x->x_bangout = outlet_new((t_object *)x, &s_bang);
    return (x);
}

static void xeq_parse_free(t_xeq_parse *x)
{
    xeq_derived_free((t_hyphen *)x);
}

static void xeq_parse_host(t_xeq_parse *x, t_symbol *seqname)
{
    xeq_derived_reembed((t_hyphen *)x, seqname);
}

/* PLAYBACK PARAMETERS METHODS */

static void xeq_parse_tracks(t_xeq_parse *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_tracks(XEQ_BASE(x), s, ac, av);
}

static void xeq_parse_transpo(t_xeq_parse *x, t_floatarg f)
{
    xeq_transpo(XEQ_BASE(x), f);
}

static void xeq_parse_tempo(t_xeq_parse *x, t_floatarg f)
{
    xeq_tempo(XEQ_BASE(x), f);
}

/* PLAYBACK CONTROL METHODS */

static void xeq_parse_flush(t_xeq_parse *x)
{
    t_xeq *base = XEQ_BASE(x);
    int channel, key, transposed;
    t_atom at[2];
    SETFLOAT(&at[1], 0);
    for (channel = 0; channel < 16; channel++)
    {
	for (key = 0; key < 128; key++)
	{
	    if ((transposed = base->x_noteons[channel][key]) >= 0)
	    {
		outlet_float(x->x_chanout, channel);
		SETFLOAT(&at[0], transposed);
		outlet_list(((t_object *)x)->ob_outlet, 0, 2, at);
		base->x_noteons[channel][key] = -1;
	    }
	}
    }
}

static void xeq_parse_rewind(t_xeq_parse *x)
{
    xeq_rewind(XEQ_BASE(x));
}

static void xeq_parse_stop(t_xeq_parse *x)
{
    xeq_stop(XEQ_BASE(x));
}

static void xeq_parse_next(t_xeq_parse *x, t_floatarg drop)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeqit *it = &XEQ_BASE(x)->x_stepit;
	xeqit_sethooks(it, xeqithook_parse_stepdelay,
		       drop ? 0 : xeqithook_applypp,
		       drop ? 0 : xeqithook_parse_message,
		       xeqithook_parse_finish, 0);
	xeqit_donext(it);
    }
}

static void xeq_parse_dobang(t_xeq_parse *x)
{
    xeq_parse_flush(x);
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x);
	xeqit_sethooks(&base->x_autoit, xeqithook_parse_autodelay,
		       xeqithook_applypp, xeqithook_parse_message,
		       xeqithook_parse_finish, xeqithook_parse_loopover);
	xeq_start(base);
    }
}

static void xeq_parse_bang(t_xeq_parse *x)
{
    t_xeqit *it = &XEQ_BASE(x)->x_autoit;
    /* FIXME rewind after load (any playable friend) */
    xeqlocator_reset(&it->i_blooploc);
    xeqlocator_hide(&it->i_elooploc);
    xeq_parse_dobang(x);
}

static void xeq_parse_loop(t_xeq_parse *x, t_symbol *s, int ac, t_atom *av)
{
    if (xeq_derived_validate((t_hyphen *)x))
	xeq_loop(XEQ_BASE(x), s, ac, av);
}

static void xeq_parse_locate(t_xeq_parse *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_locate(XEQ_BASE(x), s, ac, av);
}

static void xeq_parse_find(t_xeq_parse *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_find(XEQ_BASE(x), s, ac, av);
}

static void *xeq_parse_newhost(t_symbol *s, int ac, t_atom *av)
{
    if (!ac) s = &s_;
    else if (av->a_type == A_SYMBOL) s = av->a_w.w_symbol;
    else return (0);
    return (xeq_parse_new(&s_, s));
}

void xeq_parse_dosetup(void)
{
    xeq_parse_class = class_new(gensym("xeq_parse"), (t_newmethod)xeq_parse_new,
			       (t_method)xeq_parse_free, sizeof(t_xeq_parse),
			       0, A_DEFSYM, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_parse_new,
		     gensym("xeq-parse"), A_DEFSYM, A_DEFSYM, 0);

    xeq_host_enable(xeq_parse_class, (t_newmethod)xeq_parse_newhost);

    class_addmethod(xeq_parse_class, (t_method)xeq_parse_host,
		    gensym("host"), A_DEFSYM, 0);

    class_addmethod(xeq_parse_class, (t_method)xeq_parse_tracks,
		    gensym("tracks"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_transpo,
		    gensym("transpo"), A_DEFFLOAT, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_tempo,
		    gensym("tempo"), A_DEFFLOAT, 0);

    class_addbang(xeq_parse_class, xeq_parse_bang);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_next,
		    gensym("next"), A_DEFFLOAT, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_loop,
		    gensym("loop"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_rewind,
		    gensym("rewind"), 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_stop,
		    gensym("stop"), 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_flush,
		    gensym("flush"), 0);

    class_addmethod(xeq_parse_class, (t_method)xeq_parse_locate,
		    gensym("locate"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_locate,
		    gensym("locafter"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_locate,
		    gensym("skipnotes"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_find,
		    gensym("find"), A_GIMME, 0);
    class_addmethod(xeq_parse_class, (t_method)xeq_parse_find,
		    gensym("event"), A_GIMME, 0);
}

void xeq_parse_setup(void)
{
    xeq_setup();
}
