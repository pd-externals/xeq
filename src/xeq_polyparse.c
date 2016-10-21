/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* Multilayer version of xeq_parse */

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

typedef struct _xeq_polyparse
{
    t_hyphen   x_this;
    int        x_firstlayer;  /* +1 on i/o */
    int        x_lastlayer;
    t_outlet  *x_polyout;
    t_outlet  *x_ctlout;
    t_outlet  *x_pgmout;
    t_outlet  *x_touchout;
    t_outlet  *x_bendout;
    t_outlet  *x_chanout;
    t_outlet  *x_layerout;
    t_outlet  *x_finout;
} t_xeq_polyparse;

static t_class *xeq_polyparse_class;

/* SEQUENCE TRAVERSING HOOKS */

static void xeqithook_polyparse_autodelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    clock_delay(base->x_clock,
		base->x_clockdelay = it->i_playloc.l_delay * base->x_tempo);
    base->x_whenclockset = clock_getsystime();
}

static void xeqithook_polyparse_stepdelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    post("parse step");
}

static void xeqithook_polyparse_message(t_xeqit *it, t_symbol *target,
					int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_polyparse *x = (t_xeq_polyparse *)((t_hyphen *)base)->x_self;
    t_pd *dest = target->s_thing;
    t_atom at[2];
    if (it->i_status)
    {
	outlet_float(x->x_layerout, ((t_hyphen *)base)->x_id + 1);
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
	    outlet_list(x->x_polyout, 0, 2, at);
	    break;
	case 0xb0:
	    outlet_list(x->x_ctlout, 0, 2, at);
	    break;
	case 0xc0:
	    outlet_float(x->x_pgmout, it->i_data1);
	    break;
	case 0xd0:
	    outlet_float(x->x_touchout, it->i_data1);
	    break;
	case 0xe0:
	    outlet_list(x->x_bendout, 0, 2, at);
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

static void xeqithook_polyparse_muted(t_xeqit *it, t_symbol *target,
				      int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_polyparse *x = (t_xeq_polyparse *)((t_hyphen *)base)->x_self;
    t_pd *dest = target->s_thing;
    t_atom at[2];
    if (it->i_status)
    {
	if (it->i_status == 0x80 || (it->i_status == 0x90 && !it->i_data2))
	{
	    outlet_float(x->x_layerout, ((t_hyphen *)base)->x_id + 1);
	    outlet_float(x->x_chanout, it->i_channel + 1);
	    SETFLOAT(&at[0], it->i_data1);
	    SETFLOAT(&at[1], 0);
	    outlet_list(((t_object *)x)->ob_outlet, 0, 2, at);
	}
    }
    else if (dest)
    {
	/* LATER... */
    }
}

static void xeqithook_polyparse_finish(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_polyparse *x = (t_xeq_polyparse *)((t_hyphen *)base)->x_self;
    outlet_float(x->x_finout, ((t_hyphen *)base)->x_id + 1);
}

static void xeq_polyparse_flush(t_xeq_polyparse *x);
static void xeqithook_polyparse_loopover(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_polyparse *x = (t_xeq_polyparse *)((t_hyphen *)base)->x_self;
    if (it->i_loopover)
	outlet_float(x->x_finout, ((t_hyphen *)base)->x_id + 1);
    xeq_polyparse_flush(x);
}

/* CLOCK HANDLER */

static void xeq_polyparse_tick(t_xeq *base)
{
    base->x_whenclockset = 0;
    xeqit_donext(&base->x_autoit);
}

/* CREATION/DESTRUCTION */

/* LATER consider using A_GIMME, because the argument order is confusing,
   and nobody knows if it is not going to change some day... */
static void *xeq_polyparse_new(t_symbol *seqname, t_symbol *refname,
			       t_floatarg f)
{
    t_xeq_polyparse *x =
	(t_xeq_polyparse *)xeq_derived_new(xeq_polyparse_class, (int)f,
					   seqname, refname,
					   (t_method)xeq_polyparse_tick);
    printf("xeq_derived_new() ok\n");
    t_xeq *base;
    int i, nlayers;
    if (!x) return (0);

    for (i = 0, nlayers = XEQ_NBASES(x), base = XEQ_BASE(x);
	 i < nlayers; i++,  base += sizeof(t_xeq))
    {
	xeqit_sethooks(&base->x_autoit, xeqithook_polyparse_autodelay,
		       xeqithook_applypp, xeqithook_polyparse_message,
		       xeqithook_polyparse_finish,
		       xeqithook_polyparse_loopover);
	xeqit_sethooks(&base->x_stepit, xeqithook_polyparse_stepdelay,
		       xeqithook_applypp, xeqithook_polyparse_message,
		       xeqithook_polyparse_finish, 0);
    }
    x->x_firstlayer = 0;
    x->x_lastlayer = nlayers - 1;

    outlet_new((t_object *)x, &s_list);
    x->x_polyout = outlet_new((t_object *)x, &s_list);
    x->x_ctlout = outlet_new((t_object *)x, &s_list);
    x->x_pgmout = outlet_new((t_object *)x, &s_float);
    x->x_touchout = outlet_new((t_object *)x, &s_float);
    x->x_bendout = outlet_new((t_object *)x, &s_list);
    x->x_chanout = outlet_new((t_object *)x, &s_float);
    x->x_layerout = outlet_new((t_object *)x, &s_float);
    x->x_finout = outlet_new((t_object *)x, &s_float);
    return (x);
}

static void xeq_polyparse_free(t_xeq_polyparse *x)
{
    xeq_derived_free((t_hyphen *)x);
}

static void xeq_polyparse_maxlayers(t_xeq_polyparse *x, t_floatarg f)
{
    xeq_derived_resizeembed((t_hyphen *)x, (int)f);
}

/* LATER allow using xeq_polyparse in a multihost mode */
static void xeq_polyparse_host(t_xeq_polyparse *x, t_symbol *seqname)
{
    xeq_derived_reembed((t_hyphen *)x, seqname);
}

/* PLAYBACK PARAMETERS METHODS */

static void xeq_polyparse_layers(t_xeq_polyparse *x,
				 t_floatarg f1, t_floatarg f2)
{
    int i1 = (int)f1, i2 = (int)f2;
    int maxlayers = XEQ_NBASES(x);
    if (i1 > maxlayers)
	return;
    if (i1 > 0 && i2 >= i1)
    {
	x->x_firstlayer = i1 - 1;
	x->x_lastlayer = i2 <= maxlayers ? i2 - 1 : maxlayers - 1;
    }
    else if (!i2)
    {
	if (!i1)  /* LATER check `layers 0 n' */
	{
	    x->x_firstlayer = 0;
	    x->x_lastlayer = maxlayers - 1;
	}
	else if (i1 > 0) x->x_firstlayer = x->x_lastlayer = i1 - 1;
    }
}

static void xeq_polyparse_tracks(t_xeq_polyparse *x,
				 t_symbol *s, int ac, t_atom *av)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeq_tracks(base, s, ac, av);
    }
}

static void xeq_polyparse_transpo(t_xeq_polyparse *x, t_floatarg f)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeq_transpo(base, f);
    }
}

static void xeq_polyparse_tempo(t_xeq_polyparse *x, t_floatarg f)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeq_tempo(base, f);
    }
}

/* PLAYBACK CONTROL METHODS */

static void xeq_polyparse_flush(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x);
    int nlayers = XEQ_NBASES(x);
    int layer, channel, key, transposed;
    t_atom at[2];
    SETFLOAT(&at[1], 0);
    for (layer = 0; layer < nlayers; layer++, base++)  /* flush them all */
    {
	for (channel = 0; channel < 16; channel++)
	{
	    for (key = 0; key < 128; key++)
	    {
		if ((transposed = base->x_noteons[channel][key]) >= 0)
		{
		    outlet_float(x->x_layerout, layer + 1);
		    outlet_float(x->x_chanout, channel);
		    SETFLOAT(&at[0], transposed);
		    outlet_list(((t_object *)x)->ob_outlet, 0, 2, at);
		    base->x_noteons[channel][key] = -1;
		}
	    }
	}
    }
}

static void xeq_polyparse_rewind(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeq_rewind(base);
    }
}

static void xeq_polyparse_stop(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeq_stop(base);
    }
}

static void xeq_polyparse_next(t_xeq_polyparse *x, t_floatarg drop)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
	int i;
	for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
	{
	    xeqit_sethooks(&base->x_stepit, xeqithook_polyparse_stepdelay,
			   drop ? 0 : xeqithook_applypp,
			   drop ? 0 : xeqithook_polyparse_message,
			   xeqithook_polyparse_finish, 0);
	    xeqit_donext(&base->x_stepit);
	}
    }
}

static void xeq_polyparse_dobang(t_xeq_polyparse *x)
{
    xeq_polyparse_flush(x);
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
	int i;
	for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
	{
	    xeqit_sethooks(&base->x_autoit, xeqithook_polyparse_autodelay,
			   xeqithook_applypp, xeqithook_polyparse_message,
			   xeqithook_polyparse_finish,
			   xeqithook_polyparse_loopover);
	    xeq_start(base);
	}
    }
}

static void xeq_polyparse_bang(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	t_xeqit *it = &base->x_autoit;
	xeqlocator_reset(&it->i_blooploc);
	xeqlocator_hide(&it->i_elooploc);
    }
    xeq_polyparse_dobang(x);
}

static void xeq_polyparse_loop(t_xeq_polyparse *x,
			       t_symbol *s, int ac, t_atom *av)
{
    if (xeq_derived_validate((t_hyphen *)x))
    {
	t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
	int i;
	for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
	{
	    xeq_loop(base, s, ac, av);
	}
    }
}

static void xeq_polyparse_mute(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeqit_sethooks(&base->x_autoit, xeqithook_polyparse_autodelay,
		       xeqithook_applypp, xeqithook_polyparse_muted,
		       xeqithook_polyparse_finish,
		       xeqithook_polyparse_loopover);
    }
}

static void xeq_polyparse_unmute(t_xeq_polyparse *x)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int i;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
    {
	xeqit_sethooks(&base->x_autoit, xeqithook_polyparse_autodelay,
		       xeqithook_applypp, xeqithook_polyparse_message,
		       xeqithook_polyparse_finish,
		       xeqithook_polyparse_loopover);
    }
}

/* this is sketchy, LATER fix it */
static void xeq_polyparse_sync(t_xeq_polyparse *x, t_floatarg f)
{
    int reflayer = (int)f;
    if (reflayer >= 0 && reflayer < XEQ_NBASES(x))
    {
	t_xeq *base = XEQ_BASE(x) + reflayer;
	t_xeqit *refit = &base->x_autoit;
	t_xeqlocator *loc, *refloc = &refit->i_playloc;
	int i;
	float clockleft = 0;  /* FIXME */
	if (base->x_whenclockset != 0)
	{
	    float elapsed = clock_gettimesince(base->x_whenclockset);
	    clockleft = base->x_clockdelay - elapsed;
	    if (clockleft < 0) clockleft = 0;
	}
	base = XEQ_BASE(x) + x->x_firstlayer;
	for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
	{
	    if (i != reflayer)
	    {
		t_xeqlocator *loc = &base->x_autoit.i_playloc;
		xeqlocator_settolocator(loc, refloc);
		/* LATER adjust loc according to clockleft */
	    }
	}
    }
}

/* LATER make a multihost version */
static void xeq_polyparse_locate(t_xeq_polyparse *x,
				 t_symbol *s, int ac, t_atom *av)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int nlayers = x->x_lastlayer - x->x_firstlayer;
    if (nlayers >= 0)
    {
	t_xeqlocator *refloc = xeq_dolocate(base, s, ac, av);
	if (refloc)
	{
	    t_symbol *which = 0;
	    if (av->a_type == A_SYMBOL) which = av->a_w.w_symbol;
	    while (nlayers--)
	    {
		base++;
		xeqlocator_settolocator(which ? xeq_whichloc(base, which)
					: &base->x_autoit.i_playloc,
					refloc);
	    }
	}
    }
}

/* LATER make a multihost version */
static void xeq_polyparse_find(t_xeq_polyparse *x,
			       t_symbol *s, int ac, t_atom *av)
{
    t_xeq *base = XEQ_BASE(x) + x->x_firstlayer;
    int nlayers = x->x_lastlayer - x->x_firstlayer;
    if (nlayers >= 0)
    {
	t_xeqlocator *refloc = &base->x_beditloc;
	xeq_find(base, s, ac, av);
	while (nlayers--)
	{
	    xeqlocator_settolocator(&(++base)->x_beditloc, refloc);
	}
    }
}

static void *xeq_polyparse_newhost(t_symbol *s, int ac, t_atom *av)
{
    t_float f = 0;
    if (ac) f = av++->a_w.w_float, ac--;
    else return (0);
    if (!ac) s = &s_;
    else if (av->a_type == A_SYMBOL) s = av->a_w.w_symbol;
    else return (0);
    return (xeq_polyparse_new(&s_, s, f));
}

void xeq_polyparse_dosetup(void)
{
    xeq_polyparse_class = class_new(gensym("xeq_polyparse"),
				    (t_newmethod)xeq_polyparse_new,
				    (t_method)xeq_polyparse_free,
				    sizeof(t_xeq_polyparse),
//				    0, A_DEFFLOAT, A_DEFSYM, A_DEFSYM, 0);
				    0, A_DEFSYM, A_DEFSYM, A_DEFFLOAT, 0);
    class_addcreator((t_newmethod)xeq_polyparse_new,
		     gensym("xeq-polyparse"),
		     A_DEFFLOAT, A_DEFSYM, A_DEFSYM, 0);

    xeq_host_enable(xeq_polyparse_class, (t_newmethod)xeq_polyparse_newhost);

    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_maxlayers,
		    gensym("maxlayers"), A_DEFFLOAT, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_host,
		    gensym("host"), A_DEFSYM, 0);

    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_layers,
		    gensym("layers"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_tracks,
		    gensym("tracks"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_transpo,
		    gensym("transpo"), A_DEFFLOAT, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_tempo,
		    gensym("tempo"), A_DEFFLOAT, 0);

    class_addbang(xeq_polyparse_class, xeq_polyparse_bang);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_next,
		    gensym("next"), A_DEFFLOAT, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_loop,
		    gensym("loop"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_rewind,
		    gensym("rewind"), 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_stop,
		    gensym("stop"), 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_mute,
		    gensym("mute"), 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_unmute,
		    gensym("unmute"), 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_flush,
		    gensym("flush"), 0);

    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_sync,
		    gensym("sync"), A_DEFFLOAT, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_locate,
		    gensym("locate"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_locate,
		    gensym("locafter"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_locate,
		    gensym("skipnotes"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_find,
		    gensym("find"), A_GIMME, 0);
    class_addmethod(xeq_polyparse_class, (t_method)xeq_polyparse_find,
		    gensym("event"), A_GIMME, 0);
    printf("xeq_polyparse_dosetup; complete\n");
}

void xeq_polyparse_setup(void)
{
    xeq_setup();
}
