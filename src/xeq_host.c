/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* A wrapper for `hostified' friends */

/* TODO:

   BUGS:
*/

#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "dict.h"
#include "hyphen.h"
#include "xeq.h"

static t_dict *xeq_host_dict = 0;  /* name -> class mapping */

/* plug this into any hostifiable friend */
void xeq_host_anything(t_hyphen *x, t_symbol *s, int ac, t_atom *av)
{
    if (x->x_host == x->x_basetable)
	pd_typedmess((t_pd *)XEQ_BASE(x), s, ac, av);
    else
	/* this is pd_defaultanything()'s body (declared static...) */
	pd_error((t_pd *)x, "%s: no method for '%s'",
		 class_getname(*(t_pd *)x), s->s_name);
}

/* called in c's setup routine */
void xeq_host_enable(t_class *c, t_newmethod newmethod)
{
    char *name = class_getname(c), *ptr;
    t_symbol *s;
    if ((ptr = strchr(name, '_')) && *++ptr) name = ptr;
    s = dict_key(xeq_host_dict, name);
    dict_bind(xeq_host_dict, (t_pd *)newmethod, s);
    class_addanything(c, xeq_host_anything);
}

/* CREATION/DESTRUCTION */

typedef t_pd *(*t_newgimme)(t_symbol *s, int argc, t_atom *argv);
static void *xeq_host_new(t_symbol *s, int ac, t_atom *av)
{
    void *x = 0;
    if (ac-- && av->a_type == A_SYMBOL && (s = av++->a_w.w_symbol))
    {
	t_symbol *s1 = dict_key(xeq_host_dict, s->s_name);
	t_newgimme m = (t_newgimme)dict_value(xeq_host_dict, s1);
	if (m)
	{
	    x = (*m)(s, ac, av);
	}
        if (!x) 
        {
            post("xeq_host: '%s' is not a valid dictionary key", s1->s_name);    
        }
    }
    return (x);
}

void xeq_host_dosetup(void)
{
    if (!xeq_host_dict) xeq_host_dict = dict_new(0);
    class_addcreator((t_newmethod)xeq_host_new,
		     gensym("xeq_host"), A_GIMME, 0);
    class_addcreator((t_newmethod)xeq_host_new,
		     gensym("xeq-host"), A_GIMME, 0);
}

void xeq_host_setup(void)
{
    xeq_setup();
    post("xeq_host: setup");
}
