/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* send a sequence to a data canvas according to the following template
   (all fields are float atoms): x y dx dy velocity time channel
*/

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

typedef struct _xeq_data
{
    t_hyphen   x_this;
    t_symbol  *x_dsym;      /* data canvas name */
    t_symbol  *x_tsym;      /* template canvas name */
    t_pd      *x_target;    /* data canvas target */
    t_atom     x_argv[8];   /* arguments to message `scalar' */
    float      x_tcoef;
    float      x_pcoef;
    float      x_vcoef;
    float      x_time;
    float      x_duration;
    uchar      x_pitch;
    uchar      x_channel;
} t_xeq_data;

static t_class *xeq_data_class;

static t_symbol *xeq_data_selector;  /* `scalar' selector */

#define XEQ_DATA_TCOEF   0.01
#define XEQ_DATA_PCOEF  10
#define XEQ_DATA_VCOEF   0.1
#define XEQ_NCOLORS     17  /* FIXME for channels: 0 (omni), 1..16 */

/* channel color table */
static int xeq_data_color[XEQ_NCOLORS] =
{
    9, 90, 900, 99, 990, 909, 3, 30, 300, 33, 330, 303, 335, 533, 373, 737, 0
};

static void xeqithook_data_delay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_data *x = (t_xeq_data *)((t_hyphen *)base)->x_self;
    x->x_time += it->i_playloc.l_delay;  /* LATER use delta (also below) */
}

static void xeqithook_data_offdelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_data *x = (t_xeq_data *)((t_hyphen *)base)->x_self;
    x->x_duration += it->i_playloc.l_delay;
}

static void xeqithook_data_message(t_xeqit *it,
				   t_symbol *target, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_data *x = (t_xeq_data *)((t_hyphen *)base)->x_self;
    if (it->i_status == 144 && it->i_data2 &&
	it->i_channel >= 0 && it->i_channel <= 16)
    {
	/* look for a matching noteoff */
	t_xeqit *offit = &base->x_walkit;
	x->x_duration = 0;
	x->x_pitch = it->i_data1;
	x->x_channel = it->i_channel;
	xeqit_rewind(offit);
	xeqit_settoit(offit, it);
	while (!offit->i_finish)
	{
	    xeqit_donext(offit);
	    if (!x->x_pitch) break;
	}

	/* create a scalar */
	SETFLOAT(&x->x_argv[1], x->x_time * x->x_tcoef);
	SETFLOAT(&x->x_argv[2], it->i_data1 * x->x_pcoef);
	SETFLOAT(&x->x_argv[5], it->i_data2 * x->x_vcoef);
	SETFLOAT(&x->x_argv[6],
		 (x->x_duration - offit->i_playloc.l_delay) * x->x_tcoef);
	SETFLOAT(&x->x_argv[7], xeq_data_color[it->i_channel]);
	pd_typedmess(x->x_target, xeq_data_selector, 8, x->x_argv);
    }
}

static void xeqithook_data_offmessage(t_xeqit *it,
				      t_symbol *target, int argc, t_atom *argv)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_data *x = (t_xeq_data *)((t_hyphen *)base)->x_self;
    if (((it->i_status == 144 && !it->i_data2) ||  it->i_status == 128) &&
	it->i_data1 == x->x_pitch && it->i_channel == x->x_channel)
    {
	x->x_pitch = 0;  /* LATER use it->i_finish */
    }
}

static void xeqithook_data_finish(t_xeqit *it)
{
    t_xeq *base = (t_xeq *)it->i_owner;
    t_xeq_data *x = (t_xeq_data *)((t_hyphen *)base)->x_self;
    outlet_bang(((t_object *)x)->ob_outlet);
}

/* CREATION/DESTRUCTION */

static void *xeq_data_new(t_symbol *seqname, t_symbol *dsym, t_symbol *tsym)
{
    printf("xeq_data_new; init\n");
    t_xeq_data *x =
	(t_xeq_data *)xeq_derived_new(xeq_data_class, 1, seqname, 0, 0);
    printf("xeq_data_new; x: %x\n", x);
    int i;
    if (!x) return (0);

    /* fill in our `vtbl' */
    xeqit_sethooks(&XEQ_BASE(x)->x_stepit, xeqithook_data_delay, 0,
		   xeqithook_data_message, xeqithook_data_finish, 0);
    printf("xeq_data_new; xeqit_sethooks x_stepit\n");
    xeqit_sethooks(&XEQ_BASE(x)->x_walkit, xeqithook_data_offdelay, 0,
		   xeqithook_data_offmessage, 0, 0);
    printf("xeq_data_new; xeqit_sethooks x_walkit\n");
 
    outlet_new((t_object *)x, &s_bang);

    if ((x->x_dsym = dsym) != &s_)
	x->x_target = x->x_dsym->s_thing;
    if ((x->x_tsym = tsym) != &s_)
	SETSYMBOL(&x->x_argv[0], x->x_tsym);
    for (i = 1; i < 8; i++)
	SETFLOAT(&x->x_argv[i], 0);
    x->x_tcoef = XEQ_DATA_TCOEF;
    x->x_pcoef = XEQ_DATA_PCOEF;
    x->x_vcoef = XEQ_DATA_VCOEF;
    x->x_time = 0;
    return (x);
}

static void xeq_data_free(t_xeq_data *x)
{
    xeq_derived_free((t_hyphen *)x);
}

static void xeq_data_host(t_xeq_data *x, t_symbol *seqname)
{
    xeq_derived_reembed((t_hyphen *)x, seqname);
}

static void xeq_data_data(t_xeq_data *x, t_symbol *s)
{
    x->x_dsym = s;
    x->x_target = x->x_dsym->s_thing;
}

static void xeq_data_template(t_xeq_data *x, t_symbol *s)
{
    x->x_tsym = s;
    SETSYMBOL(&x->x_argv[0], x->x_tsym);
}

static void xeq_data_scale(t_xeq_data *x,
			   t_floatarg f1, t_floatarg f2, t_floatarg f3)
{
    x->x_tcoef = f1 ? f1 : XEQ_DATA_TCOEF;
    x->x_pcoef = f2 ? f2 : XEQ_DATA_PCOEF;
    x->x_vcoef = f3 ? f3 : XEQ_DATA_VCOEF;
}

static void xeq_data_bang(t_xeq_data *x)
{
    t_xeq *host = XEQ_HOST(x);
    if (host && x->x_dsym != &s_ && x->x_tsym != &s_ && x->x_target)
    {
	t_xeq *base = XEQ_BASE(x);
	t_xeqit *it = &base->x_stepit;
	x->x_time = 0;
	xeqit_rewind(it);
	while (!it->i_finish)
	{
	    xeqit_donext(it);
	}
    }
}

void xeq_data_dosetup(void)
{
    printf("xeq_data_dosetup init\n");
    xeq_data_class = class_new(gensym("xeq_data"), (t_newmethod)xeq_data_new,
			       (t_method)xeq_data_free, sizeof(t_xeq_data),
			       0, A_SYMBOL, A_DEFSYM, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_data_new,
		     gensym("xeq-data"), A_SYMBOL, A_DEFSYM, A_DEFSYM, 0);
    class_addmethod(xeq_data_class, (t_method)xeq_data_host,
		    gensym("host"), A_DEFSYM, 0);
    class_addmethod(xeq_data_class, (t_method)xeq_data_data,
		    gensym("data"), A_DEFSYM, 0);
    class_addmethod(xeq_data_class, (t_method)xeq_data_template,
		    gensym("template"), A_DEFSYM, 0);
    class_addmethod(xeq_data_class, (t_method)xeq_data_scale,
		    gensym("scale"), A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addbang(xeq_data_class, xeq_data_bang);
    xeq_data_selector = gensym("scalar");
    printf("xeq_data_dosetup ok\n");
}

void xeq_data_setup(void)
{
     printf("xeq_data_setup init\n");
   xeq_setup();
}
