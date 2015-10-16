/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* midifile/binbuf interface, a prototype version */

#ifndef __MFBB_H__
#define __MFBB_H__

/* this structure is `derived' from t_squiter `base' */
/* LATER try to make it a more generic t_bbiter */
typedef struct _mfbb_iterator
{
    size_t         i_size;
    t_binbuf      *i_b;
    t_atom        *i_a;  /* current atom */
    t_squiterhook  i_hooks[SQUITER_NHOOKS];
    int  i_i;  /* current index, i.e. x->i_b->b_vec + x->i_i == x->i_a */
} t_mfbb_iterator;

/* midifile/binbuf interface */
int mfbb_read(t_binbuf *x, const char *filename, const char *dirname,
	      t_symbol *tts);
int mfbb_write(t_binbuf *x, const char *filename, const char *dirname,
	       t_symbol *tts);

/* binbuf manipulation */
t_mifi_stream *mfbb_make_stream(t_binbuf *x, t_squtt *tt, int flags);
void mfbb_merge_tracks(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt);
void mfbb_separate_tracks(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt);

#endif
