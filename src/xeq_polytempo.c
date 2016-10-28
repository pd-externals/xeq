/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* TODO:
   - allow for new rubato to interrupt current one
   - stop rubato at: stop, rewind, end of sequence
   - make it more robust (survive host changes etc.)
   - rethink time constraints of rubato envelope
   - rit./acc. (a separate preprocessing message or class?)

   CONSIDER:
   - do not assume host tempo is constant: check/fix on last tick
*/

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

#define XEQ_POLYTEMPO_ENVELOPE_MAX  256

typedef struct _xeq_polytempo_envelope
{
    t_xeq    *e_base;
    t_clock  *e_clock;
    double    e_whenclockset;
    float     e_clockdelay;
    float     e_table[XEQ_POLYTEMPO_ENVELOPE_MAX];
    int       e_count;
    int       e_current;
    float     e_targettempo;
} t_xeq_polytempo_envelope;

typedef struct _xeq_polytempo
{
    t_hyphen  x_this;
    int       x_firstlayer;  /* +1 on i/o */
    int       x_lastlayer;
    int       x_maxlayers;   /* x_env nelements */
    t_xeq_polytempo_envelope  *x_env;
} t_xeq_polytempo;

static t_class *xeq_polytempo_class;

static void xeq_polytempo_layers(t_xeq_polytempo *x,
				 t_floatarg f1, t_floatarg f2);

static void xeq_polytempo_tick(t_xeq_polytempo_envelope *ep)
{
    int current = ep->e_current + 2;
    if (current + 1 == ep->e_count)
    {
	bug("xeq_polytempo_tick");
	current++;
    }
    if (current >= ep->e_count)
    {
	ep->e_whenclockset = 0;
	xeq_tempo(ep->e_base, ep->e_targettempo);
    }
    else {
	float nextdelay = ep->e_table[current];
	xeq_tempo(ep->e_base, ep->e_table[current + 1] / nextdelay);
	clock_delay(ep->e_clock, ep->e_clockdelay = nextdelay);
	ep->e_whenclockset = clock_getsystime();
	ep->e_current = current;
    }
}

static void xeq_polytempo_newclocks(t_xeq_polytempo *x)
{
    t_xeq_polytempo_envelope *ep = x->x_env;
    int i;
    if (ep) for (i = 0; i < x->x_maxlayers; i++, ep++)
    {
	ep->e_whenclockset = 0;
	ep->e_clockdelay = 0;
	ep->e_clock = clock_new(ep, (t_method)xeq_polytempo_tick);
    }
}

static void xeq_polytempo_freeclocks(t_xeq_polytempo *x)
{
    t_xeq_polytempo_envelope *ep = x->x_env;
    int i;
    if (ep) for (i = 0; i < x->x_maxlayers; i++, ep++)
    {
	ep->e_whenclockset = 0;
	ep->e_clockdelay = 0;
	if (ep->e_clock) clock_free(ep->e_clock);
    }
}

static void *xeq_polytempo_new(t_symbol *name)
{
    t_xeq_polytempo *x =
	(t_xeq_polytempo *)hyphen_new(xeq_polytempo_class, "xeq");
    hyphen_attach((t_hyphen *)x, name);
    x->x_firstlayer = 0;
    x->x_lastlayer = -1;
    x->x_maxlayers = 0;
    x->x_env = 0;
    xeq_polytempo_layers(x, 0, 0);
    return (x);
}

static void xeq_polytempo_free(t_xeq_polytempo *x)
{
    hyphen_detach((t_hyphen *)x);
    xeq_polytempo_freeclocks(x);
    freebytes(x->x_env, x->x_maxlayers * sizeof(*x->x_env));
}

static void xeq_polytempo_host(t_xeq_polytempo *x, t_symbol *name)
{
    hyphen_attach((t_hyphen *)x, name);
    x->x_firstlayer = 0;
    x->x_lastlayer = -1;
    xeq_polytempo_layers(x, 0, 0);
}

/* LATER make it safer */
static void xeq_polytempo_layers(t_xeq_polytempo *x,
				 t_floatarg f1, t_floatarg f2)
{
    int i1 = (int)f1, i2 = (int)f2;
    int maxlayers;
printf("xeq_polytempo_layers; x_host: %x\n", XEQ_HOST(x));
    t_xeq *base = XEQ_BASE(x);
    if (!base)
	return;
printf("xeq_polytempo_layers; x_self: %x\n", XEQ_NBASES(((t_hyphen *)base)->x_self));
    maxlayers = XEQ_NBASES(((t_hyphen *)base)->x_self);
printf("xeq_polytempo_layers maxlayers: %d \n", maxlayers);
    if (!x->x_env)
    {
	if (!(x->x_env = getbytes(maxlayers * sizeof(*x->x_env))))
	    return;
	x->x_maxlayers = maxlayers;
	xeq_polytempo_newclocks(x);
printf("xeq_polytempo_layers; set x_env %x\n", x->x_env);
    }
    else if (maxlayers > x->x_maxlayers)
    {
	xeq_polytempo_freeclocks(x);
	if (!(x->x_env = resizebytes(x->x_env,
				     x->x_maxlayers * sizeof(*x->x_env),
				     maxlayers * sizeof(*x->x_env))))
	    return;
	x->x_maxlayers = maxlayers;
	xeq_polytempo_newclocks(x);
printf("xeq_polytempo_layers; resizebytes: %x\n", x->x_env);
    }
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

static void xeq_polytempo_float(t_xeq_polytempo *x, t_floatarg f)
{
    t_xeq *base = XEQ_HOST(x);
    if (base)
    {
	int i;
	base += x->x_firstlayer;
	for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++)
	{
	    xeq_tempo(base, f);
	}
    }
}

static void xeq_polytempo_rubato(t_xeq_polytempo *x,
				 t_symbol *s, int ac, t_atom *av)
{
    t_xeq *base = XEQ_HOST(x), *base1;
    t_xeq_polytempo_envelope *ep = x->x_env, *ep1;
    float reftable[XEQ_POLYTEMPO_ENVELOPE_MAX];
    float realtime = 0, usertime = 0, resyncrealtime = 0;
    int resyncindex = 0;
    float firsttempo;
    int i, j;
    if (!base || !ep || x->x_firstlayer > x->x_lastlayer)
	return;
    if (ac > XEQ_POLYTEMPO_ENVELOPE_MAX) ac = XEQ_POLYTEMPO_ENVELOPE_MAX;
    for (i = 0; i < ac; i++)
    {
	if (av[i].a_type != A_FLOAT)
	{
	    ac = i;
            post("xeq_polytempo_rubato; non-flaoat argument at %d", i);
	    break;
	}
	reftable[i] = av[i].a_w.w_float;
	/* LATER rethink range constraints (separate realtime and usertime?) */
	if (reftable[i] < 50) reftable[i] = 50;
    }
    if (ac < 2)
	return;
    if (ac % 2)
    {
	resyncindex = i = ac - 1;
	resyncrealtime = reftable[i];
	while (i > 1)
	{
	    usertime += reftable[--i];
	    realtime += reftable[--i];
	}
	reftable[ac] = reftable[resyncindex];  /* redundant */
	ac++;
    }
    base += x->x_firstlayer;
    ep += x->x_firstlayer;
    base1 = base;
    ep1 = ep;
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base1++, ep1++)
    {
	if (ep1->e_whenclockset != 0) continue;  /* LATER allow */
	ep1->e_base = base1;
	for (j = 0; j < ac; j++) ep1->e_table[j] = reftable[j];
	ep1->e_count = ac;
	ep1->e_current = 0;
	ep1->e_targettempo = 1. / base1->x_tempo;
	if (resyncindex)
	{
	    float resyncusertime =
		(resyncrealtime + realtime) * ep1->e_targettempo - usertime;
	    /* LATER rethink resync usertime minimum */
	    if (resyncusertime < 50)
	    {
		ep1->e_table[resyncindex] =
		    resyncrealtime - resyncusertime + 50;
		resyncusertime = 50;
#ifdef XEQ_VERBOSE
		post("rubato: resync time expanded to %f",
		     ep1->e_table[resyncindex]);
#endif
	    }
	    ep1->e_table[resyncindex + 1] = resyncusertime;
	}
    }
    firsttempo = reftable[1] / reftable[0];
    for (i = x->x_firstlayer; i <= x->x_lastlayer; i++, base++, ep++)
    {
	if (ep->e_whenclockset != 0) continue;  /* LATER allow */
	xeq_tempo(base, firsttempo);
	clock_delay(ep->e_clock, ep->e_clockdelay = reftable[0]);
	ep->e_whenclockset = clock_getsystime();
    }
}
//    t_hyphen  x_this;
//    int       x_firstlayer;  /* +1 on i/o */
//    int       x_lastlayer;
//    int       x_maxlayers;   /* x_env nelements */
//    t_xeq_polytempo_envelope  *x_env;

//    t_xeq    *e_base;
//    t_clock  *e_clock;
//    double    e_whenclockset;
//    float     e_clockdelay;
//    float     e_table[XEQ_POLYTEMPO_ENVELOPE_MAX];
//    int       e_count;
//    int       e_current;
//    float     e_targettempo;

static void xeq_polytempo_status(t_xeq_polytempo *x)
{
    post("  --==## xeq_polytempo ##==--");
    post("x_this.x_hostname: %s", (x->x_this.x_hostname) ? x->x_this.x_hostname->s_name : "??");
    post("is a host: %s", ((int*)x == (int*)x->x_this.x_host) ? "yes" : "no");
    post("firstlayer: %d", x->x_firstlayer);
    post("lastlayer: %d",  x->x_lastlayer);
    post("maxlayer: %d",   x->x_maxlayers);
    if (x->x_env) {
        post("envelope e_whenclockset: %df", x->x_env->e_whenclockset);
        post("envelope e_clockdelay: %f", x->x_env->e_clockdelay);
        post("envelope e_count: %d", x->x_env->e_count);
        post("envelope e_current: %d", x->x_env->e_current);
        post("envelope e_targettempo: %f", x->x_env->e_targettempo);
        post("envelope e_table:");
        int i;
        for (i = 0; i < x->x_env->e_count; i++)
        {
            post(" %i: %f", i, x->x_env->e_table[i]);
        }
    }
}

void xeq_polytempo_dosetup(void)
{
    xeq_polytempo_class = class_new(gensym("xeq_polytempo"),
				    (t_newmethod)xeq_polytempo_new,
				    (t_method)xeq_polytempo_free,
				    sizeof(t_xeq_polytempo),
				    0, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_polytempo_new,
		     gensym("xeq-polytempo"), A_DEFSYM, 0);
    class_addmethod(xeq_polytempo_class, (t_method)xeq_polytempo_host,
		    gensym("host"), A_DEFSYM, 0);

    class_addmethod(xeq_polytempo_class, (t_method)xeq_polytempo_layers,
		    gensym("layers"), A_DEFFLOAT, A_DEFFLOAT, 0);

    class_addfloat(xeq_polytempo_class, xeq_polytempo_float);
    class_addmethod(xeq_polytempo_class, (t_method)xeq_polytempo_rubato,
		    gensym("rubato"), A_GIMME, 0);
    
    class_addmethod(xeq_polytempo_class, (t_method)xeq_polytempo_status,
		    gensym("status"), 0);
}

void xeq_polytempo_setup(void)
{
    xeq_setup();
}
