/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* midifile/binbuf interface, a prototype version */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "bifi.h"
#include "mifi.h"
#include "mfbb.h"

#if 1
#define MFBB_VERBOSE
#if 0
#define MFBB_DEBUG
#endif
#endif

#define MFBB_PARTICLE_SIZE  7  /* number of atoms used to store one event */

/* LATER use access methods (guard against possible future t_binbuf changes) */
struct _binbuf
{
    int b_n;
    t_atom *b_vec;
};

/* a callback routine to process current event during writing */
typedef int (*t_mfbb_parsinghook)(t_mifi_stream *x,
				  /* LATER maybe pack extra args to mifi_stream */
				  t_mifi_event *e, int track, t_symbol *tname);

/* First pass of reading: analyse and allocate. */
static int mfbb_read_pass1(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt)
{
    int result = mifi_read_analyse(stp, tt);
    int natoms = stp->s_nevents * MFBB_PARTICLE_SIZE;
    t_atom *ap;
    if (result == MIFI_READ_EOF)
    {
	freebytes(x->b_vec, x->b_n * sizeof(t_atom));
	if (ap = getbytes(natoms * sizeof(t_atom)))
	{
	    x->b_n = natoms;
	    x->b_vec = ap;
	}
	else result = MIFI_READ_FATAL;  /* warning is in getbytes() */
    }
    return (result);
}

/* Second pass of reading: read data into buffers. */
static int mfbb_read_pass2(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt)
{
#ifdef MFBB_VERBOSE
    post("filling %d-atom binbuf from %d tracks out of %d channel-tracks (%d total)",
	 stp->s_nevents * MFBB_PARTICLE_SIZE,
	 stp->s_ntracks, stp->s_alltracks, stp->s_hdtracks);
#endif
    return (mifi_read_doit(stp, tt));
}

/* standard event handling hooks passed to binbuf parsing routine */

static int mfbb_format0_hook(t_mifi_stream *x, t_mifi_event *e,
			     int track, t_symbol *tname)
{
    return (mifi_write_event(x, e));
}

/* LATER sort out track == -1 case */
static int mfbb_format1_hook(t_mifi_stream *x, t_mifi_event *e,
			     int track, t_symbol *tname)
{
    return (track == x->s_trackid ? mifi_write_event(x, e) : -1);
}

/* default hook used for updating the counts */
static int mfbb_analyse_hook(t_mifi_stream *x, t_mifi_event *e,
			     int track, t_symbol *tname)
{
    if (!track)
    {
	post("binbuf analysis bug: track zero");
	return (0);
    }
    if (track < 0)
    {
	if (x->s_ntracks > 1)
	{
	    post("binbuf analysis bug: single/multitrack modes clash");
	    return (0);
	}
	x->s_ntracks = 1;
    }
    else {
	int i = x->s_ntracks;
	t_squack *trp = x->s_trackmap;
	while (i-- > 0)
	{
	    if (trp->tr_id == track)
	    {
		trp->tr_nevents++;
		break;
	    }
	    trp++;
	}
	if (i < 0)
	{
	    if (!(trp = squax_add(x)))
		return (0);
	    trp->tr_id = track;
	    trp->tr_name = tname;
	    trp->tr_nevents = 1;
	}
    }
    x->s_nevents++;
    return (1);
}

static int mfbb_parse_status(t_atom *ap, t_mifi_event *evp)
{
    float f;
    if (ap->a_type == A_FLOAT && (f = ap->a_w.w_float) >= 128 && f < 240)
    {
	evp->e_status = (uchar)f & 0xf0;  /* we forgive this... */
	return (1);
    }
    return (0);

}

static int mfbb_parse_data(t_atom *ap, t_mifi_event *evp, int n)
{
    float f;
    if (ap->a_type == A_FLOAT && (f = ap->a_w.w_float) >= 0 && f <= 127)
    {
	evp->e_data[n] = (uchar)f;
	return (1);
    }
    return (0);
}

/* LATER handle more channels (multiple ports) */
/* LATER handle default track channel */
static int mfbb_parse_channel(t_atom *ap, t_mifi_event *evp)
{
    float f;
    if (ap->a_type == A_FLOAT && (f = ap->a_w.w_float) >= 0 && f <= 16)
    {
	if ((evp->e_channel = (uchar)f) > 0) evp->e_channel--;
	return (1);
    }
    return (0);

}

/* binbuf parsing routine */
/* LATER analyse various cases of mixed (midi/nonmidi) binbufs and try to
   find a better way of adjusting time in such cases */
static int mfbb_parse(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt,
		      t_mfbb_parsinghook hook)
{
    t_mifi_event *evp = stp->s_auxeve;
    t_mfbb_parsinghook thehook = (hook ? hook : mfbb_analyse_hook);
    int natoms = x->b_n;
    t_atom *ap= x->b_vec;
    uint32 thisticks = 0, pastticks = 0;
    int track, hookresult;
    t_symbol *tname = 0;

    if (!hook)
    {
	/* LATER make sure we are called after mifi_stream_new()
	   or after reinitialization */
    }
    while (natoms >= MFBB_PARTICLE_SIZE)
    {
	t_atom *ap1 = ap;
	t_float f, delaytime = 0;
	if (ap1->a_type != A_FLOAT || (delaytime = ap1->a_w.w_float) < 0)
	    goto nextmessage;
	ap1++;
#if 1  /* assume folded time */
	thisticks += (uint32)(delaytime * stp->s_timecoef);
#else  /* but keep unfolded version ready */
	thisticks = (uint32)(delaytime * stp->s_timecoef);
#endif
	if (!(track = squtt_checkatom(tt, ap1)))
	    goto nextmessage;
	tname = ap1->a_w.w_symbol;
	ap1++;
	if (mfbb_parse_status(ap1, evp)) ap1++;
	else goto nextmessage;
	if (mfbb_parse_data(ap1, evp, 0)) ap1++;
	else goto nextmessage;
	if (MIFI_ONE_DATABYTE(evp->e_status))
	{
	    if (mfbb_parse_channel(ap1, evp)) ap1++;
	    else goto nextmessage;
	    evp->e_data[1] = 0, ap1++;  /* accept this being just anything... */
	}
	else {
	    if (mfbb_parse_data(ap1, evp, 1)) ap1++;
	    else goto nextmessage;
	    if (mfbb_parse_channel(ap1, evp)) ap1++;
	    else goto nextmessage;
	}
	if (ap1->a_type != A_SEMI)  /* ...but this is required */
	    goto nextmessage;

	evp->e_delay = thisticks - pastticks;
	if (!(hookresult = thehook(stp, evp, track, tname)))
	    return (0);
	if (hookresult > 0) pastticks = thisticks;

	natoms -= MFBB_PARTICLE_SIZE;
	ap += MFBB_PARTICLE_SIZE;
	continue;

    nextmessage:
#ifdef MFBB_DEBUG
	if (!hook)
	{  /* print this only once, i.e. during a default (analysis) pass */
	    startpost("skip"); postatom(1, ap);
	    poststring("with"); postatom(6, ap+1);
	    poststring("bad"); postatom(1, ap1);
	    endpost();
	}
#endif
	while (natoms-- > MFBB_PARTICLE_SIZE && (ap++)->a_type != A_SEMI);
    }
    return (1);
}

static void mfbb_seekhook(t_mfbb_iterator *it, int offset)
{
    if (offset >= 0) {
	it->i_a = it->i_b->b_vec + offset * MFBB_PARTICLE_SIZE;
	it->i_i = offset;
    }
}

/* LATER put mfbb_parse() functionality here */
static void mfbb_getevehook(t_mfbb_iterator *it, t_mifi_event *evp, int *incr)
{
    if (it->i_i < it->i_b->b_n)
    {
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
    }
    else if (incr) *incr = 0;
}

static void mfbb_setevehook(t_mfbb_iterator *it, t_mifi_event *evp, int *incr)
{
    if (it->i_i < it->i_b->b_n)
    {
	t_atom *ap = it->i_a;
	SETFLOAT(ap, evp->e_delay), ap++;
	ap++;  /* target is set in a separate mfbb_settarhook() call */
	SETFLOAT(ap, evp->e_status), ap++;
	SETFLOAT(ap, evp->e_data[0]), ap++;
	if (MIFI_ONE_DATABYTE(evp->e_status))
	{
	    SETFLOAT(ap, evp->e_channel + 1), ap++;
	    SETFLOAT(ap, 0), ap++;
	}
	else {
	    SETFLOAT(ap, evp->e_data[1]), ap++;
	    SETFLOAT(ap, evp->e_channel + 1), ap++;
	}
	SETSEMI(ap);
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
    }
    else if (incr) *incr = 0;
}

static t_float mfbb_gettimhook(t_mfbb_iterator *it, int *incr)
{
    if (it->i_i < it->i_b->b_n && it->i_a->a_type == A_FLOAT)
    {
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
	return (it->i_a->a_w.w_float);
    }
    else if (incr) *incr = 0;
    return (0);
}

static void mfbb_settimhook(t_mfbb_iterator *it, t_float v, int *incr)
{
    if (it->i_i < it->i_b->b_n)
    {
	it->i_a->a_w.w_float = v;
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
    }
    else if (incr) *incr = 0;
}

static t_symbol *mfbb_gettarhook(t_mfbb_iterator *it, int *incr)
{
    if (it->i_i < it->i_b->b_n && it->i_a->a_type == A_SYMBOL)
    {
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
	return (it->i_a[1].a_w.w_symbol);
    }
    else if (incr) *incr = 0;
    return (0);
}

static void mfbb_settarhook(t_mfbb_iterator *it, t_symbol *s, int *incr)
{
    if (it->i_i < it->i_b->b_n)
    {
	SETSYMBOL(it->i_a+1, s);
	if (incr)
	{
	    it->i_a += MFBB_PARTICLE_SIZE;
	    it->i_i += MFBB_PARTICLE_SIZE;
	    *incr = 1;
	}
    }
    else if (incr) *incr = 0;
}

static int mfbb_make_iterator(t_binbuf *x, t_mifi_stream *stp)
{
    t_mfbb_iterator *it = squiter_new(stp, sizeof(t_mfbb_iterator));
    if (it)
    {
	it->i_b = x;
	it->i_a = x->b_vec;
	it->i_i = 0;
	it->i_hooks[SQUITER_SEEKHOOK] = (t_squiterhook)mfbb_seekhook;
	it->i_hooks[SQUITER_GETEVEHOOK] = (t_squiterhook)mfbb_getevehook;
	it->i_hooks[SQUITER_SETEVEHOOK] = (t_squiterhook)mfbb_setevehook;
	it->i_hooks[SQUITER_GETTIMHOOK] = (t_squiterhook)mfbb_gettimhook;
	it->i_hooks[SQUITER_SETTIMHOOK] = (t_squiterhook)mfbb_settimhook;
	it->i_hooks[SQUITER_GETTARHOOK] = (t_squiterhook)mfbb_gettarhook;
	it->i_hooks[SQUITER_SETTARHOOK] = (t_squiterhook)mfbb_settarhook;
	return (1);
    }
    else return (0);
}

/* public interface */

/* LATER clean up the flags */
t_mifi_stream *mfbb_make_stream(t_binbuf *x, t_squtt *tt, int flags)
{
    t_mifi_stream *stp = 0;
    if (!(stp = mifi_stream_new()) ||
        !mfbb_make_iterator(x, stp))
	goto makefailed;
    if (flags)
    {
	if (!mfbb_parse(x, stp, tt, 0))
	    goto makefailed;
    }
    return (stp);
makefailed:
    if (stp) mifi_stream_free(stp);
    return (0);
}

/* comparison functions used by qsort (assume unfolded time) */
/* LATER replace qsort with some more efficient merging algorithm (maybe use
   semi-atoms to store links?) */
static int mfbb_compare_particles(const void *ap1, const void *ap2)
{
    return (((t_atom *)ap1)->a_w.w_float > ((t_atom *)ap2)->a_w.w_float ? 1 : -1);
}

static t_squtt *tartemhack;
static int mfbb_decompare_particles(const void *ap1, const void *ap2)
{
    int track1, track2;
    if (!(track2 = squtt_checkatom(tartemhack, (t_atom *)ap2))) return (-1);
    if (!(track1 = squtt_checkatom(tartemhack, (t_atom *)ap1))) return (1);
    if (track1 < track2) return (-1);
    if (track1 > track2) return (1);
    return (((t_atom *)ap1)->a_w.w_float > ((t_atom *)ap2)->a_w.w_float ? 1 : -1);
}

/* track interleaving */
void mfbb_merge_tracks(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt)
{
    tartemhack = tt;
    qsort(x->b_vec, stp->s_nevents, MFBB_PARTICLE_SIZE * sizeof(t_atom),
	  mfbb_compare_particles);
}

/* track demultiplexing */
void mfbb_separate_tracks(t_binbuf *x, t_mifi_stream *stp, t_squtt *tt)
{
    tartemhack = tt;
    qsort(x->b_vec, stp->s_nevents, MFBB_PARTICLE_SIZE * sizeof(t_atom),
	  mfbb_decompare_particles);
}

/* This is to be called in a qlist reading routine.

   Note, that rather than extending qlist_read(), we may plug midifile
   reading into binbuf_read_via_path().  Then, of course, someone could be
   tempted to send ``read <filename> mf'' to a data-canvas, producing an
   error (just as with any other data-incompatible file), which probably
   is ok...  Or, perhaps there is some reasonable mapping from midifile to
   data-arrays?

   Any channel event is stored in 7 atoms: delay, target, status, first
   databyte, second databyte (or channel if program or monopressure),
   channel (or zero, if program or monopressure), and semi.  We keep
   number of atoms constant 1) to simplify handling of messages sent by
   qlists in Pd patches 2) to enable inplace sorting of events during
   merging of tracks.  Hopefully one extra atom for every monopressure
   is not a terrible waste... or is it?
*/
int mfbb_read(t_binbuf *x, const char *filename, const char *dirname,
	      t_symbol *tts)
{
    t_mifi_stream *stp = 0;
    int result = 1;  /* expecting failure ;-) */
    t_squtt tartem;
    squtt_make(&tartem, tts);

    if (!(stp = mfbb_make_stream(x, &tartem, 0)) ||
	!mifi_read_start(stp, filename, dirname))
	goto readfailed;
    if (stp->s_nframes)
	post("midifile (format %d): %d tracks, %d ticks (%d smpte frames)",
	     stp->s_format, stp->s_hdtracks, stp->s_nticks, stp->s_nframes);
    else
	post("midifile (format %d): %d tracks, %d ticks per beat",
	     stp->s_format, stp->s_hdtracks, stp->s_nticks);

    if ((result = mfbb_read_pass1(x, stp, &tartem)) != MIFI_READ_EOF ||
	!mifi_read_restart(stp) ||
	(result = mfbb_read_pass2(x, stp, &tartem)) != MIFI_READ_EOF)
	goto readfailed;

    /* we sort tempomap in case tempo-events were scattered across tracks */
    squmpi_sort(stp);
    mfbb_merge_tracks(x, stp, &tartem);
    sq_fold_time(stp);

#ifdef MFBB_VERBOSE
    post("finished reading %d events from midifile", stp->s_nevents);
#endif
    result = 0;  /* success */
readfailed:
    if (stp)
    {
	mifi_read_end(stp);
	mifi_stream_free(stp);
    }
    return (result);
}

/* This is to be called in a qlist writing routine.

   Midifile format is specified through `target template symbol' argument (tts):
   format 1 is used in case of a variable target, otherwise it is format 0.
*/
int mfbb_write(t_binbuf *x, const char *filename, const char *dirname,
	       t_symbol *tts)
{
    t_mifi_stream *stp = 0;
    int result = 1;  /* failure */
    t_squtt tartem;
    squtt_make(&tartem, tts);

    if (!(stp = mfbb_make_stream(x, &tartem, 1)))
	goto writefailed;
    if (stp->s_ntracks < 1)
    {  /* avoid writing empty files */
	post("request to write empty midifile ignored");
	return (0);
    }
    stp->s_hdtracks = stp->s_ntracks;
    stp->s_format = tartem.t_start &&  /* variable target */
	!(stp->s_hdtracks == 1 && tartem.t_default) ? 1 : 0;
    if (!mifi_write_start(stp, filename, dirname))
	goto writefailed;

    if (stp->s_format)
    {
	int i = stp->s_hdtracks;
	t_squack *trp = stp->s_trackmap;
	while (i-- > 0)
	{
	    if (!mifi_write_start_track(stp))
		goto writefailed;
	    if (trp->tr_name)
	    {
		mifi_event_settext(stp->s_auxeve, MIFI_META_TRACKNAME,
				   trp->tr_name->s_name);
		if (!mifi_write_event(stp, stp->s_auxeve))
		    goto writefailed;
	    }
	    if (!mfbb_parse(x, stp, &tartem, mfbb_format1_hook) ||
		!mifi_write_adjust_track(stp, 0))
		goto writefailed;
	    trp++;
	}
    }
    else {
	mifi_event_settext(stp->s_auxeve, MIFI_META_TRACKNAME,
			   tartem.t_default ? "1-track" : tartem.t_base->s_name);
	if (!mifi_write_start_track(stp) ||
	    !mifi_write_event(stp, stp->s_auxeve) ||
	    !mfbb_parse(x, stp, &tartem, mfbb_format0_hook) ||
	    !mifi_write_adjust_track(stp, 0))
	    goto writefailed;
    }

    result = 0;  /* success */
writefailed:
    if (stp)
    {
	mifi_write_end(stp);
	mifi_stream_free(stp);
    }
    return (result);
}
