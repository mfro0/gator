/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/atiproto.h,v 1.9 2004/01/05 16:42:04 tsi Exp $ */
/*
 * Copyright 1997 through 2004 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of Marc Aurele La France not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Marc Aurele La France makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as-is" without express or implied warranty.
 *
 * MARC AURELE LA FRANCE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL MARC AURELE LA FRANCE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ___ATIPROTO_H___
#define ___ATIPROTO_H___ 1

#include "Xfuncproto.h"

/*
 * This isn't quite ready for Xfuncproto.h yet.
 */

#ifndef FunctionPrototype
#   if NeedFunctionPrototypes
#       define FunctionPrototype(FunctionArgumentTypes) FunctionArgumentTypes
#   else
#       define FunctionPrototype(FunctionArgumentTypes) ()
#   endif
#endif

#ifndef NestedPrototype
#   if NeedNestedPrototypes
#       define NestedPrototype(NestedArgumentTypes) NestedArgumentTypes
#   else
#       define NestedPrototype(NestedArgumentTypes) ()
#   endif
#endif

#endif /* ___ATIPROTO_H___ */
