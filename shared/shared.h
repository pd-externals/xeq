/* Copyright (c) 1997-2002 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#ifndef __SHARED_H__
#define __SHARED_H__

#ifndef int32
typedef long int32;
#endif
#ifndef uint32
typedef unsigned long uint32;
#endif
#ifndef int16
typedef short int16;
#endif
#ifndef uint16
typedef unsigned short uint16;
#endif
#ifndef uchar
typedef unsigned char uchar;
#endif

/* LATER find a proper place for #include <limits.h> */
#ifdef INT_MAX
#define SHARED_INT_MAX  INT_MAX
#else
#define SHARED_INT_MAX  0x7FFFFFF
#endif

/* LATER find a proper place for #include <float.h> */
#ifdef FLT_MAX
#define SHARED_FLT_MAX  FLT_MAX
#else
#define SHARED_FLT_MAX  1E+36
#endif

#endif
