/*
 * $Id: debug.h,v 1.1 2001/07/14 11:23:33 garlick Exp $
 * $Source: /slc/cvs/mlinux/include/asm-sparc/meiko/debug.h,v $
 */

#ifndef _SPARC_MEIKO_DEBUG_H
#define _SPARC_MEIKO_DEBUG_H

#ifndef NDEBUG
#define ASSERT(exp) \
   if (!(exp)) { \
       panic("ASSERT(%s): %s, line %d\n",  #exp, __FILE__, __LINE__); \
   }
#else
#define ASSERT(exp)
#endif

#endif /* _SPARC_MEIKO_DEBUG_H */
