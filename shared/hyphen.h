/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#ifndef __HYPHEN_H__
#define __HYPHEN_H__

#include "dict.h"

typedef struct _hyphen
{
    t_object         x_ob;
    int              x_id;    /* selfindex into basetable element */
    struct _hyphen  *x_self;  /* selflink from embedded part to friend proper */
    struct _hyphen  *x_host;  /* x == x_host iff host hyphen */
    t_symbol        *x_hostname;
    t_symbol        *x_friendname;
    t_dict          *x_hdict;
    t_dict          *x_fdict;
    t_class         *x_baseclass;
    int              x_tablesize;
    struct _hyphen  *x_basetable;  /* table of embedded bases */
} t_hyphen;

void hyphen_setup(t_class *hostclass, t_class **baseclass);
t_hyphen *hyphen_new(t_class *c, char *hostclassname);
void hyphen_free(t_hyphen *x);
void hyphen_initialize(t_hyphen *x, t_class *c, char *hostclassname);
void hyphen_attach(t_hyphen *x, t_symbol *s);
void hyphen_detach(t_hyphen *x);
t_hyphen *hyphen_findhost(t_hyphen *x, t_symbol *s);
t_hyphen *hyphen_derive(t_hyphen *x, t_class *baseclass);
t_hyphen *hyphen_multiderive(t_hyphen *x, t_class *baseclass, int tablesize);
t_hyphen *hyphen_resizebase(t_hyphen *x, int tablesize);
void hyphen_freebase(t_hyphen *x);
void hyphen_forallfriends(t_hyphen *x, t_dict_hook hook, void *hookarg);

#endif
