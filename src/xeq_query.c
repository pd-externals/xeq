/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* A reporting tool.  Selector of any message it sends is the selector
   of a quering message.  Output messages may be filtered through `route'. */

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "hyphen.h"
#include "xeq.h"

#define XEQ_QUERY_NATOMS_MAX  16

typedef struct _xeq_query
{
    t_hyphen   x_this;
    t_atom     x_buffer[XEQ_QUERY_NATOMS_MAX];  /* ideally a nonshrinking
						   binbuf */
} t_xeq_query;

static t_class *xeq_query_class;

static void *xeq_query_new(t_symbol *name)
{
    t_xeq_query *x = (t_xeq_query *)hyphen_new(xeq_query_class, "xeq");
    hyphen_attach((t_hyphen *)x, name);
    outlet_new((t_object *)x, 0);
    return (x);
}

static void xeq_query_free(t_xeq_query *x)
{
    hyphen_detach((t_hyphen *)x);
}

static void xeq_query_host(t_xeq_query *x, t_symbol *name)
{
    hyphen_attach((t_hyphen *)x, name);
}

static void xeq_query_tracks(t_xeq_query *x, t_symbol *s, int ac, t_atom *av)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	SETSYMBOL(&x->x_buffer[0],
		  host->x_ttp ? host->x_ttp->t_given : gensym("all"));
	outlet_anything(((t_object *)x)->ob_outlet, s, 1, x->x_buffer);
    }
}

static void xeq_query_transpo(t_xeq_query *x, t_symbol *s, int ac, t_atom *av)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	SETFLOAT(&x->x_buffer[0], host->x_transpo);
	outlet_anything(((t_object *)x)->ob_outlet, s, 1, x->x_buffer);
    }
}

static void xeq_query_tempo(t_xeq_query *x, t_symbol *s, int ac, t_atom *av)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	SETFLOAT(&x->x_buffer[0], host->x_tempo);
	outlet_anything(((t_object *)x)->ob_outlet, s, 1, x->x_buffer);
    }
}

static void xeq_query_natoms(t_xeq_query *x, t_symbol *s, int ac, t_atom *av)
{
    t_xeq *host = XEQ_HOST(x);
    if (host)
    {
	SETFLOAT(&x->x_buffer[0],
		 host->x_binbuf ? binbuf_getnatom(host->x_binbuf) : 0);
	outlet_anything(((t_object *)x)->ob_outlet, s, 1, x->x_buffer);
    }
}

static void xeq_query_status(t_xeq_query *x)
{
    post("  --==## xeq_query status ##==--");
    post("x_this.x_hostname:  %s", x->x_this.x_hostname->s_name);
    post("x_this.x_friendname: %s", x->x_this.x_friendname->s_name);
//    post("x_this.x_host.x_hostname: %s", x->x_this.x_host->x_hostname->s_name);
//    post("x_this.x_host.x_ttp.first: %d", XEQ_HOST(x->x_this.x_host)->x_ttp->t_first);
    int i;
    for (i = 0; i < XEQ_QUERY_NATOMS_MAX; i++)
    {
        post("nantom %d: %d", i, x->x_buffer[i].a_type);
    }
}

void xeq_query_dosetup(void)
{
    xeq_query_class = class_new(gensym("xeq_query"),
				(t_newmethod)xeq_query_new,
				(t_method)xeq_query_free, sizeof(t_xeq_query),
				0, A_DEFSYM, 0);
    class_addcreator((t_newmethod)xeq_query_new,
		     gensym("xeq-query"), A_DEFSYM, 0);
    class_addmethod(xeq_query_class, (t_method)xeq_query_host,
		    gensym("host"), A_DEFSYM, 0);

    class_addmethod(xeq_query_class, (t_method)xeq_query_tracks,
		    gensym("tracks"), A_GIMME, 0);
    class_addmethod(xeq_query_class, (t_method)xeq_query_transpo,
		    gensym("transpo"), A_GIMME, 0);
    class_addmethod(xeq_query_class, (t_method)xeq_query_tempo,
		    gensym("tempo"), A_GIMME, 0);
    class_addmethod(xeq_query_class, (t_method)xeq_query_natoms,
		    gensym("natoms"), A_GIMME, 0);
    class_addmethod(xeq_query_class, (t_method)xeq_query_status,
		    gensym("status"), 0);
}

void xeq_query_setup(void)
{
    xeq_setup();
}
