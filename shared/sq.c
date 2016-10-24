/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* squeak and squeal: sequencing utilities, a prototype version */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"

#if 1
#define SQ_VERBOSE
#if 0
#define SQ_DEBUG
#endif
#endif

/* #define SQUMPI_IGNORE */

#define SQUB_NALLOC     32
#define SQUMPI_NALLOC  (32 * sizeof(t_squmpo))
#define SQUAX_NALLOC   (32 * sizeof(t_squack))

#define SQUMPI_DEFAULT  500000  /* 120 bpm in microseconds per beat */

/* Arguments reqcount and elsize as in calloc, returns count */
size_t squb_checksize(void *buf, size_t reqcount, size_t elsize)
{
    t_squb *x = buf;
    size_t reqsize = reqcount * elsize;
    size_t newsize = x->b_bufsize;
    while (newsize < reqsize) newsize *= 2;
    if (newsize == x->b_bufsize)
	return (newsize);
#ifdef SQ_DEBUG
    post("need to resize buffer %x from %d to %d (requested size %d)",
	 (int)x, x->b_bufsize, newsize, reqsize);
#endif
    if (!(x->b_data = resizebytes(x->b_data, x->b_bufsize, newsize)) &&
	/* rather hopeless... */
	!(x->b_data = getbytes(newsize = SQUB_NALLOC * elsize)))
	newsize = 0;
    return ((x->b_bufsize = newsize) / elsize);  /* LATER do it right */
}

/* generic event */

/* tempo map */

/* comparison function used by qsort */
static int squmpi_compare(const void *tp1, const void *tp2)
{
    return (((t_squmpo *)tp1)->te_onset > ((t_squmpo *)tp2)->te_onset ? 1 : -1);
}

void squmpi_sort(t_sq *x)
{
    int i;
    t_squmpo *tp;
    qsort(x->s_tempomap, x->s_ntempi, sizeof(t_squmpo), squmpi_compare);
#if defined SQ_VERBOSE && ! defined SQ_DEBUG
    for (i = x->s_ntempi, tp = x->s_tempomap; i > 0 ; i--, tp++)
	post("tempo %d at %d", tp->te_value, (int)tp->te_onset);
#endif
}

t_squmpo *squmpi_add(t_sq *x)
{
    size_t count = x->s_ntempi + 1;
    t_squmpo *tep;
    if (squb_checksize(x->s_mytempi, count, sizeof(t_squmpo)) < count)
	return (0);
    tep = x->s_tempomap + x->s_ntempi++;
    squmpo_reset(tep);
    return (tep);
}

void squmpo_reset(t_squmpo *x)
{
    x->te_onset = 0;
    x->te_value = SQUMPI_DEFAULT;
}

/* track map */

t_squack *squax_add(t_sq *x)
{
    size_t count = x->s_ntracks + 2;  /* guard point */
    t_squack *trp;
    if (squb_checksize(x->s_mytracks, count, sizeof(t_squack)) < count)
	return (0);
    trp = x->s_trackmap + x->s_ntracks++;
    squack_reset(trp);
    return (trp);
}

void squack_reset(t_squack *x)
{
    x->tr_id = 0;  /* this is no-id */
    x->tr_nevents = 0;
    x->tr_name = 0;
    x->tr_head = 0;
}

/* generic iterator */

void *squiter_new(t_sq *x, size_t sz)
{
    if (x->s_myiter = getbytes(sz))
	x->s_myiter->i_size = sz;
    return (x->s_myiter);
}

/* routines to access iterator hooks (setting hooks is explicit only) */
t_squiter_seekhook squiter_seekhook(t_squiter *x)
{
    return (x ? (t_squiter_seekhook)x->i_hooks[SQUITER_SEEKHOOK] : 0);
}

t_squiter_getevehook squiter_getevehook(t_squiter *x)
{
    return (x ? (t_squiter_getevehook)x->i_hooks[SQUITER_GETEVEHOOK] : 0);
}

t_squiter_setevehook squiter_setevehook(t_squiter *x)
{
    return (x ? (t_squiter_setevehook)x->i_hooks[SQUITER_SETEVEHOOK] : 0);
}

t_squiter_gettimhook squiter_gettimhook(t_squiter *x)
{
    return (x ? (t_squiter_gettimhook)x->i_hooks[SQUITER_GETTIMHOOK] : 0);
}

t_squiter_settimhook squiter_settimhook(t_squiter *x)
{
    return (x ? (t_squiter_settimhook)x->i_hooks[SQUITER_SETTIMHOOK] : 0);
}

t_squiter_gettarhook squiter_gettarhook(t_squiter *x)
{
    return (x ? (t_squiter_gettarhook)x->i_hooks[SQUITER_GETTARHOOK] : 0);
}

t_squiter_settarhook squiter_settarhook(t_squiter *x)
{
    return (x ? (t_squiter_settarhook)x->i_hooks[SQUITER_SETTARHOOK] : 0);
}

/* time conversion */

/* Compute reusable coefficient, rather then repeatedly apply the formula.
   For smpte time:
   d msecs == (d / 1000.) secs == ((d * nframes * nticks) / 1000.) ticks
   or for metrical time:
   d msecs == (d * 1000.) usecs == ((d * 1000.) / tempo) beats
   == ((d * nticks * 1000.) / tempo) ticks
*/
/* LATER ntsc */
float sq_ticks2msecs(t_sq *x, uint32 tempo)
{
    if (x->s_nframes)
	return (1000. / (x->s_nframes * x->s_nticks));
    if (tempo <= 0)
	tempo = x->s_tempo;
    if (tempo <= 0)
	tempo = SQUMPI_DEFAULT;
    return (tempo / (x->s_nticks * 1000.));
}

float sq_msecs2ticks(t_sq *x, uint32 tempo)
{
    if (x->s_nframes)
	return (((x->s_nframes * x->s_nticks) / 1000.));
    if (!tempo)
	tempo = x->s_tempo;
    if (!tempo)
	tempo = SQUMPI_DEFAULT;
    return ((x->s_nticks * 1000.) / tempo);
}

/* track template */

/* The general form of a track template (aka target template) is
   <range>:<pattern>, where <range> is <first>:<last>, and <pattern> is
   <start><base>.  The three fields, <first>, <last>, and <start>, are
   always numeric (positive integers), while <base> has to be a name.
   Track template may be input in one of three ``input forms'': two-colon,
   one-colon, and colonless.  Full, four-field template has always two
   colons in it (<first>:<last>:<start><base>).

   In case of a two-colon form, missing <range> fields are filled with
   default values, which are 1 for <first>, <ntracks> (number of tracks
   containing midi events) for <last>, and "-" (dash) for <base>.
   Missing <start> is a special case (see below).

   Possible abbreviations of one-colon form are: <first>:, :<last>, and
   <first>:<last>, with defaults used for the remaining fields.  The
   colonless template may be either a number, which is substituted for
   both <first>, and <last>, or a symbol, which is used as <pattern>.

   There are three forms of a <start>-less <pattern>.  One, -<name>, is
   equivalent to <first>-<name>, i.e. it is <start><base> with <start>
   equal <first>, and <base> equal -<name>.  The other is a mere dash,
   and its meaning in writing is ``any track'', while in reading we
   either substitute a track name stored as meta-event, or a default
   "-track".  If <base> has no leading dash, and <start> is missing,
   then this is (the only) case of a ``constant target'' (merging)
   track template.  All other forms of <pattern> indicate a ``variable
   target'' track template.

   Reading: tracks preceding <first> and succeeding <last> are ignored;
   if <start> is missing, the target for all tracks is <base>; otherwise
   variable target <id><base> is used; <id> for track <first>
   is <start>; <id> is incremented for successive tracks.

   Writing: from all valid messages with target matching the <pattern>
   we write only those with <id>s in <range>.  If <start> is missing,
   <range> is ignored (unless <base> has a leading dash).
*/

t_squtt *squtt_new(void)
{
    t_squtt *x = (t_squtt *)getbytes(sizeof(*x));
    if (x) squtt_make(x, &s_);
    return (x);
}

void squtt_free(t_squtt *x)
{
    freebytes(x, sizeof(*x));
}

/* Make track template structure from track template symbol.
   Return start number if variable target, zero otherwise */
int squtt_make(t_squtt *x, t_symbol *tts)
{
    int buf[3], i, ncolons = 0;
    char *ptr;
    x->t_first = x->t_last = x->t_start = 0;
    x->t_default = 0;
    if (!tts || tts == &s_ || !tts->s_name[0])
	tts = gensym("-");
    x->t_given = tts;
    ptr = tts->s_name;
    for (i = 0; i < 3; i++)
    {
	buf[i] = 0;
	while (*ptr >= '0' && *ptr <= '9')
	    buf[i] = buf[i] * 10 + *ptr++ - '0';
	if (*ptr == ':' && ++ncolons < 3) ptr++;
    }
    if (ncolons >= 2)
    {
	x->t_first = buf[0];
	x->t_last = buf[1];
	x->t_start = buf[2];
    }
    else if (ncolons == 1)
    {
	x->t_first = buf[0];
	x->t_last = buf[1];
	x->t_start = 0;
    }
    else x->t_first = x->t_last = buf[0];

    if (!x->t_first) x->t_first = 1;
    if (!x->t_last)
	x->t_last = 0x7fffffff;
    else if (x->t_last < x->t_first)
	x->t_first = 0x7fffffff;  /* for parsing only, w/o loading/saving */

    if (*ptr)
    {
	x->t_base = gensym(ptr);
	if (*ptr == '-')
	{
	    if (!x->t_start) x->t_start = x->t_first;
	    if (!ptr[1]) x->t_default = gensym("-track");
	}
    }
    else {
	if (!x->t_start) x->t_start = x->t_first;
	x->t_base = gensym("-");
	x->t_default = gensym("-track");
    }

    return (x->t_start);
}

/* If name matches track template, return matching track id
   (or -1 if track has no id), otherwise return 0.  This was never
   used yet -- needs syncing to squtt_checkatom() and testing. */
int squtt_checkstring(t_squtt *x, char *name)
{
    if (x->t_default && !x->t_first && x->t_last == 0x7fffffff)
	/* if empty base and range, all targets are equivalent */
	return (-1);
    else if (x->t_start)
    {
	int track = 0;
	while (*name >= '0' && *name <= '9')
	    track = track * 10 + *name++ - '0';
	if (track >= x->t_start &&
	    track >= x->t_first && track <= x->t_last &&
	    (x->t_default || (*name && !strcmp(name, x->t_base->s_name))))
	    return (track);
    }
    else if (!strcmp(name, x->t_base->s_name))
	return (-1);
    return (0);
}

/* Like squtt_checkstring(), but more efficient for constant targets.
   This is called during binbuf parsing, i.e. during saving or separating
   tracks. */
int squtt_checksymbol(t_squtt *x, t_symbol *s)
{
    if (x->t_default && !x->t_first && x->t_last == 0x7fffffff)
	/* if empty base and range, all targets are equivalent */
	return (-1);
    else if (x->t_start)
    {
//	char *p = s->s_name;
	int track = 1;
//	while (*p >= '0' && *p <= '9')
//	    track = track * 10 + *p++ - '0';
//	if (track >= x->t_start &&
//	    track >= x->t_first && track <= x->t_last &&
//	    (x->t_default || (*p && !strcmp(p, x->t_base->s_name))))
	    return (track);
    }
    else if (s == x->t_base)
	return (-1);
    return (0);
}

int squtt_checkatom(t_squtt *x, t_atom *ap)
{
    if (ap->a_type == A_SYMBOL)
	return (squtt_checksymbol(x, ap->a_w.w_symbol));
    else return (0);
}

t_symbol *squtt_makesymbol(t_atom *ap)
{
    char buf[80];
    if (ap->a_type == A_SYMBOL) return (ap->a_w.w_symbol);
    else if (ap->a_type == A_FLOAT)
    {
	int i = (int)ap->a_w.w_float;
	if (i > 0)
	{
	    sprintf(buf, "%d", i);
	    return (gensym(buf));
	}
    }
    atom_string(ap, buf, 80);
    error("bad track template: %s", buf);
    return (0);
}

/* transform onset ticks into delta msecs */
void sq_fold_time(t_sq *x)
{
    t_squiter *it = x->s_myiter;
    t_squiter_seekhook seekhook = squiter_seekhook(it);
    t_squiter_gettimhook gethook = squiter_gettimhook(it);
    t_squiter_settimhook sethook = squiter_settimhook(it);
    int i, incr, nevents = x->s_nevents;

    if (!it) return;
    seekhook(it, 0);
    if (x->s_nframes)
    {
	float coef = sq_ticks2msecs(x, 0);
	t_float lasttime = 0;
	for (i = 0; i < nevents; i++)
	{
	    t_float thistime = gethook(it, 0) * coef;
	    sethook(it, thistime - lasttime, &incr);  /* back to delta time */
	    lasttime = thistime;
	    if (!incr)
	    {
		post("sequence folding error: bad iterator");
		break;
	    }
	}
    }
    else  /* apply tempomap */
    {
	float coef = sq_ticks2msecs(x, SQUMPI_DEFAULT);
	int ntempi = x->s_ntempi;
	t_float lasttime = 0, thistime = 0;
	t_float temposince = 0;
	t_float tempoonset = 0;
	int tempondx = 0;
	for (i = 0; i < nevents; i++)
	{
	    t_float thisonset = gethook(it, 0);
	    t_float nexttempoonset;
#ifdef SQUMPI_IGNORE
	    thistime = thisonset * coef;
#else
	    while (tempondx < ntempi  /* LATER consider using guard point */
		   && (nexttempoonset = x->s_tempo_onset(tempondx)) < thisonset)
	    {
		temposince += (nexttempoonset - tempoonset) * coef;
		tempoonset = nexttempoonset;
		coef = sq_ticks2msecs(x, x->s_tempo_value(tempondx));
		tempondx++;
	    }
	    thistime = temposince + (thisonset - tempoonset) * coef;
#endif
 	    if (thistime < lasttime)
	    {
#ifdef SQ_DEBUG
		/* FIXME under msvc -- horror! */
		if (thistime != lasttime)
		    post("ndx %d, this-last (%x-%x) %.15f, \
tix %.9f, tsince %.9f, ttix %.9f, coef %.9f",
			 tempondx, (int)thistime, (int)lasttime, thistime - lasttime,
			 thisonset, temposince, tempoonset, coef);
#endif
		thistime = lasttime;
	    }
	    sethook(it, thistime - lasttime, &incr);  /* back to delta time */
	    lasttime = thistime;
	    if (!incr)
	    {
		post("sequence folding error: bad iterator");
		break;
	    }
	}
    }
}

/* transform delta msecs into onset msecs */
/* LATER add an option (or a separate function) for obtaining ticks
   (according to tempomap) */
void sq_unfold_time(t_sq *x)
{
    t_squiter *it = x->s_myiter;
    t_squiter_seekhook seekhook = squiter_seekhook(it);
    t_squiter_gettimhook gethook = squiter_gettimhook(it);
    t_squiter_settimhook sethook = squiter_settimhook(it);
    int i, incr, nevents = x->s_nevents;
    t_float thisonset = 0;

    if (!it) return;
    seekhook(it, 0);
    for (i = 0; i < nevents; i++)
    {
	thisonset += gethook(it, 0);
	sethook(it, thisonset, &incr);
	if (!incr)
	{
	    post("sequence unfolding error: bad iterator");
	    break;
	}
    }
}

void sq_reset(t_sq *x)
{
    x->s_eof = 0;
    x->s_newtrack = 0;
    x->s_anapass = 1;
    x->s_fp = 0;
    x->s_time = 0;
    x->s_tempo = SQUMPI_DEFAULT;
    x->s_track = 0;
}

t_sq *sq_new(void)
{
    t_sq *x = (t_sq *)getbytes(sizeof(*x));
    if (!x)
	goto constructorfailure;

    /* these two are allocated in derived structure constructor */
    x->s_myiter = 0;
    x->s_auxeve = 0;

    if (!(x->s_mytempi = getbytes(sizeof(t_squmpi))))
	goto constructorfailure;
    if (!(x->s_tempomap = getbytes(x->s_mytempi->m_bufsize = SQUMPI_NALLOC)))
	goto constructorfailure;
    x->s_ntempi = 0;
    if (!(x->s_mytracks = getbytes(sizeof(t_squax))))
	goto constructorfailure;
    if (!(x->s_trackmap = getbytes(x->s_mytracks->m_bufsize = SQUAX_NALLOC)))
	goto constructorfailure;
    x->s_ntracks = 0;

    x->s_auto = 0;
    x->s_format = 0;
    x->s_nticks = 192;  /* LATER parametrize this somehow */
    x->s_nframes = 0;

    sq_reset(x);
    return (x);
constructorfailure:
    if (x) sq_free(x);
    return (0);
}

void sq_free(t_sq *x)
{
    if (x->s_mytempi)
    {
	if (x->s_tempomap)
	    freebytes(x->s_tempomap, x->s_mytempi->m_bufsize);
	freebytes(x->s_mytempi, sizeof(t_squmpi));
    }
    if (x->s_mytracks)
    {
	if (x->s_trackmap)
	    freebytes(x->s_trackmap, x->s_mytracks->m_bufsize);
	freebytes(x->s_mytracks, sizeof(t_squax));
    }
    if (x->s_myiter)
	freebytes(x->s_myiter, x->s_myiter->i_size);
    freebytes(x, sizeof(*x));
}
