/* Copyright (c) 1997-2002 Miller Puckette and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#ifndef __XEQ_H__
#define __XEQ_H__

#define XEQ_VERBOSE

typedef struct _xeqlocator
{
    float      l_when;        /* logical time locator is set to */
    float      l_delay;       /* logical delay until next event */
    float      l_delta;       /* logical delta time of next event */
    int        l_atprevious;  /* atom-index of previous event's target */
    int        l_atdelta;     /* atom-index of next event's delta vector */
    int        l_atnext;      /* atom-index of next event's target symbol */
    t_binbuf  *l_binbuf;
    /* traversal helpers (redundant) */
    t_atom    *l_firstatom;
    int        l_natoms;
} t_xeqlocator;

/* LATER abstract t_xeqit and hooks into generic squiter handling */
struct _xeqit;
typedef void (*t_xeqithook_delay)(struct _xeqit *it, int argc, t_atom *argv);
typedef int (*t_xeqithook_applypp)(struct _xeqit *it, t_symbol *trackname,
				   int status, int *channel,
				   int *data1, int *data2);
typedef void (*t_xeqithook_message)(struct _xeqit *it, t_symbol *target,
				    int argc, t_atom *argv);
typedef void (*t_xeqithook_finish)(struct _xeqit *it);
typedef void (*t_xeqithook_loopover)(struct _xeqit *it);

typedef struct _xeqit
{
    void  *i_owner;
    t_xeqithook_delay     i_delay_hook;
    t_xeqithook_applypp   i_applypp_hook;
    t_xeqithook_message   i_message_hook;
    t_xeqithook_finish    i_finish_hook;
    t_xeqithook_loopover  i_loopover_hook;
    /* playback position and looping locators */
    t_xeqlocator  i_playloc;  /* next message to look at */
    t_xeqlocator  i_blooploc;
    t_xeqlocator  i_elooploc;
    /* flags */
    int    i_finish;
    int    i_restarted;
    int    i_loopover;
    /* current event */
    int    i_status;
    int    i_channel;
    int    i_data1;
    int    i_data2;
    /* FIXME patterns used in searching, filtering etc. */
    t_symbol  *i_messtarget;
    int        i_messlength;
    t_atom    *i_message;
} t_xeqit;

typedef struct _xeq
{
    t_hyphen      x_this;
    t_outlet     *x_midiout;
    t_outlet     *x_bangout;
    void         *x_binbuf;
    t_clock      *x_clock;
    double        x_whenclockset;  /* real time */
    float         x_clockdelay;    /* user time */
    t_symbol     *x_dir;
    t_canvas     *x_canvas;
    signed char   x_noteons[16][128];
    /* playback parameters */
    t_squtt      *x_ttp;
    int           x_transpo;
    float         x_tempo;
    /* iterators and locators */
    t_xeqit       x_autoit;  /* auto playback state */
    t_xeqit       x_stepit;  /* step playback state */
    t_xeqit       x_walkit;  /* walking state (transient) */
    t_xeqlocator  x_beditloc;
    t_xeqlocator  x_eeditloc;
} t_xeq;

#define XEQ_HOST(x)    ((t_xeq *)((t_hyphen *)x)->x_host)
#define XEQ_BASE(x)    ((t_xeq *)((t_hyphen *)x)->x_basetable)
#define XEQ_NBASES(x)  (((t_hyphen *)x)->x_tablesize)

#define XEQ_FAIL_OK          0
#define XEQ_FAIL_EOS         1  /* end of sequence */
#define XEQ_FAIL_EMPTY       2  /* empty sequence */
#define XEQ_FAIL_CORRUPT     3  /* corrupt sequence */
#define XEQ_FAIL_BADREQUEST  4

int xeq_listparse(int argc, t_atom *argv,
		  int *statusp, int *channelp, int *data1p, int *data2p);
int xeq_applypp(t_xeq *x, t_symbol *trackname,
		int status, int *channelp, int *data1p, int *data2p);

void xeq_rewind(t_xeq *x);
void xeq_stop(t_xeq *x);
void xeq_start(t_xeq *x);
void xeq_loop(t_xeq *x, t_symbol *s, int ac, t_atom *av);

void xeq_tracks(t_xeq *x, t_symbol *s, int ac, t_atom *av);
void xeq_transpo(t_xeq *x, t_floatarg f);
void xeq_tempo(t_xeq *x, t_float f);

t_xeqlocator *xeq_dolocate(t_xeq *x, t_symbol *s, int ac, t_atom *av);
void xeq_locate(t_xeq *x, t_symbol *s, int ac, t_atom *av);
void xeq_find(t_xeq *x, t_symbol *s, int ac, t_atom *av);

t_xeqlocator *xeq_whichloc(t_xeq *x, t_symbol *s);
float xeqlocator_reset(t_xeqlocator *x);
void xeqlocator_hide(t_xeqlocator *x);
int xeqlocator_settoindex(t_xeqlocator *x, int ndx);
float xeqlocator_settotime(t_xeqlocator *x, float when);
float xeqlocator_settolocator(t_xeqlocator *x, t_xeqlocator *reference);
float xeqlocator_move(t_xeqlocator *x, float interval);
float xeqlocator_skipnotes(t_xeqlocator *x, int count);
void xeqlocator_post(t_xeqlocator *loc, char *name);

void xeqit_sethooks(t_xeqit *it, t_xeqithook_delay dhook,
		    t_xeqithook_applypp ahook, t_xeqithook_message mhook,
		    t_xeqithook_finish fhook, t_xeqithook_loopover lhook);
void xeqit_rewind(t_xeqit *it);
int xeqit_loop(t_xeqit *it, t_float lpos, t_float rpos);
int xeqit_reloop(t_xeqit *it);
void xeqit_donext(t_xeqit *it);
void xeqit_settoit(t_xeqit *it, t_xeqit *reference);
int xeqithook_applypp(t_xeqit *it, t_symbol *trackname,
		      int status, int *channelp, int *data1p, int *data2p);

void xeq_tick(t_xeq *x);

t_hyphen *xeq_derived_new(t_class *derivedclass, int tablesize,
			  t_symbol *seqname, t_symbol *refname,
			  t_method tickmethod);
void xeq_derived_free(t_hyphen *x);
void xeq_derived_clone(t_hyphen *x);
void xeq_derived_reembed(t_hyphen *x, t_symbol *seqname);
void xeq_derived_resizeembed(t_hyphen *x, int newtablesize);
int xeq_derived_validate(t_hyphen *x);

void xeq_host_anything(t_hyphen *x, t_symbol *s, int ac, t_atom *av);
void xeq_host_enable(t_class *c, t_newmethod newmethod);

void xeq_setup(void);
void xeq_host_dosetup(void);
void xeq_parse_dosetup(void);
void xeq_polyparse_dosetup(void);
void xeq_record_dosetup(void);
void xeq_follow_dosetup(void);
void xeq_data_dosetup(void);
void xeq_polytempo_dosetup(void);
void xeq_time_dosetup(void);
void xeq_query_dosetup(void);

#endif
