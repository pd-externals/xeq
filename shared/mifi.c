/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* reading/writing midifiles, a prototype version */

#ifdef NT
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "m_pd.h"
#include "shared.h"
#include "sq.h"
#include "bifi.h"
#include "mifi.h"

#if 1
#define MIFI_VERBOSE
#if 0
#define MIFI_DEBUG
#endif
#endif

#define MIFI_SHORTEST_EVENT         2  /* singlebyte delta and one databyte */
#define MIFI_EVENT_NALLOC          32  /* LATER do some research (average max?) */
#define MIFI_HEADER_SIZE           14  /* in case t_mifi_header is padded to 16 */
#define MIFI_HEADERDATA_SIZE        6
#define MIFI_TRACKHEADER_SIZE       8

/* header structures for midifile and track */

typedef struct _mifi_header
{
    char    h_type[4];
    uint32  h_length;
    uint16  h_format;
    uint16  h_ntracks;
    uint16  h_division;
} t_mifi_header;

typedef struct _mifi_trackheader
{
    char    h_type[4];
    uint32  h_length;
} t_mifi_trackheader;

/* reading helpers */

static void mifi_earlyeof(t_mifi_stream *x)
{
    x->s_bytesleft = 0;
    x->s_eof = 1;
}

/* Get next byte from track data.
   On error: return 0 (which is a valid result) and set x->s_eof.
*/
static uchar mifi_getbyte(t_mifi_stream *x)
{
    if (x->s_bytesleft)
    {
	int c;
	if ((c = fgetc(x->s_fp)) == EOF)
	{
	    mifi_earlyeof(x);
	    return (0);
	}
	else {
	    x->s_bytesleft--;
	    return ((uchar)c);
	}
    }
    else return (0);
}

static uint32 mifi_readbytes(t_mifi_stream *x, uchar *buf, uint32 size)
{
    size_t res;
    if (size > x->s_bytesleft)
	size = x->s_bytesleft;
    if ((res = fread(buf, 1, (size_t)size, x->s_fp)) == size)
	x->s_bytesleft -= res;
    else
	mifi_earlyeof(x);
    return (res);
}

static int mifi_skipbytes(t_mifi_stream *x, uint32 size)
{
    if (size > x->s_bytesleft)
	size = x->s_bytesleft;
    if (size)
    {
	int res = fseek(x->s_fp, size, SEEK_CUR);
	if (res < 0)
	    mifi_earlyeof(x);
	else
	    x->s_bytesleft -= size;
	return res;
    }
    else return (0);
}

/* helpers handling variable-length quantities */

static size_t mifi_writevarlen(t_mifi_stream *x, uint32 n)
{
    uint32 buf = n & 0x7f;
    size_t length = 1;
    while ((n >>= 7) > 0)
    {
	buf <<= 8;
	buf |= 0x80;
	buf += n & 0x7f;
	length++;
    }
    return ((fwrite(&buf, 1, length, x->s_fp) == length) ? length : 0);
}

static uint32 mifi_readvarlen(t_mifi_stream *x)
{
    uint32 n = 0;
    uchar c;
    uint32 count = x->s_bytesleft;
    if (count > 4) count = 4;
    while (count--)
    {
	n = (n << 7) + ((c = mifi_getbyte(x)) & 0x7f);
	if ((c & 0x80) == 0)
	    break;
    }
    return (n);
}

/* other helpers */

static void mifi_printmeta(t_mifi_stream *x, t_mifi_event *e)
{
    static int isprintable[MIFI_META_MAXPRINTABLE+1] =
    {
#ifdef MIFI_DEBUG
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
#elif defined MIFI_VERBOSE
	0, 0, 1, 1, 1, 1, 1, 1
#endif
    };
    static char *printformat[MIFI_META_MAXPRINTABLE+1] =
    {
	"", "text: %s", "copyright: %s", "track name: %s",
	"instrument name: %s", "lyric: %s", "marker: %s", "cue point: %s"
    };
    if (e->e_meta <= MIFI_META_MAXPRINTABLE)
    {
	if (isprintable[e->e_meta] && printformat[e->e_meta])
	    post(printformat[e->e_meta], e->e_data);
    }
#ifdef MIFI_DEBUG  /* in verbose mode tempo printout done only after sorting */
    else if (e->e_meta == MIFI_META_TEMPO)
	post("tempo %d after %d", x->s_tempo, e->e_delay);
#endif
}

static int mifi_read_start_track(t_mifi_stream *x)
{
    t_mifi_trackheader header;
    long skip;
    int notyet = 1;
    do {
	int readResult = fread(&header, 1, MIFI_TRACKHEADER_SIZE, x->s_fp);
//	printf("mifi_read_start_track; readResult = %d, should be %d\n", readResult, MIFI_TRACKHEADER_SIZE);
	if (readResult < MIFI_TRACKHEADER_SIZE)
	    goto nomoretracks;
	mifi_fix_track_read_header((char *)&header);
	header.h_length = bifi_swap4(header.h_length);
//	printf("mifi_read_start_track; header.h_length: %d (%X) - %d\n", header.h_length, header.h_length, bifi_swap4(header.h_length));
	if (strncmp(header.h_type, "MTrk", 4))
	{
	    char buf[5];
	    strncpy(buf, header.h_type, 4);
	    buf[4] = '\0';
	    if (x->s_anapass)
		post("unknown chunk %s in midifile -- skipped", buf);
	}
	else if (header.h_length < MIFI_SHORTEST_EVENT)
	{
	    if (x->s_anapass) post("empty track in midifile -- skipped");
	}
	else notyet = 0;
	if (notyet && (skip = header.h_length) &&
	    fseek(x->s_fp, skip, SEEK_CUR) < 0)
	    goto nomoretracks;
    } while (notyet);

    x->s_track++;
    x->s_newtrack = 1;
    x->s_status = x->s_channel = 0;
    x->s_bytesleft = header.h_length;
    x->s_time = 0;

    return (1);
nomoretracks:
    if (x->s_track == 0)
	if (x->s_anapass) post("no valid miditracks");
    return (0);
}

/* public interface */

int mifi_event_settext(t_mifi_event *e, int type, char *text)
{
    e->e_delay = 0;
    e->e_status = MIFI_EVENT_META;
    e->e_meta = type;
    e->e_length = strlen(text);
    if (squb_checksize(e, e->e_length + 1, 1) <= e->e_length)
	return (0);
    strcpy(e->e_data, text);
    return (1);
}

void mifi_stream_reset(t_mifi_stream *x)
{
    sq_reset(x);
    x->s_status = x->s_channel = 0;
    x->s_timecoef = sq_msecs2ticks(x, 0);
    x->s_bytesleft = 0;
}

t_mifi_stream *mifi_stream_new(void)
{
    t_mifi_stream *x = sq_new();
    if (!x)
	goto constructorfailure;
    if (!(x->s_auxeve = mifi_event_new()))
	goto constructorfailure;

    x->s_hdtracks = 1;
    x->s_alltracks = 0;

    mifi_stream_reset(x);  /* LATER avoid calling sq_reset() twice */
    return (x);
constructorfailure:
    if (x) mifi_stream_free(x);
    return (0);
}

void mifi_stream_free(t_mifi_stream *x)
{
    if (x->s_auxeve)
	mifi_event_free(x->s_auxeve);
    sq_free(x);
}

t_mifi_event *mifi_event_new(void)
{
    t_mifi_event *e = getbytes(sizeof(*e));
    if (e && !(e->e_data = getbytes(e->e_bufsize = MIFI_EVENT_NALLOC)))
    {
	freebytes(e, sizeof(*e));
	return (0);
    }
    return (e);
}

void mifi_event_free(t_mifi_event *e)
{
    freebytes(e->e_data, e->e_bufsize);
    freebytes(e, sizeof(*e));
}

// Kludge to get the file header in the struct. Modern compilers tend to 
//  align values, preventing a direct copy from the file.
void mifi_fix_read_header(t_mifi_header *header)
{
    // header:
    //   char    h_type[4];   0-3
    //   uint32  h_length;    4-7
    //   uint16  h_format;    8-9
    //   uint16  h_ntracks;  10-11
    //   uint16  h_division; 12-13
    t_mifi_header tmpHeader;

    char *headerChars = (char *)header;
    unsigned int i;
    for (i = 0; i < sizeof(header->h_type); i++) {
	tmpHeader.h_type[i] = headerChars[i];
    }
    tmpHeader.h_length  = headerChars[4]   | (headerChars[5] << 8) | (headerChars[6] << 16) | (headerChars[7] << 24);
    tmpHeader.h_format  = headerChars[8]   | (headerChars[9] << 8);
    tmpHeader.h_ntracks = headerChars[10]  | (headerChars[11] << 8);
    tmpHeader.h_division = headerChars[12] | (headerChars[13] << 8);
    for (i = 0; i < sizeof(header->h_type); i++) {
	header->h_type[i] = tmpHeader.h_type[i];
    }
    header->h_length   = tmpHeader.h_length;
    header->h_format   = tmpHeader.h_format;
    header->h_ntracks  = tmpHeader.h_ntracks;
    header->h_division = tmpHeader.h_division;
}

// Another kludge. As expected, it duplicates code
void mifi_fix_track_read_header(t_mifi_header *header)
{
    t_mifi_trackheader tmpHeader;

    char *headerChars = (char *)header;
    unsigned int i;
    for (i = 0; i < sizeof(header->h_type); i++) {
	tmpHeader.h_type[i] = headerChars[i];
    }
    tmpHeader.h_length  = headerChars[4]   | (headerChars[5] << 8) | (headerChars[6] << 16) | (headerChars[7] << 24);
    header->h_length   = tmpHeader.h_length;
}

// More kludges for writing headers

void mifi_fix_write_header(t_mifi_header *header)
{
    t_mifi_header tmpHeader;
    
    char *headerChars = (char *)header;
   
    unsigned int i;
    for (i = 0; i < sizeof(header->h_type); i++) {
	tmpHeader.h_type[i] = header->h_type[i];
    }
    tmpHeader.h_length   = header->h_length;
    tmpHeader.h_format   = header->h_format;
    tmpHeader.h_ntracks  = header->h_ntracks;
    tmpHeader.h_division = header->h_division;
    
    for (i = 0; i < sizeof(header->h_type); i++) {
	headerChars[i] = tmpHeader.h_type[i];
    }
    headerChars[4]  =  tmpHeader.h_length & 0xFF;
    headerChars[5]  = (tmpHeader.h_length >> 8)  & 0xFF;
    headerChars[6]  = (tmpHeader.h_length >> 16) & 0xFF;
    headerChars[7]  = (tmpHeader.h_length >> 24) & 0xFF;
    headerChars[8]  =  tmpHeader.h_format & 0xFF;
    headerChars[9]  = (tmpHeader.h_format >> 8)  & 0xFF;
    headerChars[10] =  tmpHeader.h_ntracks & 0xFF;
    headerChars[11] = (tmpHeader.h_ntracks >> 8)  & 0xFF;
    headerChars[12] =  tmpHeader.h_division & 0xFF;
    headerChars[13] = (tmpHeader.h_division >> 8)  & 0xFF;
}

void mifi_fix_track_write_header(t_mifi_header *header)
{
    t_mifi_trackheader tmpHeader;
    
    char *headerChars = (char *)header;
    
    unsigned int i;
    for (i = 0; i < sizeof(header->h_type); i++) {
	tmpHeader.h_type[i] = header->h_type[i];
    }
    tmpHeader.h_length = header->h_length;
    
    for (i = 0; i < sizeof(header->h_type); i++) {
	headerChars[i] = tmpHeader.h_type[i];
    }
    headerChars[4] =  tmpHeader.h_length & 0xFF;
    headerChars[5] = (tmpHeader.h_length >> 8)  & 0xFF;
    headerChars[6] = (tmpHeader.h_length >> 16) & 0xFF;
    headerChars[7] = (tmpHeader.h_length >> 24) & 0xFF;
}

/* Open midifile for reading, parse the header.  May be used as t_mifi_stream
   allocator (if x is a null pointer), to be freed by mifi_read_end() or
   explicitly.

   Return value: null on error, else x if passed a valid pointer, else pointer
   to an allocated structure.
*/
t_mifi_stream *mifi_read_start(t_mifi_stream *x,
			       const char *filename, const char *dirname)
{
    t_mifi_stream *result = x;
    t_bifi bifi;
    t_bifi *bp = &bifi;
    t_mifi_header header;
    long skip;

    bifi_new(bp, (char *)&header, MIFI_HEADER_SIZE);
    if (!bifi_read_start(bp, filename, dirname))
    {
	bifi_error_report(bp);
//	bifi_free(bp);
	return (0);
    }
    mifi_fix_read_header((char *)&header);
    if (strncmp(header.h_type, "MThd", 4))
	goto badheader;
    header.h_length   = bifi_swap4(header.h_length);
    header.h_format   = bifi_swap2(header.h_format);
    header.h_ntracks  = bifi_swap2(header.h_ntracks);
    header.h_division = bifi_swap2(header.h_division);
//    printf("mifi_read_start; h_length: %d, h_format: %d, h_ntracks: %d, h_division: %d\n", 
//        header.h_length, header.h_format, header.h_ntracks, header.h_division);
    if (header.h_length < MIFI_HEADERDATA_SIZE)
	goto badheader;
    if (skip = header.h_length - MIFI_HEADERDATA_SIZE)
    {
//	printf("\nmifi_read_start; %ld extra bytes of midifile header (%ld) -- skipped\n", skip, header.h_length);
	post("%ld extra bytes of midifile header -- skipped", skip);
	if (fseek(bp->b_fp, skip, SEEK_CUR) < 0) {
	    result = 0;
	    goto badstart;
	}
    }

    /* since we will tolerate other incompatibilities, now we can allocate */
    if (x) mifi_stream_reset(x);
    else
    {
	if (!(result = mifi_stream_new()))
	    goto badstart;
	result->s_auto = 1;
    }
    result->s_fp = bp->b_fp;
    result->s_format   = header.h_format;
    result->s_hdtracks = header.h_ntracks;
    result->s_nticks   = header.h_division;
    if (result->s_nticks & 0x8000)
    {
	result->s_nframes = (result->s_nticks >> 8);
	result->s_nticks &= 0xff;
    }
    else result->s_nframes = 0;
    if (result->s_nticks == 0)
	goto badheader;

    return (result);
badheader:
//    printf("mifi_read_start; \'%s/%s\' is not a valid midifile\n", dirname, filename);
    post("`%s\' is not a valid midifile", filename);
badstart:
    if (result && !x) mifi_stream_free(result);
//    bifi_free(bp);
    return (0);
}

int mifi_read_restart(t_mifi_stream *x)
{
    FILE *fp = x->s_fp;
    mifi_stream_reset(x);
    x->s_anapass = 0;
    x->s_fp = fp;
    return (fseek(fp, 0, SEEK_SET) ? 0 : 1);
}

/* Close midifile and free t_mifi_stream if it was allocated
   by mifi_read_start() */
void mifi_read_end(t_mifi_stream *x)
{
    if (x->s_fp) fclose(x->s_fp);
    x->s_fp = 0;
    if (x->s_auto) mifi_stream_free(x);
}

/* Read next event from midifile.
   Return value: see #defines in mifi.h.
*/
int mifi_read_event(t_mifi_stream *x, t_mifi_event *e)
{
    uchar status, channel;
    uint32 length;

    x->s_newtrack = 0;
nextattempt:
//printf("mifi_read_event; x->s_bytesleft = %d\n", x->s_bytesleft);
    if (x->s_bytesleft < MIFI_SHORTEST_EVENT && !mifi_read_start_track(x))
	return (MIFI_READ_EOF);

    x->s_time += (e->e_delay = mifi_readvarlen(x));

    if ((status = mifi_getbyte(x)) < 0x80)
    {
	if (MIFI_IS_CHANNEL(x->s_status))
	{
	    e->e_data[0] = status;
	    e->e_length = 1;
	    status = x->s_status;
	    e->e_channel = x->s_channel;
	}
	else {
	    if (x->s_anapass)
		post("missing running status in midifile -- skip to end of track");
	    goto endoftrack;
	}
    }
    else e->e_length = 0;

    /* channel message */
    if (status < 0xf0)
    {
	if (e->e_length == 0)
	{
	    e->e_data[0] = mifi_getbyte(x);
	    e->e_length = 1;
	    x->s_status = status & 0xf0;
	    x->s_channel = e->e_channel = status & 0x0f;
	    status = x->s_status;
	}
	if (!MIFI_ONE_DATABYTE(status))
	{
	    e->e_data[1] = mifi_getbyte(x);
	    e->e_length = 2;
	}
    }

    /* system exclusive */
    else if (status == MIFI_SYSEX_FIRST || status == MIFI_SYSEX_NEXT)
    {
	/* LATER choose the right way --
	   do we really need all those huge bulk dumps? */
	length = mifi_readvarlen(x);
	if (squb_checksize(e, length, 1) < length)
	{
	    if (mifi_skipbytes(x, length) < 0)
		return (MIFI_READ_FATAL);
	    goto nextattempt;
	}
	if (mifi_readbytes(x, e->e_data, length) != length)
	    return (MIFI_READ_FATAL);
	e->e_length = length;
#ifdef MIFI_VERBOSE
	if (x->s_anapass) post("got %d bytes of sysex", length);
#endif
    }

    /* meta-event */
    else if (status == MIFI_EVENT_META)
    {
	e->e_meta = mifi_getbyte(x);
	length = mifi_readvarlen(x);
	if (e->e_meta > 127)
	{
	    /* try to skip corrupted meta-event (quietly) */
#ifdef MIFI_VERBOSE
	    if (x->s_anapass) post("bad meta: %d > 127", e->e_meta);
#endif
	    if (mifi_skipbytes(x, length) < 0)
		return (MIFI_READ_FATAL);
	    goto nextattempt;
	}
	switch (e->e_meta)
	{
	case MIFI_META_EOT:
	    if (length)
	    {
		/* corrupted eot: ignore and skip to the real end of track */
#ifdef MIFI_VERBOSE
		if (x->s_anapass) post("corrupted eot, length %d", length);
#endif
		goto endoftrack;
	    }
	    break;
	case MIFI_META_TEMPO:
	    if (length != 3)
	    {
		if (x->s_anapass)
		    post("corrupted event in midifile -- skip to end of track");
		goto endoftrack;
	    }
	    if (mifi_readbytes(x, e->e_data+1, 3) != 3)
		return (MIFI_READ_FATAL);
	    e->e_data[0] = 0;
	    x->s_tempo = bifi_swap4(*(uint32*)e->e_data);
	    break;
	default:
	    if (squb_checksize(e, length + 1, 1) <= length)
	    {
		if (mifi_skipbytes(x, length) < 0)
		    return (MIFI_READ_FATAL);
		goto nextattempt;
	    }
	    if (mifi_readbytes(x, e->e_data, length) != length)
		return (MIFI_READ_FATAL);
	    e->e_length = length;
	    if (e->e_meta && e->e_meta <= MIFI_META_MAXPRINTABLE)
		e->e_data[length] = '\0';  /* text meta-event nultermination */
	}
    }
    else {
	if (x->s_anapass)
	    post("unknown event type in midifile -- skip to end of track");
	goto endoftrack;
    }

    return ((e->e_status = status) == MIFI_EVENT_META ? e->e_meta : status);

endoftrack:
    if (mifi_skipbytes(x, x->s_bytesleft) < 0)
	return (MIFI_READ_FATAL);
    return (MIFI_READ_SKIP);
}

/* Gather statistics (nevents, ntracks, ntempi), pick track names, and
   allocate the maps.  To be called in the first pass of reading.
   Rationale for two-pass reading: 1) reasonable midifiles are < few
   hundred kb (they should fit in cache); 2) since we'll need space for
   binbuf, which is ca 10 times > file size, we prefer doing binbuf
   allocation after the sizes are known (avoid resizing).
*/
/* LATER consider optional reading of nonchannel events */
int mifi_read_analyse(t_mifi_stream *x, t_squtt *tt)
{
    t_mifi_event *evp = x->s_auxeve;
    int evtype, result = MIFI_READ_FATAL;
    int newtrack = 0, inrange = 0;  /* two flags */
    int i;
    char tnamebuf[MAXPDSTRING];
    t_symbol *tnamesym = 0;
    t_squack *trp = 0;

    *tnamebuf = '\0';
    x->s_alltracks = x->s_ntracks = 0;
    x->s_nevents = 0;
    x->s_ntempi = 0;

    while ((evtype = mifi_read_event(x, evp)) >= MIFI_READ_SKIP)
    {
	if (evtype == MIFI_READ_SKIP) {
//	    printf("mifi_read_analyse; evtype == MIFI_READ_SKIP (%d == %d)\n", evtype, MIFI_READ_SKIP);
	    continue;
        }
	if (x->s_newtrack)
	{
#ifdef MIFI_VERBOSE
	    post("track %d", x->s_track);
#endif
	    newtrack = 1;
	    *tnamebuf = '\0';
	    tnamesym = 0;  /* set to nonzero for inrange nonempty tracks only */
	}
	if (MIFI_IS_CHANNEL(evtype))
	{
//	    printf("MIFI_IS_CHANNEL(evtype) %d\n", evtype);
	    if (newtrack)
	    {
		newtrack = 0;
		x->s_alltracks++;
		if (inrange = ((int)x->s_alltracks >= tt->t_first &&
			       (int)x->s_alltracks <= tt->t_last))
		{
		    if (!(trp = squax_add(x)))
			goto anafail;
		    if (tt->t_default && *tnamebuf)
		    {
			tnamesym = trp->tr_name = gensym(tnamebuf);
#ifdef MIFI_DEBUG
			post("nonempty inrange track name %s",
			     tnamesym->s_name);
#endif
		    }
		    else  /* not default or lacking a name... */
			tnamesym = trp->tr_name = &s_;
		}
	    }
	    if (inrange)
		x->s_nevents++;
	}
	else if (evtype < 0x80)
	{
	    mifi_printmeta(x, evp);
	    if (evtype == MIFI_META_TEMPO)
		x->s_ntempi++;
	    else if (evtype == MIFI_META_TRACKNAME && tt->t_default)
	    {
		char *p1 = evp->e_data;
		if (*p1 && !*tnamebuf)
		{  /* take the first one */
		    while (*p1 == ' ') p1++;
		    if (*p1)
		    {
			char *p2 = evp->e_data + evp->e_length - 1;
			while (p2 > p1 && *p2 == ' ') *p2-- = '\0';
			p2 = p1;
			do if (*p2 == ' ' || *p2 == ',' || *p2 == ';')
			    *p2 = '-';
			while (*++p2);
			if (tnamesym == &s_)
			{  /* trackname after channel-event */
			    if (trp)  /* redundant check */
				tnamesym = trp->tr_name = gensym(p1);
			}
			else strcpy(tnamebuf, p1);
		    }
		}
	    }
	}
    }
    if (evtype != MIFI_READ_EOF)
	goto anafail;

    /* resolve the targets */
    i = x->s_ntracks;
    while (--i >= 0)
    {
	if (!x->s_track_name(i) || x->s_track_name(i) == &s_)
	{
	    if (tt->t_start)
	    {
		sprintf(tnamebuf, "%d%s", i + tt->t_start,
			tt->t_default ? tt->t_default->s_name
			: tt->t_base->s_name);
		x->s_track_name(i) = gensym(tnamebuf);
	    }
	    else x->s_track_name(i) = tt->t_base;
	}
    }

    /* now (re)allocate the buffers */
    if (squb_checksize(x->s_mytempi,
		       x->s_ntempi, sizeof(t_squmpo)) < x->s_ntempi)
	goto anafail;
    x->s_track_nevents(0) = 0;
    x->s_track_nevents(x->s_ntracks) = x->s_nevents;  /* guard point */

    result = evtype;
anafail:
    return (result);
}

/* To be called in second pass of reading */
/* LATER do not trust analysis: in case of inconsistency give up or checksize */
int mifi_read_doit(t_mifi_stream *x, t_squtt *tt)
{
    t_mifi_event *evp = x->s_auxeve;
    t_squiter *it = x->s_myiter;
    t_squiter_seekhook seekhook = squiter_seekhook(it);
    t_squiter_setevehook evehook = squiter_setevehook(it);
    t_squiter_settimhook timhook = squiter_settimhook(it);
    t_squiter_settarhook tarhook = squiter_settarhook(it);
    int evtype, result = MIFI_READ_FATAL;
    int nevents = x->s_nevents;  /* three proxies... */
    int ntracks = x->s_ntracks;
    int ntempi = x->s_ntempi;
    int alltracks;  /* ...not a proxy (used in iteration) */
    int track;
    int newtrack = 0, inrange = 0;  /* two flags */
    int i;
    t_squmpo *tp = x->s_tempomap;
    t_symbol *thistarget = tt->t_base;

    if (!it) goto readfailed;
    seekhook(it, 0);

    alltracks = 0;  /* this counter is for nonempty tracks */
    track = 0;      /* this counter is for inrange nonempty tracks */

    while ((evtype = mifi_read_event(x, evp)) >= MIFI_READ_SKIP)
    {
	if (evtype == MIFI_READ_SKIP)
	    continue;
	if (x->s_newtrack)
	    newtrack = 1;
	if (MIFI_IS_CHANNEL(evtype))
	{
	    int incr;
	    if (newtrack)
	    {
		newtrack = 0;
		alltracks++;
		if (inrange = (alltracks >= tt->t_first &&
			       alltracks <= tt->t_last))
		{
		    thistarget = x->s_track_name(track);
		    track++;
		    if (!thistarget || thistarget == &s_)
		    {
			if (tt->t_start)
			    goto readfailed;  /* this is redundant, anyway... */
			else
			    thistarget = tt->t_base;
		    }
		}
	    }
	    if (!inrange)
		continue;
	    x->s_track_nevents(track)++;
	    evehook(it, (t_squeve *)evp, 0);
	    /* We store onset times instead of delta times, because:
	       1) some deltas may represent delays since nonchannel events;
	       2) we'll need onsets while merging the tracks. */
	    timhook(it, (t_float)x->s_time, 0);
	    tarhook(it, thistarget, &incr);
	    if (!incr)
		goto readfailed;
	}
	else if (evtype < 0x80)
	{
	    if (evtype == MIFI_META_TEMPO)
	    {
		tp->te_onset = x->s_time;
		tp->te_value = x->s_tempo;
		tp++;
	    }
	}
    }
    if (evtype != MIFI_READ_EOF)
	goto readfailed;

    result = evtype;
readfailed:
    return (result);
}

/* Open midifile for saving, write the header.  May be used as t_mifi_stream
   allocator (if x is a null pointer), to be freed by mifi_write_end() or
   explicitly.

   Return value: null on error, else x if passed a valid pointer, else pointer
   to allocated structure.
*/
t_mifi_stream *mifi_write_start(t_mifi_stream *x,
				const char *filename, const char *dirname)
{
    t_mifi_stream *result = x;
    t_bifi bifi;
    t_bifi *bp = &bifi;
    t_mifi_header header;

    /* this must precede bifi_swap() calls */
    bifi_new(bp, (char *)&header, MIFI_HEADER_SIZE);

    if (x->s_format == 0)
    {
	if (x->s_ntracks != 1)
	    goto startfailure;  /* LATER replace with a warning only? */
#ifdef MIFI_VERBOSE
	post("writing singletrack midifile %s", filename);
#endif
    }
#ifdef MIFI_VERBOSE
    else post("writing midifile %s (%d tracks)", filename, x->s_ntracks);
#endif

    strncpy(header.h_type, "MThd", 4);
    header.h_length = bifi_swap4(MIFI_HEADERDATA_SIZE);
    if (x)
    {
	if (!x->s_hdtracks || !x->s_nticks)
	    goto startfailure;
	header.h_format = bifi_swap2(x->s_format);
	header.h_ntracks = bifi_swap2(x->s_hdtracks);
	if (x->s_nframes)
	    header.h_division = ((x->s_nframes << 8) | x->s_nticks) | 0x8000;
	else
	    header.h_division = x->s_nticks & 0x7fff;
	header.h_division = bifi_swap2(header.h_division);
    }
    else {
	header.h_format = 0;
	header.h_ntracks = bifi_swap2(1);
	header.h_division = bifi_swap2(192);  /* LATER parametrize this somehow */
    }
    
    mifi_fix_write_header(&header);
    if (!bifi_write_start(bp, filename, dirname))
    {
	bifi_error_report(bp);
	bifi_free(bp);
	return (0);
    }

    if (x) mifi_stream_reset(x);
    else
    {
	if (!(result = mifi_stream_new()))
	    goto startfailure;
	result->s_auto = 1;
    }
    result->s_fp = bp->b_fp;
    result->s_track = 0;

    return (result);
startfailure:
    if (result && !x) mifi_stream_free(result);
    bifi_free(bp);
    return (0);
}

/* Close midifile, free t_mifi_stream if it was allocated
   by mifi_write_start(). */
void mifi_write_end(t_mifi_stream *x)
{
    if (x->s_auto)
    {
	/* LATER adjust ntracks field in file header, but only if stream was
	   autoallocated.  Number of tracks must be known before calling
	   mifi_write_start() for preexisting stream. */
    }
    if (x->s_fp) fclose(x->s_fp);
    x->s_fp = 0;
    if (x->s_auto) mifi_stream_free(x);
}

int mifi_write_start_track(t_mifi_stream *x)
{
    t_mifi_trackheader header;
    /* LATER check if (x->s_track < x->s_hdtracks)... after some thinking */
    strncpy(header.h_type, "MTrk", 4);
    header.h_length = 0;
    x->s_trackid = x->s_track_id(x->s_track);
    x->s_track++;
    x->s_newtrack = 1;
    x->s_status = x->s_channel = 0;
    x->s_bytesleft = 0;
    x->s_time = 0;
    mifi_fix_track_write_header(&header);
    if (fwrite(&header, 1,
	       MIFI_TRACKHEADER_SIZE, x->s_fp) != MIFI_TRACKHEADER_SIZE)
    {
	post("unable to write midifile header");
	return (0);
    }
    return (1);
}

/* append eot meta and update length field in a track header */
int mifi_write_adjust_track(t_mifi_stream *x, uint32 eotdelay)
{
    t_mifi_event *evp = x->s_auxeve;
    long skip;
    uint32 length;
    evp->e_delay = eotdelay;
    evp->e_status = MIFI_EVENT_META;
    evp->e_meta = MIFI_META_EOT;
    evp->e_length = 0;
    if (!mifi_write_event(x, evp))
	return (0);
    skip = x->s_bytesleft + 4;
    length = bifi_swap4(x->s_bytesleft);
#ifdef MIFI_DEBUG
    post("adjusting track size to %d", x->s_bytesleft);
#endif
    /* LATER add sanity check (compare to saved filepos) */
    if (skip > 4 &&
	fseek(x->s_fp, -skip, SEEK_CUR) < 0 ||
	fwrite(&length, 1, 4, x->s_fp) != 4 ||
	fseek(x->s_fp, 0, SEEK_END) < 0)
    {
	post("unable to adjust length field in midifile track header (length %d)",
	     x->s_bytesleft);
	return (0);
    }
    return (1);
}

/* LATER analyse shrinking effect caused by truncation */
int mifi_write_event(t_mifi_stream *x, t_mifi_event *e)
{
    uchar buf[3], *ptr = buf;
    size_t size = mifi_writevarlen(x, e->e_delay);
    if (!size)
	return (0);
    x->s_bytesleft += size;
    if (MIFI_IS_CHANNEL(e->e_status))
    {
	if ((*ptr = e->e_status | e->e_channel) == x->s_status)
	    size = 1;
	else {
	    x->s_status = *ptr++;
	    size = 2;
	}
	*ptr++ = e->e_data[0];
	if (!MIFI_ONE_DATABYTE(e->e_status))
	{
	    *ptr = e->e_data[1];
	    size++;
	}
	ptr = buf;
    }
    else if (e->e_status == MIFI_EVENT_META)
    {
	x->s_status = 0;  /* sysex and meta-events cancel any running status */
	buf[0] = e->e_status;
	buf[1] = e->e_meta;
	if (fwrite(buf, 1, 2, x->s_fp) != 2)
	    return (0);
	x->s_bytesleft += 2;
	size = mifi_writevarlen(x, (uint32)(e->e_length));
	if (!size)
	    return (0);
	x->s_bytesleft += size;
	size = e->e_length;
	ptr = e->e_data;
    }
    else return (0);
    if (fwrite(ptr, 1, size, x->s_fp) != size)
	return (0);
    x->s_bytesleft += size;
    return (1);
}
