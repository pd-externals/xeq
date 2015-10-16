/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* URGENT:
   - derivation api: make it simple and tidy

   IN 0.2:
   - design a xeq~ microsequencer prototype
   - delta/tempo vector (d = d0*tempo0 + d1*tempo1...)
   - xeq_vexing sequence to multiple arrays mapper
   - xeq_data fully editable
   - event duration, if there is a clean way
   - coll-like positioning by event/note number (`locate event', `locate note')

   LATER:
   - make xeq_polyparse a multihost friend
   - multi-trigger auto mode (delta-less events need explicit triggering)
   - optional unfolded positioning and editing
   - smoothed positioning: timespan argument, attempt to make acc./ret.
   - better searching of midi events
   - `merge' method (sorting version of addclone)
   - `freeze' pp
   - clean up and debug saving feature
   - find a better way of plugging xeq features into the friends
   - synchronizing code of a polybase class with a singlebase version

   CONSIDER:
   - midionly flag
   - another friend: xeq_play (midiout, nonmidiout and bang outlets)
   - another friend: xeq_send (index and bang outlets)
   - another friend: xeq_tracks ({midiout}_n and bang outlets)

   BUGS: (plenty)
   - order dependence in case of indirect hyphenation
*/

#include <stdio.h>
#include <string.h>
#ifdef UNIX
#include <unistd.h>
#endif
#ifdef NT
#include <io.h>
#endif

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "bifi.h"
#include "mifi.h"
#include "mfbb.h"
#include "hyphen.h"
#include "text.h"
#include "xeq.h"
#include "build_counter"

#if 1
#define XEQ_VERBOSE
#if 0
#define XEQ_DEBUG
#endif
#endif

static t_class *xeq_class;
static t_class *xeq_base_class;

/* GUI EXTENSIONS */

static void xeq_gui_defs(void)
{
    /* Generic window definition.  Ideally, some variant of
       what is defined below should find its way into pd.tk */
    sys_gui("proc xeq_window {name geometry title contents} {\n");
    sys_gui(" if {[winfo exists $name]} {\n");
    sys_gui("  $name.text delete 1.0 end\n");
    sys_gui(" } else {\n");
    sys_gui("  toplevel $name\n");
    sys_gui("  wm title $name $title\n");
    sys_gui("  wm geometry $name $geometry\n");
    sys_gui("  text $name.text -relief raised -bd 2 \\\n");
    sys_gui("   -font -*-courier-medium--normal--12-* \\\n");
    sys_gui("   -yscrollcommand \"$name.scroll set\" -background lightgrey\n");
    sys_gui("  scrollbar $name.scroll -command \"$name.text yview\"\n");
    sys_gui("  pack $name.scroll -side right -fill y\n");
    sys_gui("  pack $name.text -side left -fill both -expand 1\n");
#if 0
    sys_gui("  raise $name\n");
#endif
    sys_gui(" }\n");
    sys_gui(" $name.text insert end $contents\n");
    sys_gui("}\n");

    sys_gui("proc xeq_window_ok {name} {\n");
    sys_gui(" if {[winfo exists $name]} {\n");
    sys_gui("  set ii [$name.text index [concat end - 1 lines]]\n");
    sys_gui("  pd [concat $name.editok clear \\;]\n");
    sys_gui("  for {set i 1} \\\n");
    sys_gui("   {[$name.text compare $i.end < $ii]} \\\n");
    sys_gui("  	{incr i 1} {\n");
    sys_gui("   set lin [$name.text get $i.0 $i.end]\n");
    sys_gui("   if {$lin != \"\"} {\n");
    sys_gui("    regsub -all \\; $lin \"  _semi_ \" tmplin\n");
    sys_gui("    regsub -all \\, $tmplin \"  _comma_ \" lin\n");
    sys_gui("    pd [concat $name.editok addline $lin \\;]\n");
    sys_gui("   }\n");
    sys_gui("  }\n");
    sys_gui(" }\n");
    sys_gui("}\n");
}

static char wname[MAXPDSTRING];

static void xeq_window(t_xeq *x, char *title, char *contents)
{
    int width = 600, height = 340;
    sprintf(wname, ".%x", (int)x);
    if (!title) title = "xeq contents";
    sys_vgui("xeq_window %s %dx%d {%s} {%s}\n",
	     wname, width, height, title, contents);
}

static void xeq_window_ok(t_xeq *x)
{
    sprintf(wname, ".%x", (int)x);
    sys_vgui("xeq_window_ok %s\n", wname);
}

/* to be called only immediately after xeq_window(),
   before there is any chance of window being closed */
static void xeq_window_append(t_xeq *x, char *contents)
{
    sprintf(wname, ".%x", (int)x);
    sys_vgui("%s.text insert end {%s}\n", wname, contents);
}

static void xeq_window_bind(t_xeq *x)
{
    sprintf(wname, ".%x.editok", (int)x);
    pd_bind((t_pd *)x, gensym(wname));
}

static void xeq_window_unbind(t_xeq *x)
{
    sprintf(wname, ".%x.editok", (int)x);
    pd_unbind((t_pd *)x, gensym(wname));
}

/* HELPERS */

static void xeq_noteons_clear(t_xeq *x)
{
    memset(x->x_noteons, (unsigned char)-1, sizeof(x->x_noteons));
}

int xeq_listparse(int argc, t_atom *argv,
		  int *statusp, int *channelp, int *data1p, int *data2p)
{
    *statusp = (int)argv++->a_w.w_float;
    *data2p = -1;
    if (--argc < 2 || argv->a_type != A_FLOAT) goto notmidi;
    *data1p = (int)argv++->a_w.w_float;
    if (*data1p < 0 || *data1p > 127 || argv->a_type != A_FLOAT) goto notmidi;
    switch (*statusp)
    {
    case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0:
	*data2p = (int)argv++->a_w.w_float;
	if (*data2p < 0 || *data2p > 127 || argv->a_type != A_FLOAT) goto notmidi;
    case 0xc0: case 0xd0:
	*channelp = (int)argv->a_w.w_float;
	if (*channelp <= 0 || *channelp > 16) goto notmidi;
	(*channelp)--;
	break;
    default: goto notmidi;
    }
    return (1);
notmidi:
#ifdef XEQ_DEBUG
    post("xeq: nonmidi list message");
#endif
    return (0);
}

/* XEQ LOCATOR */

/* standard locator names (to speed up message parsing a little) */
static t_symbol *xeq_s_auto, *xeq_s_step,
    *xeq_s_bloop, *xeq_s_eloop,
    *xeq_s_bedit, *xeq_s_eedit;

static void xeq_locator_setup(void)
{
    xeq_s_auto = gensym("auto");
    xeq_s_step = gensym("step");
    xeq_s_bloop = gensym("bloop");
    xeq_s_eloop = gensym("eloop");
    xeq_s_bedit = gensym("bedit");
    xeq_s_eedit = gensym("eedit");
}

t_xeqlocator *xeq_whichloc(t_xeq *x, t_symbol *s)
{
    t_xeqlocator *loc = 0;
    if (s == xeq_s_auto) loc = &x->x_autoit.i_playloc;
    else if (s == xeq_s_step) loc = &x->x_stepit.i_playloc;
    else if (s == xeq_s_bloop) loc = &x->x_autoit.i_blooploc;
    else if (s == xeq_s_eloop) loc = &x->x_autoit.i_elooploc;
    else if (s == xeq_s_bedit) loc = &x->x_beditloc;
    else if (s == xeq_s_eedit) loc = &x->x_eeditloc;
    return (loc);
}

static int xeqlocator_lookatfirst(t_xeqlocator *x)
{
    t_atom *ap;
    if (!x->l_binbuf || (x->l_natoms = binbuf_getnatom(x->l_binbuf)) <= 0)
	return (XEQ_FAIL_EMPTY);
    ap = x->l_firstatom = binbuf_getvec(x->l_binbuf);
    x->l_atprevious = -1;
    x->l_atdelta = -1;
    for (x->l_atnext = 0; x->l_atnext < x->l_natoms; x->l_atnext++, ap++)
    {
	switch (ap->a_type)
	{
	case A_FLOAT:
	    if (x->l_atdelta < 0)
	    {
		if ((x->l_delta = ap->a_w.w_float) < 0)
		    x->l_delta = 0;
		x->l_atdelta = x->l_atnext;
	    }
	    break;
	case A_SYMBOL:
	    x->l_when = x->l_delta;
	    return (XEQ_FAIL_OK);
	case A_SEMI:
	    break;
	default:
	    return (XEQ_FAIL_CORRUPT);
	}
    }
    return (XEQ_FAIL_CORRUPT);
}

/* look at target symbol of next event */
/* LATER sort out semi/comma rules and check again... */
static int xeqlocator_lookatnext(t_xeqlocator *x)
{
    t_atom *ap = x->l_firstatom + x->l_atnext;
    int checkdelay = 0, checktarget = 0;
    x->l_atprevious = x->l_atnext;
    for (; x->l_atnext < x->l_natoms; x->l_atnext++, ap++)
    {
	if (checkdelay && ap->a_type == A_FLOAT)
	{
	    if ((x->l_delta = ap->a_w.w_float) < 0)
		x->l_delta = 0;
	    x->l_atdelta = x->l_atnext;
	    checkdelay = 0;
	    checktarget = 1;
	}
	else if (checktarget && ap->a_type == A_SYMBOL)
	{
	    x->l_when += x->l_delta;
	    return (XEQ_FAIL_OK);
	}
	else checkdelay = checktarget = ap->a_type == A_SEMI;
    }
    return (XEQ_FAIL_EOS);
}

static int xeqlocator_lookatindex(t_xeqlocator *x, int ndx)
{
    int result = xeqlocator_lookatfirst(x);
    if (result != XEQ_FAIL_OK)
	return (result);
    if (ndx < -1)
	return (XEQ_FAIL_BADREQUEST);  /* LATER retrograde (last event: -1) */
    else if (ndx == -1)
	while (xeqlocator_lookatnext(x) == XEQ_FAIL_OK);
    else while (ndx--)
	if (xeqlocator_lookatnext(x) != XEQ_FAIL_OK)
	    return (XEQ_FAIL_EOS);
    return (XEQ_FAIL_OK);
}

float xeqlocator_reset(t_xeqlocator *x)
{
    return (xeqlocator_settotime(x, 0));  /* hope this is safe... */
}

void xeqlocator_hide(t_xeqlocator *x)
{
    x->l_delay = x->l_delta = 0;
    /* do not play/loop, unless left/right locator's atnext is nonnegative */
    x->l_atprevious = x->l_atdelta = x->l_atnext = -1;
}

int xeqlocator_settoindex(t_xeqlocator *x, int ndx)
{
    int result;
    t_xeqlocator loc = *x;
    switch (result = xeqlocator_lookatindex(&loc, ndx))
    {
    case XEQ_FAIL_OK:
	*x = loc;
	x->l_delay = 0;
	break;
    default:;
    }
    return (result);
}

/* FIXME/CHECKIT */
/* return delay until next message, if any (negative return otherwise) */
float xeqlocator_settotime(t_xeqlocator *x, float when)
{
    xeqlocator_hide(x);
    x->l_when = when;
    if (xeqlocator_lookatfirst(x) != XEQ_FAIL_OK)
	return (-1);
    do if (x->l_when >= when)
	return (x->l_delay = x->l_when - when);
    while (xeqlocator_lookatnext(x) == XEQ_FAIL_OK);
    return (-1);
}

float xeqlocator_settolocator(t_xeqlocator *x, t_xeqlocator *reference)
{
    if (x->l_binbuf == reference->l_binbuf)
    {
	*x = *reference;
	return (x->l_atnext >= 0 ? x->l_delay : -1);
    }
    else return (xeqlocator_settotime(x, reference->l_when));
}

float xeqlocator_move(t_xeqlocator *x, float interval)
{
    int natoms;
    t_atom *ap;
    float lastdelay = 0;
    float nexttime = x->l_when + x->l_delay;
    int ndx = x->l_atnext;
    int prv = x->l_atprevious;
    int checkdelay, checktarget;
    x->l_when += interval;
    if (interval < 0 || ndx <= 0)
	return (xeqlocator_settotime(x, x->l_when));
    xeqlocator_hide(x);
    if (!x->l_binbuf || (natoms = binbuf_getnatom(x->l_binbuf)) <= 0)
	return (-1);

    ap = binbuf_getvec(x->l_binbuf) + ndx;
    checkdelay = 0;
    checktarget = 1;
    for (; ndx < natoms; ndx++, ap++)
    {
	if (checkdelay && ap->a_type == A_FLOAT)
	{
	    lastdelay = ap->a_w.w_float;
	    checkdelay = 0;
	    checktarget = 1;
	}
	else if (checktarget && ap->a_type == A_SYMBOL)
	{
	    if ((nexttime += lastdelay) >= x->l_when)
	    {
		x->l_delay = nexttime - x->l_when;
		x->l_delta = lastdelay;
		x->l_atnext = ndx;
		x->l_atprevious = prv;
		return (x->l_delay);
	    }
	    prv = ndx;
	    lastdelay = 0;
	    checkdelay = checktarget = 0;
	}
	/* LATER sort out semi/comma rules and check again... */
	else checkdelay = checktarget = ap->a_type == A_SEMI;
    }
    return (-1);
}

float xeqlocator_skipnotes(t_xeqlocator *x, int count)
{
    int natoms;
    t_atom *ap;
    float lastdelay = 0;
    float nexttime = x->l_when + x->l_delay;
    int ndx = x->l_atnext;
    int prv = x->l_atprevious;
    int checkdelay, checktarget;
    if (count < 0)
	return (-1);
    xeqlocator_hide(x);
    if (!x->l_binbuf || (natoms = binbuf_getnatom(x->l_binbuf)) <= 0)
	return (-1);

    ap = binbuf_getvec(x->l_binbuf) + ndx;
    checkdelay = 0;
    checktarget = 1;
    for (; ndx < natoms; ndx++, ap++)
    {
	if (checkdelay && ap->a_type == A_FLOAT)
	{
	    lastdelay = ap->a_w.w_float;
	    checkdelay = 0;
	    checktarget = 1;
	}
	else if (checktarget && ap->a_type == A_SYMBOL)
	{
	    if (ap[1].a_type == A_FLOAT && (int)ap[1].a_w.w_float == 144
		&& ap[3].a_type == A_FLOAT && (int)ap[3].a_w.w_float > 0
		&& count-- <= 0)
	    {
		x->l_delay = 0;
		x->l_delta = lastdelay;
		x->l_atnext = ndx;
		x->l_atprevious = prv;
		return (x->l_delay);
	    }
	    prv = ndx;
	    lastdelay = 0;
	    checkdelay = checktarget = 0;
	}
	/* LATER sort out semi/comma rules and check again... */
	else checkdelay = checktarget = ap->a_type == A_SEMI;
    }
    return (-1);
}

void xeqlocator_post(t_xeqlocator *loc, char *name)
{
    post("%s: %f (atom %d after %f, previous atom %d, delta %f)",
	 name, loc->l_when, loc->l_atnext, loc->l_delay,
	 loc->l_atprevious, loc->l_delta);
}

/* XEQ ITERATOR: STATE OF SEQUENCE TRAVERSAL */

void xeqit_rewind(t_xeqit *it)
{
    xeqlocator_reset(&it->i_playloc);
    xeqlocator_reset(&it->i_blooploc);
    xeqlocator_hide(&it->i_elooploc);
    it->i_finish = 0;
    it->i_restarted = 1;
    it->i_loopover = 0;
}

static int xeqit_preloop(t_xeqit *it)
{
    if (it->i_blooploc.l_atnext < 0 ||
	it->i_blooploc.l_atnext >= it->i_elooploc.l_atnext)
    {
	xeqlocator_hide(&it->i_elooploc);  /* do not loop */
	return (0);
    }
    it->i_playloc.l_delay =  /* set predelay of a loop: */
	it->i_elooploc.l_delta - it->i_elooploc.l_delay;
    it->i_loopover = 1;
    if (it->i_delay_hook)
    {
	t_atom at;
	SETFLOAT(&at, it->i_playloc.l_delay);
	it->i_delay_hook(it, 1, &at);
    }
    return (1);
}

static int xeqit_postloop(t_xeqit *it)
{
    if (it->i_loopover_hook) it->i_loopover_hook(it);
    /* clear loopover flag _after_ applying the hook, in order
       to enable resolving startloop()/postloop() confusion */
    it->i_loopover = 0;  /* LATER sort out reentrancy etc. */
    xeqlocator_settolocator(&it->i_playloc, &it->i_blooploc);
    if (it->i_delay_hook)
    {
	t_atom at;
	SETFLOAT(&at, it->i_playloc.l_delay);
	it->i_delay_hook(it, 1, &at);
    }
    return (1);
}

/* LATER clear up */
static int xeqit_startloop(t_xeqit *it)
{
    /* from preloop */
    if (it->i_blooploc.l_atnext < 0 ||
	it->i_blooploc.l_atnext >= it->i_elooploc.l_atnext)
    {
	xeqlocator_hide(&it->i_elooploc);  /* do not loop */
	return (0);
    }
#if 0  /* not needed? */
    it->i_playloc.l_delay =  /* set predelay of a loop: */
	it->i_elooploc.l_delta - it->i_elooploc.l_delay;
#endif

    /* from postloop */
    it->i_loopover = 0;  /* LATER sort out reentrancy etc. */
    if (it->i_loopover_hook) it->i_loopover_hook(it);
    xeqlocator_settolocator(&it->i_playloc, &it->i_blooploc);
    if (it->i_delay_hook)
    {
	t_atom at;
	SETFLOAT(&at, it->i_playloc.l_delay);
	it->i_delay_hook(it, 1, &at);
    }
    return (1);
}

static void xeqit_stoploop(t_xeqit *it)
{
    xeqlocator_reset(&it->i_blooploc);
    xeqlocator_hide(&it->i_elooploc);  /* do not loop */
}

int xeqit_loop(t_xeqit *it, t_float lpos, t_float rpos)
{
    if (lpos < 0 || rpos <= lpos)
    {
	xeqit_stoploop(it);
	return (0);
    }
    else {
	xeqlocator_settotime(&it->i_blooploc, lpos);
	xeqlocator_settotime(&it->i_elooploc, rpos);
	it->i_restarted = 1;
	return (xeqit_startloop(it));
    }
}

int xeqit_reloop(t_xeqit *it)
{
    it->i_restarted = 1;
    return (xeqit_startloop(it));
}

void xeqit_sethooks(t_xeqit *it, t_xeqithook_delay dhook,
		    t_xeqithook_applypp ahook, t_xeqithook_message mhook,
		    t_xeqithook_finish fhook, t_xeqithook_loopover lhook)
{
    it->i_delay_hook = dhook;
    it->i_applypp_hook = ahook;
    it->i_message_hook = mhook;
    it->i_finish_hook = fhook;
    it->i_loopover_hook = lhook;
}

void xeqit_settoit(t_xeqit *it, t_xeqit *reference)
{
    it->i_finish = reference->i_finish;
    xeqlocator_settolocator(&it->i_playloc, &reference->i_playloc);
}

/* MULTICASTING HOOKS */

static void xeq_setbinbuf(t_xeq *x, t_binbuf *bb);
static int xeqhook_multicast_setbinbuf(t_pd *f, void *dummy)
{
    t_xeq *host = XEQ_HOST(f);
    t_xeq *base = XEQ_BASE(f);
    int nbases = XEQ_NBASES(f);
    if (host && base)
	while (nbases-- > 0) xeq_setbinbuf(base++, host->x_binbuf);
    return (1);
}

static int xeqhook_multicast_rewind(t_pd *f, void *dummy)
{
    t_xeq *host = XEQ_HOST(f);
    t_xeq *base = XEQ_BASE(f);
    int nbases = XEQ_NBASES(f);
    if (host && base)
	while (nbases-- > 0) xeq_rewind(base++);
    return (1);
}

/* SEQUENCE TRAVERSING HOOKS */

static void xeqithook_autodelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    if (x->x_clock)
    {
	clock_delay(x->x_clock,
		    x->x_clockdelay = it->i_playloc.l_delay * x->x_tempo);
	x->x_whenclockset = clock_getsystime();
    }
}

static void xeqithook_stepdelay(t_xeqit *it, int argc, t_atom *argv)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    outlet_list(((t_object *)x)->ob_outlet, 0, argc, argv);
}

/* apply playback parameters and update noteon buffer */
int xeqithook_applypp(t_xeqit *it, t_symbol *trackname,
		      int status, int *channelp, int *data1p, int *data2p)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    if (x->x_ttp && !squtt_checksymbol(x->x_ttp, trackname)) return (0);
    if (status == 0x90 && *data2p)
    {
	int transposed;
	if (x->x_noteons[*channelp][*data1p] >= 0)
	    return (0);                                 /* ignore repetition */
	transposed = *data1p + x->x_transpo;            /* add transposition */
	if (transposed < 0 || transposed > 127)
	    return (0);                                 /* validate */
	x->x_noteons[*channelp][*data1p] = transposed;  /* store */
	*data1p = transposed;
    }
    else if (status <= 0x90)
    {
	int transposed = x->x_noteons[*channelp][*data1p];  /* read transpo */
	if (transposed >= 0)
	{
	    x->x_noteons[*channelp][*data1p] = -1;
	    *data1p = transposed;
	}
    }
    return (1);
}

/* never use implicit running status (expect other midi senders) */
static void xeqithook_playmessage(t_xeqit *it,
				  t_symbol *target, int argc, t_atom *argv)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    t_pd *dest = target->s_thing;
    if (argv->a_type == A_FLOAT)
    {
	int status = it->i_status;
	if (dest) typedmess(dest, &s_list, argc, argv);
	if (status)
	{
	    status |= it->i_channel;
	    outlet_float(x->x_midiout, status);
	    outlet_float(x->x_midiout, it->i_data1);
	    if (it->i_data2 >= 0) outlet_float(x->x_midiout, it->i_data2);
	}
    }
    else if (argv->a_type == A_SYMBOL && dest)
	typedmess(dest, argv->a_w.w_symbol, argc-1, argv+1);
}

static void xeqithook_findmessage(t_xeqit *it,
				  t_symbol *target, int argc, t_atom *argv)
{
    if (it->i_messtarget != &s_ && it->i_messtarget != target) return;
    if (argc > it->i_messlength) argc = it->i_messlength;
    if (argc == it->i_messlength)
    {
	t_atom *messp = it->i_message;
	while (argc--)
	{
	    if (argv->a_type == A_FLOAT)
	    {
		if (messp->a_type != A_FLOAT ||
		    argv->a_w.w_float != messp->a_w.w_float) break;
	    }
	    else if (argv->a_type == A_SYMBOL)
	    {
		if (messp->a_type != A_SYMBOL ||
		    argv->a_w.w_symbol != messp->a_w.w_symbol) break;
	    }
	    else break;
	    argv++;
	    messp++;
	}
	if (argc < 0) it->i_finish = -1;
    }
}

static void xeqithook_findevent(t_xeqit *it,
				t_symbol *target, int argc, t_atom *argv)
{
    if (it->i_status) {
	t_xeq *x = (t_xeq *)it->i_owner;
    }
}

static void xeqithook_playfinish(t_xeqit *it)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    outlet_bang(x->x_bangout);
    x->x_whenclockset = 0;
}

static void xeqithook_stepfinish(t_xeqit *it)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    outlet_bang(x->x_bangout);
}

static void xeq_flush(t_xeq *x);
static void xeqithook_loopover(t_xeqit *it)
{
    t_xeq *x = (t_xeq *)it->i_owner;
    if (it->i_loopover)
    {
	/* bangout at postloop only */
	outlet_bang(x->x_bangout);
	x->x_whenclockset = 0;
    }
    xeq_flush(x);  /* flush also at startloop */
}

/* SEQUENCE TRAVERSAL */

/* this is qlist_donext(), somewhat modified */
/* LATER abstract this into a generic binbuf parsing routine
   (current version of mfbb_parse() ignores nonmidi events). */
void xeqit_donext(t_xeqit *it)
{
    t_xeq *owner = (t_xeq *)it->i_owner;
    t_symbol *target = 0;
    int status, data1, data2, channel;
    if (it->i_finish)
    {
	if (it->i_finish == 1) goto end;
	else return;  /* forced: do not perform normal finishing */
    }
    if (it->i_playloc.l_atnext < 0) goto end;
    if (!owner->x_binbuf) goto end;
    if (it->i_loopover && xeqit_postloop(it))
	return;
    it->i_playloc.l_when += it->i_playloc.l_delay;
    it->i_playloc.l_delay = 0;
    while (1)
    {
    	int argc = binbuf_getnatom(owner->x_binbuf);
    	t_atom *argv = binbuf_getvec(owner->x_binbuf);
	int count, wasrestarted;
	int onset = it->i_playloc.l_atnext, onset2;
    	t_atom *ap = argv + onset, *ap2;
	t_symbol *lasttarget = target;

	if (onset > it->i_elooploc.l_atprevious && xeqit_preloop(it))
	    return;
	if (onset >= argc) goto end;

	/* skip to message beginning */
	/* LATER sort out semi/comma rules and check again... */
	target = 0;
    	while (ap->a_type == A_SEMI || ap->a_type == A_COMMA)
    	{
    	    if (ap->a_type == A_COMMA) target = lasttarget;
    	    onset++, ap++;
    	    if (onset >= argc) goto end;
	}

	if (!target && ap->a_type == A_FLOAT)
	{
	    /* we are at the first atom of a delta vector */
    	    ap2 = ap + 1;
    	    onset2 = onset + 1;
    	    while (onset2 < argc && ap2->a_type == A_FLOAT)
    	    	onset2++, ap2++;
	    it->i_playloc.l_atprevious = it->i_playloc.l_atnext;
    	    it->i_playloc.l_atnext = onset2;
	    it->i_playloc.l_delay = it->i_playloc.l_delta = ap->a_w.w_float;
	    if (it->i_delay_hook) it->i_delay_hook(it, onset2-onset, ap);
    	    return;
    	}

	/* we are at message beginning, which either has to be a new
	   target atom (if target is needed) or it can be any atom
	   (if previous target is used due to a preceding comma) */
    	ap2 = ap + 1;
    	onset2 = onset + 1;
    	while (onset2 < argc &&
    	    (ap2->a_type == A_FLOAT || ap2->a_type == A_SYMBOL))
    	    	onset2++, ap2++;
    	count = onset2 - onset;   /* message length */
    	if (!target)
    	{
    	    if (ap->a_type != A_SYMBOL)  /* no target after delay? */
	    {   /* what then... a pointer?) */
		it->i_playloc.l_atnext = onset2;  /* index to next separator */
		continue;  /* skip to next separator */
	    }
    	    else target = ap->a_w.w_symbol;
    	    ap++;
    	    onset++;
    	    count--;
    	    if (!count)  /* is message empty? */
    	    {
		it->i_playloc.l_atnext = onset2;  /* index to next separator */
    	    	continue;  /* skip to next separator */
    	    }
    	}

	/* now we know both the message and the target */
	if (ap->a_type == A_FLOAT &&
	    xeq_listparse(count, ap, &status, &channel, &data1, &data2) &&
	    (!it->i_applypp_hook ||
	     it->i_applypp_hook(it, target, status, &channel, &data1, &data2)))
	{
#if 0
	    post("%d %d %d %d", status, channel, data1, data2);
#endif
	    it->i_status = status;
	    it->i_channel = channel;
	    it->i_data1 = data1;
	    it->i_data2 = data2;
	}
	else it->i_status = 0;

	wasrestarted = it->i_restarted;
	it->i_restarted = 0;
	if (it->i_message_hook) it->i_message_hook(it, target, count, ap);
	it->i_playloc.l_atprevious = it->i_playloc.l_atnext;
	it->i_playloc.l_atnext = onset2;  /* index to next separator */
	if (it->i_restarted)
	    return;
	it->i_restarted = wasrestarted;
    }  /* while (1); never falls through */

end:
    xeqlocator_hide(&it->i_playloc);
    it->i_finish = 1;
    if (it->i_finish_hook) it->i_finish_hook(it);
}

/* CLOCK HANDLER */

void xeq_tick(t_xeq *x)
{
    x->x_whenclockset = 0;
    xeqit_donext(&x->x_autoit);
}

/* CREATION/DESTRUCTION */

static void xeq_setbinbuf(t_xeq *x, t_binbuf *bb)
{
    x->x_binbuf = bb;
    x->x_beditloc.l_binbuf = bb;
    x->x_eeditloc.l_binbuf = bb;
    x->x_autoit.i_playloc.l_binbuf = bb;
    x->x_autoit.i_blooploc.l_binbuf = bb;
    x->x_autoit.i_elooploc.l_binbuf = bb;
    x->x_stepit.i_playloc.l_binbuf = bb;
    x->x_stepit.i_blooploc.l_binbuf = bb;
    x->x_stepit.i_elooploc.l_binbuf = bb;
    x->x_walkit.i_playloc.l_binbuf = bb;
    x->x_walkit.i_blooploc.l_binbuf = bb;
    x->x_walkit.i_elooploc.l_binbuf = bb;
}

static void xeq_newbase(t_xeq *x, t_binbuf *bb, t_method tickmethod)
{
    xeq_window_bind(x);
    x->x_tempo = 1;
    x->x_canvas = canvas_getcurrent();
    x->x_whenclockset = 0;
    x->x_clockdelay = 0;
    x->x_clock = tickmethod ? clock_new(x, tickmethod) : 0;
    xeq_noteons_clear(x);
    x->x_ttp = 0;
    x->x_transpo = 0;
    x->x_autoit.i_owner = x;
    x->x_stepit.i_owner = x;
    x->x_walkit.i_owner = x;
    xeq_setbinbuf(x, bb);
    xeqit_sethooks(&x->x_autoit, xeqithook_autodelay, xeqithook_applypp,
		   xeqithook_playmessage, xeqithook_playfinish,
		   xeqithook_loopover);
    xeqit_sethooks(&x->x_stepit, xeqithook_stepdelay, xeqithook_applypp,
		   xeqithook_playmessage, xeqithook_stepfinish, 0);
    xeqit_sethooks(&x->x_walkit, 0, 0, 0, 0, 0);
    xeqit_rewind(&x->x_autoit);
    xeqit_rewind(&x->x_stepit);
    xeqit_rewind(&x->x_walkit);
}

static void *xeq_new(t_symbol *name)
{
    t_xeq *x = (t_xeq *)hyphen_new(xeq_class, 0);
    hyphen_attach((t_hyphen *)x, name);
    xeq_newbase(x, binbuf_new(), (t_method)xeq_tick);
    hyphen_forallfriends((t_hyphen *)x, xeqhook_multicast_setbinbuf, 0);
    outlet_new((t_object *)x, &s_list);
    x->x_midiout = outlet_new((t_object *)x, &s_float);
    x->x_bangout = outlet_new((t_object *)x, &s_bang);
    return (x);
}

static void xeq_freebase(t_xeq *x)
{
    if (x->x_clock) clock_free(x->x_clock);
    xeq_window_unbind(x);
}

static void xeq_free(t_xeq *x)
{
    t_binbuf *bb = x->x_binbuf;
    x->x_binbuf = 0;
    hyphen_forallfriends((t_hyphen *)x, xeqhook_multicast_setbinbuf, 0);
    hyphen_detach((t_hyphen *)x);
    binbuf_free(bb);
    xeq_freebase(x);
}

/* PLAYBACK PARAMETERS METHODS */

void xeq_tracks(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    t_symbol *tts = &s_;
    if (ac && !(tts = squtt_makesymbol(av))) return;
    if (tts != &s_)
    {
	if (x->x_ttp || (x->x_ttp = squtt_new()))
	    squtt_make(x->x_ttp, tts);
    }
    else if (x->x_ttp) post("track template: %s", x->x_ttp->t_given->s_name);
    else post("track template is empty");
}

void xeq_transpo(t_xeq *x, t_floatarg f)
{
    int i = (int)f;
    if (i > -128 && i < 128) x->x_transpo = i;
}

void xeq_tempo(t_xeq *x, t_floatarg f)
{
    float newtempo;
    if (f == 0) f = 1;  /* tempo message without argument (FIXME) */
    else if (f < 1e-20) f = 1e-20;
    else if (f > 1e20) f = 1e20;
    newtempo = 1./f;
    if (x->x_whenclockset != 0)
    {
    	float elapsed = clock_gettimesince(x->x_whenclockset);
    	float left = x->x_clockdelay - elapsed;
    	if (left < 0) left = 0;
    	else left *= newtempo / x->x_tempo;
    	clock_delay(x->x_clock, x->x_clockdelay = left);
	x->x_whenclockset = clock_getsystime();
    }
    x->x_tempo = newtempo;
}

/* PLAYBACK CONTROL METHODS */

static void xeq_flush(t_xeq *x)
{
    int channel, key, transposed;
    for (channel = 0; channel < 16; channel++)
    {
	for (key = 0; key < 128; key++)
	{
	    if ((transposed = x->x_noteons[channel][key]) >= 0)
	    {
		outlet_float(x->x_midiout, 0x90 | channel);
		outlet_float(x->x_midiout, transposed);
		outlet_float(x->x_midiout, 0);
		x->x_noteons[channel][key] = -1;
	    }
	}
    }
}

void xeq_rewind(t_xeq *x)
{
    xeqit_rewind(&x->x_autoit);
    xeqit_rewind(&x->x_stepit);  /* LATER rethink */
    if (x->x_clock) clock_unset(x->x_clock);
    x->x_whenclockset = 0;
}

void xeq_stop(t_xeq *x)
{
    x->x_autoit.i_restarted = 1;  /* LATER rethink */
    if (x->x_clock) clock_unset(x->x_clock);
    if (x->x_whenclockset != 0)
    {
    	float elapsed = clock_gettimesince(x->x_whenclockset);
    	float left = x->x_clockdelay - elapsed;
    	if (left < 0) left = 0;
	x->x_autoit.i_playloc.l_delay = left / x->x_tempo;
#if 0
	post("stop: delay set to %f (user %f)",
	     x->x_autoit.i_playloc.l_delay, left);
#endif
	x->x_whenclockset = 0;
    }
}

static void xeq_next(t_xeq *x, t_floatarg drop)
{
    xeqit_sethooks(&x->x_stepit, xeqithook_stepdelay,
		   drop ? 0 : xeqithook_applypp,
		   drop ? 0 : xeqithook_playmessage, xeqithook_stepfinish, 0);
    xeqit_donext(&x->x_stepit);
}

void xeq_start(t_xeq *x)
{
    if (x->x_clock)
    {
#if 0
	post ("start delay %f", x->x_autoit.i_playloc.l_delay);
#endif
 	clock_delay(x->x_clock, x->x_clockdelay =
		    x->x_autoit.i_playloc.l_delay * x->x_tempo);
	x->x_whenclockset = clock_getsystime();
    }
}

/* LATER do nothing during playback */
static void xeq_dobang(t_xeq *x)
{
    t_xeqit *it = &x->x_autoit;
    xeq_flush(x);
    xeqit_sethooks(it, xeqithook_autodelay, xeqithook_applypp,
		   xeqithook_playmessage, xeqithook_playfinish,
		   xeqithook_loopover);
    xeq_start(x);
}

static void xeq_bang(t_xeq *x)
{
    t_xeqit *it = &x->x_autoit;
    xeqlocator_reset(&it->i_blooploc);
    xeqlocator_hide(&it->i_elooploc);
    xeq_dobang(x);
}

/* LATER do nothing in certain conditions */
void xeq_loop(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    t_xeqit *it = &x->x_autoit;
    if (ac < 1)
    {
	xeqit_reloop(it);
    }
    else if (ac == 1)
    {
	if (av->a_type == A_SYMBOL && av->a_w.w_symbol == gensym("break"))
	    xeqit_stoploop(it);
    }
    else if (av[0].a_type == A_FLOAT && av[1].a_type == A_FLOAT)
    {
	xeqit_loop(it, av[0].a_w.w_float, av[1].a_w.w_float);
    }
}

/* SEARCHING METHODS */

t_xeqlocator *xeq_dolocate(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    t_symbol *which = 0, *refwhich = 0;
    float when = 0;
    t_xeqlocator *loc = 0, *refloc = 0;
    int relative = s == gensym("locafter");
    int skipnotes = s == gensym("skipnotes");
    if (ac > 0)
    {
	if (av->a_type == A_SYMBOL) which = av->a_w.w_symbol;
	else if (av->a_type == A_FLOAT) when = av->a_w.w_float;
	else goto postem;
    }
    else goto postem;
    if (ac > 1 && which)
    {
	av++;
	if (av->a_type == A_SYMBOL) refwhich = av->a_w.w_symbol;
	else if (av->a_type == A_FLOAT) when = av->a_w.w_float;
    }
    if (which && !(loc = xeq_whichloc(x, which))) goto postem;
    if (refwhich && !(refloc = xeq_whichloc(x, refwhich))) goto postem;
    if (!loc) loc = &x->x_autoit.i_playloc;
    if (refloc) xeqlocator_settolocator(loc, refloc);
    else if (relative) xeqlocator_move(loc, when);
    else if (skipnotes) xeqlocator_skipnotes(loc, (int)when);
    else xeqlocator_settotime(loc, when);
    return (loc);
postem:
    xeqlocator_post(&x->x_autoit.i_playloc, "auto");
    xeqlocator_post(&x->x_stepit.i_playloc, "step");
    xeqlocator_post(&x->x_autoit.i_blooploc, "bloop");
    xeqlocator_post(&x->x_autoit.i_elooploc, "eloop");
    xeqlocator_post(&x->x_beditloc, "bedit");
    xeqlocator_post(&x->x_eeditloc, "eedit");
    return (0);
}

void xeq_locate(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_dolocate(x, s, ac, av);
}

void xeq_find(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    float when;
    int ndx;
    t_xeqit *it = &x->x_walkit;
    t_xeqithook_applypp ahook = 0;
    t_xeqithook_message mhook = 0;
    if (!ac) return;
    if (s == gensym("find"))
    {
	if (av->a_type == A_SYMBOL)
	{
	    it->i_messtarget = av->a_w.w_symbol;
	    ac--;
	    av++;
	}
	else it->i_messtarget = &s_;
	it->i_messlength = ac;
	it->i_message = av;
	mhook = xeqithook_findmessage;
    }
    else if (s == gensym("event"))  /* LATER */
    {
	/* store event */
	mhook = xeqithook_findevent;
    }
    else return;
    xeqit_sethooks(it, 0, ahook, mhook, 0, 0);
    xeqit_rewind(it);
    while (!it->i_finish)
    {
	ndx = it->i_playloc.l_atnext;
	xeqit_donext(it);
	when = it->i_playloc.l_when;
    }
    if (it->i_finish == 1)
	post("not found...");
    else {
	post("found after atom %d, onset %f", ndx, when);
	x->x_beditloc.l_when = when;
	x->x_beditloc.l_delay = 0;
	x->x_beditloc.l_atnext = ndx;
    }
}

/* EDITING METHODS */

static void xeq_add(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    t_atom a;
    SETSEMI(&a);
    binbuf_add(x->x_binbuf, ac, av);
    binbuf_add(x->x_binbuf, 1, &a);
}

static void xeq_add2(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    binbuf_add(x->x_binbuf, ac, av);
}

static void xeq_addline(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    int i;
    for (i = 0; i < ac; i++)
    {
    	if (av[i].a_type == A_SYMBOL)
	{
	    if (!strcmp(av[i].a_w.w_symbol->s_name, "_semi_"))
	    	SETSEMI(&av[i]);
	    else if (!strcmp(av[i].a_w.w_symbol->s_name, "_comma_"))
	    	SETCOMMA(&av[i]);
    	}
    }
    binbuf_add(x->x_binbuf, ac, av);
}

static void xeq_clear(t_xeq *x)
{
    xeq_rewind(x);
    binbuf_clear(x->x_binbuf);
}

static void xeq_set(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    xeq_clear(x);
    xeq_add(x, s, ac, av);
}

static void xeq_doclone(t_xeq *x, t_symbol *name, int append)
{
    t_xeq *otherhost = (t_xeq *)hyphen_findhost((t_hyphen *)x, name);
    if (otherhost && otherhost->x_binbuf)
    {
	if (!append) xeq_clear(x);
	binbuf_add(x->x_binbuf, binbuf_getnatom(otherhost->x_binbuf),
		   binbuf_getvec(otherhost->x_binbuf));
    }
}

static void xeq_clone(t_xeq *x, t_symbol *name)
{
    xeq_doclone(x, name, 0);
}

static void xeq_addclone(t_xeq *x, t_symbol *name)
{
    xeq_doclone(x, name, 1);
}

/* FILE INPUT/OUTPUT METHODS */

static void xeq_mfread(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    t_symbol *filename, *tts = &s_;
    if (!ac || av->a_type != A_SYMBOL) return;
    filename = av->a_w.w_symbol;
    if (ac > 1 && !(tts = squtt_makesymbol(av + 1))) return;
    if (mfbb_read(x->x_binbuf, filename->s_name,
		  canvas_getdir(x->x_canvas)->s_name, tts))
	error("%s: read failed", filename->s_name);
    xeq_rewind(x);
    hyphen_forallfriends((t_hyphen *)x, xeqhook_multicast_rewind, 0);
}

static void xeq_mfwrite(t_xeq *x, t_symbol *filename, t_symbol *tts)
{
    char buf[MAXPDSTRING];
    canvas_makefilename(x->x_canvas, filename->s_name,
			buf, MAXPDSTRING);
    if (mfbb_write(x->x_binbuf, buf, "", tts))
	error("%s: write failed", filename->s_name);
}

static void xeq_read(t_xeq *x, t_symbol *s, int ac, t_atom *av)
{
    char *format, *filename;
    int fid = 0;
    if (!ac || av->a_type != A_SYMBOL) return;
    format = av->a_w.w_symbol->s_name;
    if (!strcmp(format, "cr"))
    	fid = 1;
    else if (!strcmp(format, "mf"))
	fid = 2;
    if (fid && (!--ac || (++av)->a_type != A_SYMBOL)) return;
    filename = av->a_w.w_symbol->s_name;
    if (fid == 2)
	xeq_mfread(x, s, ac, av);
    else {
	if (binbuf_read_via_path(x->x_binbuf, filename,
				 canvas_getdir(x->x_canvas)->s_name, fid))
	    error("%s: read failed", filename);
	xeq_rewind(x);
	hyphen_forallfriends((t_hyphen *)x, xeqhook_multicast_rewind, 0);
    }
}

static void xeq_write(t_xeq *x, t_symbol *filename,
		      t_symbol *format, t_symbol *tts)
{
    int cr = 0;
    char buf[MAXPDSTRING];
    if (!strcmp(format->s_name, "cr"))
    	cr = 1;
    else if (!strcmp(format->s_name, "mf"))
    {
	xeq_mfwrite(x, filename, tts);
	return;
    }
    else if (*format->s_name)
    	error("xeq_read: unknown flag: %s", format->s_name);
    canvas_makefilename(x->x_canvas, filename->s_name,
    	buf, MAXPDSTRING);
    if (binbuf_write(x->x_binbuf, buf, "", cr))
	error("%s: write failed", filename->s_name);
}

static void xeq_print(t_xeq *x)
{
    post("--------- xeq contents: -----------");
    binbuf_print(x->x_binbuf);
}

static void xeq_edit(t_xeq *x)
{
    t_atom *ap = binbuf_getvec(x->x_binbuf);
    int natoms = binbuf_getnatom(x->x_binbuf);
    char buf[MAXPDSTRING+2];
    int buflen = 0;
    int i, newline = 1;
    xeq_window(x, 0, "");
    *buf = '\0';
    for (i = 0; i < natoms; i++, ap++)
    {
	if (i)
	{
	    if (newline)
	    {
		strcat(buf, "\n");
		xeq_window_append(x, buf);
		*buf = '\0';
		buflen = 0;
	    }
	    else {
		strcat(buf, " ");
		buflen++;
	    }
	}
	if (buflen < MAXPDSTRING)
	    atom_string(ap, buf + buflen, MAXPDSTRING - buflen);
	buflen = strlen(buf);
	newline = ap->a_type == A_SEMI;
    }
    if (natoms) strcat(buf, "\n");
    if (*buf) xeq_window_append(x, buf);
}

static void xeq_editok(t_xeq *x)
{
    xeq_window_ok(x);

}

/* INHERITANCE HELPERS */

/* derive a friend */
static t_xeq *xeq_derived_embed(t_hyphen *x, int tablesize,
				t_symbol *seqname, t_symbol *refname,
				t_method tickmethod)
{
    t_xeq *base;
    t_binbuf *bb = 0;
    int i;
    hyphen_attach(x, seqname);
    if (!hyphen_multiderive(x, xeq_base_class, tablesize))
    {
	hyphen_detach(x);
	return (0);
    }
    if (x->x_host) bb = ((t_xeq *)x->x_host)->x_binbuf;
    for (i = 0, base = XEQ_BASE(x); i < XEQ_NBASES(x); i++, base++)
    {
	if (refname && !i)
	    /* attach reference name to the first base,
	       LATER use <id>-<refname> scheme */
	    hyphen_attach((t_hyphen *)base, refname);
	xeq_newbase(base, bb, tickmethod);
    }
    return (XEQ_BASE(x));
}

/* derive a host */
static t_xeq *xeq_derived_hostify(t_hyphen *x, int tablesize,
				  t_symbol *refname, t_method tickmethod)
{
    t_xeq *base = 0;
    t_binbuf *bb = 0;
    int i;
#ifdef XEQ_VERBOSE
    if (!refname || refname == &s_)
	post("hostifying %s without a reference name",
	     class_getname(*(t_pd *)x));
#endif
    if (!hyphen_multiderive(x, xeq_base_class, tablesize))
	return (0);
    base = XEQ_BASE(x);
    x->x_host = (t_hyphen *)base;
    bb = binbuf_new();
    /* initialize first base and attach a reference name to it,
       LATER use <id>-<refname> scheme */
    hyphen_initialize((t_hyphen *)base, xeq_base_class, 0);
    hyphen_attach((t_hyphen *)base, refname);
    for (i = 0; i < XEQ_NBASES(x); i++, base++)
    {
	xeq_newbase(base, bb, tickmethod);
    }
    hyphen_forallfriends((t_hyphen *)XEQ_BASE(x),
			 xeqhook_multicast_setbinbuf, 0);
    return (XEQ_BASE(x));
}

/* called from within a derived constructor */
t_hyphen *xeq_derived_new(t_class *derivedclass, int tablesize,
			  t_symbol *seqname, t_symbol *refname,
			  t_method tickmethod)
{
    t_hyphen *x = 0;
    int failure = 0;
    if (seqname && seqname != &s_)  /* [xeq_<name> ...] */
    {
	if (!(x = hyphen_new(derivedclass, "xeq"))
	    || !xeq_derived_embed(x, tablesize, seqname, refname, tickmethod))
	    failure = 1;
    }
    else  /* [xeq_host <name> ...] */
    {
	/* A visible xeq_host is first initialized as host, then
	   converted to its own XEQ_BASE friend in xeq_derived_hostify().
	   It is never attached -- a real, attached host is XEQ_BASE.
	   The XEQ_HOST == XEQ_BASE condition holds after hostifying
	   conversion, and may be used to block friend requests (like
	   `host' message).
	*/
	if (!(x = hyphen_new(derivedclass, 0))
	    || !xeq_derived_hostify(x, tablesize, refname, tickmethod))
	    failure = 1;
    }
    if (failure && x)
    {
	hyphen_free(x);
	x = 0;
    }
    return (x);
}

static void xeq_derived_deembed(t_hyphen *x)
{
    t_xeq *base;
    int i;
    for (i = 0, base = XEQ_BASE(x); i < XEQ_NBASES(x); i++, base++)
    {
	hyphen_detach((t_hyphen *)base);
	xeq_freebase(base);  /* free clock resource */
    }
    hyphen_freebase(x);
    hyphen_detach(x);
}

void xeq_derived_free(t_hyphen *x)
{
    t_binbuf *bb = 0;
    if (x->x_host == x->x_basetable)
    {
	t_xeq *base = XEQ_BASE(x);
	bb = base->x_binbuf;
	base->x_binbuf = 0;
	hyphen_forallfriends((t_hyphen *)base, xeqhook_multicast_setbinbuf, 0);
    }
    xeq_derived_deembed(x);
    if (bb) binbuf_free(bb);
}

void xeq_derived_clone(t_hyphen *x)
{
    t_xeq *base = XEQ_BASE(x);

    /* (using standard xeq hooks of the base) */

    /* the outlets are created for (and pd_freed() from) a visible,
       derived object, but plugged into the base pointers */
    ((t_object *)base)->ob_outlet = outlet_new((t_object *)x, &s_list);
    base->x_midiout = outlet_new((t_object *)x, &s_float);
    base->x_bangout = outlet_new((t_object *)x, &s_bang);
}

/* called from within `host' method */
void xeq_derived_reembed(t_hyphen *x, t_symbol *seqname)
{
    if (x->x_host != x->x_basetable)
    {
	t_xeq *base;
	t_binbuf *bb = 0;
	int i;
	hyphen_attach(x, seqname);
	if (x->x_host) bb = ((t_xeq *)x->x_host)->x_binbuf;
	for (i = 0, base = XEQ_BASE(x); i < XEQ_NBASES(x); i++, base++)
	{
	    xeq_setbinbuf(base, bb);
	    xeqit_rewind(&base->x_autoit);
	    xeqit_rewind(&base->x_stepit);
	    xeqit_rewind(&base->x_walkit);
	}
    }
}

/* called from within `maxlayers' method */
void xeq_derived_resizeembed(t_hyphen *x, int newtablesize)
{
    /* it is not that trivial... perhaps needs splitting into two calls? */
}

/* LATER call this in xeq's methods (instead of friend wrappers) */
int xeq_derived_validate(t_hyphen *x)
{
    if (x->x_host != x->x_basetable)
    {
	t_xeq *host = XEQ_HOST(x);
	t_binbuf *bb;
	if (host && (bb = host->x_binbuf))
	{
	    t_xeq *base;
	    int i;
	    for (i = 0, base = XEQ_BASE(x); i < XEQ_NBASES(x); i++, base++)
	    {
		base->x_binbuf = bb;  /* LATER rethink (enable multihosting) */
	    }
	}
	else return (0);
    }
    return (1);
}

/* ENTRY POINT */

void xeq_setup(void)
{
    post("beware! this is xeq %s, %s %s build... it may bite!",
	 XEQ_VERSION, text_ordinal(XEQ_BUILD), XEQ_RELEASE);
    xeq_gui_defs();
    xeq_locator_setup();

    xeq_class = class_new(gensym("xeq"), (t_newmethod)xeq_new,
			  (t_method)xeq_free, sizeof(t_xeq), 0, A_DEFSYM, 0);

    class_addmethod(xeq_class, (t_method)xeq_tracks,
		    gensym("tracks"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_transpo,
		    gensym("transpo"), A_DEFFLOAT, 0);
    class_addmethod(xeq_class, (t_method)xeq_tempo,
		    gensym("tempo"), A_DEFFLOAT, 0);

    class_addbang(xeq_class, xeq_bang);
    class_addmethod(xeq_class, (t_method)xeq_next,
		    gensym("next"), A_DEFFLOAT, 0);
    class_addmethod(xeq_class, (t_method)xeq_loop,
		    gensym("loop"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_rewind, gensym("rewind"), 0);
    class_addmethod(xeq_class, (t_method)xeq_stop, gensym("stop"), 0);
    class_addmethod(xeq_class, (t_method)xeq_flush, gensym("flush"), 0);

    class_addmethod(xeq_class, (t_method)xeq_locate,
		    gensym("locate"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_locate,
		    gensym("locafter"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_locate,
		    gensym("skipnotes"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_find,
		    gensym("find"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_find,
		    gensym("event"), A_GIMME, 0);

    class_addmethod(xeq_class, (t_method)xeq_clear, gensym("clear"), 0);
    class_addmethod(xeq_class, (t_method)xeq_clone,
		    gensym("clone"), A_DEFSYM, 0);
    class_addmethod(xeq_class, (t_method)xeq_clone,
		    gensym("addclone"), A_DEFSYM, 0);
    class_addmethod(xeq_class, (t_method)xeq_set,
		    gensym("set"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_add,
		    gensym("add"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_add2,
		    gensym("add2"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_addline,
		    gensym("addline"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_add,
		    gensym("append"), A_GIMME, 0);

    class_addmethod(xeq_class, (t_method)xeq_edit, gensym("edit"), 0);
    class_addmethod(xeq_class, (t_method)xeq_editok, gensym("editok"), 0);

    class_addmethod(xeq_class, (t_method)xeq_read,
		    gensym("read"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_write,
		    gensym("write"), A_SYMBOL, A_DEFSYM, A_DEFSYM, 0);
    class_addmethod(xeq_class, (t_method)xeq_mfread,
		    gensym("mfread"), A_GIMME, 0);
    class_addmethod(xeq_class, (t_method)xeq_mfwrite,
		    gensym("mfwrite"), A_SYMBOL, A_DEFSYM, 0);

    class_addmethod(xeq_class, (t_method)xeq_print, gensym("print"), 0);

    hyphen_setup(xeq_class, &xeq_base_class);
    xeq_host_dosetup();  /* this must precede the others */
    xeq_parse_dosetup();
    xeq_polyparse_dosetup();
    xeq_record_dosetup();
    xeq_follow_dosetup();
    xeq_data_dosetup();
    xeq_polytempo_dosetup();
    xeq_time_dosetup();
    xeq_query_dosetup();
}
