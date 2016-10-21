/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* Host and friends -- an extern group. */

#include "m_pd.h"
#include "m_imp.h"
#include "dict.h"
#include "hyphen.h"

#if 1
#define HYPHEN_VERBOSE
#if 0
#define HYPHEN_DEBUG
#endif
#endif

/* Each hyphen class holds two dictionaries: one to bind host names and hosts,
   and another for friend names.  Host and friend dictionaries of a class are
   bound to class name in meta-dictionaries. */
static t_dict *hyphen_hdict = 0;  /* host meta-dictionary */
static t_dict *hyphen_fdict = 0;  /* friend meta-dictionary */

/* called from within host setup routine,
   to create a glue for hosts and friends */
void hyphen_setup(t_class *hostclass, t_class **baseclass)
{
    t_dict *hdict = dict_new(0);
    t_dict *fdict = dict_new(0);
    if (!hyphen_hdict) hyphen_hdict = dict_new(0);
    if (!hyphen_fdict) hyphen_fdict = dict_new(0);
    dict_bind(hyphen_hdict, (t_pd *)hdict,
	      dict_key(hyphen_hdict, hostclass->c_name->s_name));
    dict_bind(hyphen_fdict, (t_pd *)fdict,
	      dict_key(hyphen_fdict, hostclass->c_name->s_name));
    if (baseclass)
    {  /* LATER make it safer */
	*baseclass = copybytes(hostclass, sizeof(*hostclass));
	(*baseclass)->c_patchable = 0;
    }
}

/* called from within both host and friend constructor, instead of pd_new() */
t_hyphen *hyphen_new(t_class *c, char *hostclassname)
{
    printf("hyphen_new \n");
    t_hyphen *x = (t_hyphen *)pd_new(c);
    hyphen_initialize(x, c, hostclassname);
    printf("ok %x\n", x);
    return (x);
}

/* called _only_ in case of a failure in <class>_new() routine,
   e.g. unsuccessful derivation, or to free a shadowy hyphen */
void hyphen_free(t_hyphen *x)
{
    pd_free((t_pd *)x);
}

void hyphen_initialize(t_hyphen *x, t_class *c, char *hostclassname)
{
    if (hostclassname)
    {  /* friend constructor */
	x->x_host = 0;
    }
    else
    {  /* host constructor */
	x->x_host = x;
	hostclassname = c->c_name->s_name;
    }
    x->x_hostname = x->x_friendname = 0;
    x->x_hdict = (t_dict *)dict_value(hyphen_hdict,
				      dict_key(hyphen_hdict, hostclassname));
    x->x_fdict = (t_dict *)dict_value(hyphen_fdict,
				      dict_key(hyphen_fdict, hostclassname));
    x->x_id = 0;
    x->x_tablesize = 0;
    x->x_basetable = 0;
}

/* These hooks are used for attaching/detaching all friend objects to/from
   their host (i.e. the host hyphen having the same name as the friends)
   during creation/destruction/renaming of that host.  They are passed to
   dict_forall(), which is our imperfect multicasting mechanism -- it calls
   these hooks for every friend hyphen registered under specified name. */
/* LATER make it safer (perhaps replace this mechanism with a true
   per class-group messaging). */
static int hyphen_attach_hook(t_pd *f, void *h)
{
#ifdef HYPHEN_DEBUG
    post("hyphen: attaching %s", class_getname(*f));
#endif
    ((t_hyphen *)f)->x_host = (t_hyphen *)h;
    return (1);
}

static int hyphen_detach_hook(t_pd *f, void *h)
{
    if (((t_hyphen *)f)->x_host == (t_hyphen *)h)
    {
#ifdef HYPHEN_DEBUG
	post("hyphen: detaching %s", class_getname(*f));
#endif
	((t_hyphen *)f)->x_host = 0;
    }
#ifdef HYPHEN_VERBOSE
    else post("hyphen warning: attempt to detach %s from unknown host",
	      class_getname(*f));
#endif
    return (1);
}

/* generic multicasting routine */
void hyphen_forallfriends(t_hyphen *x, t_dict_hook hook, void *hookarg)
{
    if (x == x->x_host)
    {
	if (x->x_fdict && x->x_friendname)
	    dict_forall(x->x_fdict, x->x_friendname, hook, hookarg);
    }
}

/* called by a friend hyphen to attach to a new host,
   or by a host hyphen to attach all its friends */
void hyphen_attach(t_hyphen *x, t_symbol *s)
{
    hyphen_detach(x);
    if (x->x_hdict && x->x_fdict && s != &s_)
    {
	/* map symbol from global namespace into namespace of hosts */
	x->x_hostname = dict_key(x->x_hdict, s->s_name);
	/* and into namespace of friends */
	x->x_friendname = dict_key(x->x_fdict, s->s_name);
	if (x == x->x_host)
	{
	    /* register ourselves */
	    dict_bind(x->x_hdict, (t_pd *)x, x->x_hostname);
	    /* multicast to all registered friends */
	    dict_forall(x->x_fdict, x->x_friendname, hyphen_attach_hook, x);
	}
	else {
	    /* identify our host */
	    x->x_host = (t_hyphen *)dict_value(x->x_hdict, x->x_hostname);
	    /* register ourselves */
	    dict_bind(x->x_fdict, (t_pd *)x, x->x_friendname);
	}
    }
}

/* called by a friend hyphen to detach from a host
   or by a host hyphen to detach all its friends */
void hyphen_detach(t_hyphen *x)
{
    if (x == x->x_host)
    {
	if (x->x_fdict && x->x_friendname)
	    dict_forall(x->x_fdict, x->x_friendname, hyphen_detach_hook, x);
	if (x->x_hdict && x->x_hostname)
	    dict_unbind(x->x_hdict, (t_pd *)x, x->x_hostname);
    }
    else {
	if (x->x_fdict && x->x_friendname)
	    dict_unbind(x->x_fdict, (t_pd *)x, x->x_friendname);
	x->x_host = 0;
    }
    x->x_hostname = x->x_friendname = 0;
}

/* called by a host hyphen to find another host of the same class */
t_hyphen *hyphen_findhost(t_hyphen *x, t_symbol *s)
{
    if (x->x_hdict && s != &s_)
    {
	/* map symbol from global namespace into namespace of hosts */
	t_symbol *hostname = dict_key(x->x_hdict, s->s_name);
	return ((t_hyphen *)dict_value(x->x_hdict, hostname));
    }
    return (0);
}

static void hyphen_basetable_init(t_hyphen *x)
{
    if (x->x_baseclass && !x->x_baseclass->c_patchable)
    {
	t_hyphen *base;
	int i;
	for (i = 0, base = x->x_basetable;
	     i < x->x_tablesize; i++,
	     base += x->x_baseclass->c_size)
	{
            printf("hyphen_basetable_init; i: %d, base: %x\n", i, base);
	    *(t_pd *)&base->x_ob = x->x_baseclass;
	    base->x_id = i;
	    base->x_self = x;
	    base->x_host = base;
	    base->x_hostname = base->x_friendname = 0;
	    base->x_hdict = x->x_hdict;
	    base->x_fdict = x->x_fdict;
	}
    }
    else bug("hyphen_basetable_init");
    printf("hyphen_basetable_init; ok\n");
}

/* Called from within friend hyphen constructor to allocate embedded
   base, make a `selflink' from base host to a derived friend (avoid
   pointer arithmetic), and to initialize the base. */
t_hyphen *hyphen_derive(t_hyphen *x, t_class *baseclass)
{
    return (hyphen_multiderive(x, baseclass, 1));
}

/* multibase version of the above */
t_hyphen *hyphen_multiderive(t_hyphen *x, t_class *baseclass, int tablesize)
{
    printf("hyphen_multiderive; baseclass: %s, tablesize %d\n", baseclass->c_name->s_name, tablesize);
    if (tablesize < 0) return (0);
    if (tablesize < 1) tablesize = 1;
    if (!(x->x_basetable = (t_hyphen *)getbytes(tablesize * baseclass->c_size))) {
        printf("hyphen_multiderive; getbytes failed\n");
	return (0);
    }
    printf("hyphen_multiderive; getbytes ok\n");
    x->x_baseclass = baseclass;
    x->x_tablesize = tablesize;
    hyphen_basetable_init(x);
    return (x->x_basetable);
}

t_hyphen *hyphen_resizebase(t_hyphen *x, int tablesize)
{
    if (!(x->x_basetable =
	  (t_hyphen *)resizebytes(x->x_basetable,
				  x->x_tablesize * x->x_baseclass->c_size,
				  tablesize * x->x_baseclass->c_size)))
	return (0);
    x->x_tablesize = tablesize;
    hyphen_basetable_init(x);
    return (x->x_basetable);
}

void hyphen_freebase(t_hyphen *x)
{
    if (x->x_basetable)
	freebytes(x->x_basetable, x->x_tablesize * x->x_baseclass->c_size);
}
