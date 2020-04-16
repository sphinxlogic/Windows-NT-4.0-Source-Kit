/*
** Copyright 1991, 1992, 1993, Silicon Graphics, Inc.
** All Rights Reserved.
**
** This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics, Inc.;
** the contents of this file may not be disclosed to third parties, copied or
** duplicated in any form, in whole or in part, without the prior written
** permission of Silicon Graphics, Inc.
**
** RESTRICTED RIGHTS LEGEND:
** Use, duplication or disclosure by the Government is subject to restrictions
** as set forth in subdivision (c)(1)(ii) of the Rights in Technical Data
** and Computer Software clause at DFARS 252.227-7013, and/or in similar or
** successor clauses in the FAR, DOD or NASA FAR Supplement. Unpublished -
** rights reserved under the Copyright Laws of the United States.
*/
#include "precomp.h"
#pragma hdrstop

/*
** Process the incoming span by calling all of the appropriate span procs.
*/
GLboolean FASTCALL __glProcessSpan(__GLcontext *gc)
{
    GLint m, i;

    m = gc->procs.span.m;

    gc->polygon.shader.done = GL_FALSE;
    for (i = 0; i < m; i++) {
	if ((*gc->procs.span.spanFuncs[i])(gc)) {
	    i++;
	    break;
	}
    }

    if (i != m && !gc->polygon.shader.done) {
	for (; i<m; i++) {
	    if ((*gc->procs.span.stippledSpanFuncs[i])(gc)) {
		break;
	    }
	}
    }

    return GL_FALSE;
}

/*
** Process the incoming span by calling all of the appropriate span procs.
**
** This routine sets gc->polygon.shader.cfb to &gc->frontBuffer and then
** to &gc->backBuffer.
*/
GLboolean FASTCALL __glProcessReplicateSpan(__GLcontext *gc)
{
    GLint n, m, i;
    __GLcolor colors[__GL_MAX_MAX_VIEWPORT], *fcp, *tcp;
    GLint w;

    w = gc->polygon.shader.length;
    n = gc->procs.span.n;
    m = gc->procs.span.m; 

    gc->polygon.shader.done = GL_FALSE;
    for (i = 0; i < n; i++) {
	if ((*gc->procs.span.spanFuncs[i])(gc)) {
	    i++;
	    goto earlyStipple;
	}
    }

    fcp = gc->polygon.shader.colors;
    tcp = colors;
    if (gc->modes.rgbMode) {
	for (i = 0; i < w; i++) {
	    *tcp++ = *fcp++;
	}
    } else {
	for (i = 0; i < w; i++) {
	    tcp->r = fcp->r;
	    fcp++;
	    tcp++;
	}
    }
    ASSERTOPENGL (m == n + 1, "m != n+1, wrong spanProc will be chosen");

    gc->polygon.shader.cfb = &gc->frontBuffer;
	(*gc->frontBuffer.storeSpan)(gc);

    // for (i = n; i < m; i++) {
	// (*gc->procs.span.spanFuncs[i])(gc);
    // }

    fcp = colors;
    tcp = gc->polygon.shader.colors;
    if (gc->modes.rgbMode) {
	for (i = 0; i < w; i++) {
	    *tcp++ = *fcp++;
	}
    } else {
	for (i = 0; i < w; i++) {
	    tcp->r = fcp->r;
	    fcp++;
	    tcp++;
	}
    }
    gc->polygon.shader.cfb = &gc->backBuffer;
	(*gc->backBuffer.storeSpan)(gc);

    // for (i = n; i < m; i++) {
	// (*gc->procs.span.spanFuncs[i])(gc);
    // }

    return GL_FALSE;

earlyStipple:
    if (gc->polygon.shader.done) return GL_FALSE;

    for (; i < n; i++) {
	if ((*gc->procs.span.stippledSpanFuncs[i])(gc)) {
	    return GL_FALSE;
	}
    }

    fcp = gc->polygon.shader.colors;
    tcp = colors;
    if (gc->modes.rgbMode) {
	for (i = 0; i < w; i++) {
	    *tcp++ = *fcp++;
	}
    } else {
	for (i = 0; i < w; i++) {
	    tcp->r = fcp->r;
	    fcp++;
	    tcp++;
	}
    }
    gc->polygon.shader.cfb = &gc->frontBuffer;
	(*gc->frontBuffer.storeStippledSpan)(gc);

    // for (i = n; i < m; i++) {
	// (*gc->procs.span.stippledSpanFuncs[i])(gc);
    // }

    fcp = colors;
    tcp = gc->polygon.shader.colors;
    if (gc->modes.rgbMode) {
	for (i = 0; i < w; i++) {
	    *tcp++ = *fcp++;
	}
    } else {
	for (i = 0; i < w; i++) {
	    tcp->r = fcp->r;
	    fcp++;
	    tcp++;
	}
    }
    gc->polygon.shader.cfb = &gc->backBuffer;
	(*gc->backBuffer.storeStippledSpan)(gc);

    // for (i = n; i < m; i++) {
	// (*gc->procs.span.stippledSpanFuncs[i])(gc);
    // }

    return GL_FALSE;
}

/*
** Perform scissoring on the incoming span, advancing parameter
** values only if necessary.
**
** Returns GL_TRUE if span was entirely (or sometimes when partially) clipped, 
** GL_FALSE otherwise.
*/
GLboolean FASTCALL __glClipSpan(__GLcontext *gc)
{
    GLint clipX0, clipX1, delta;
    GLint x, xr;
    GLint w, w2;
    GLboolean stippled;

    w = gc->polygon.shader.length;

    x = gc->polygon.shader.frag.x;
    stippled = GL_FALSE;
    clipX0 = gc->transform.clipX0;
    clipX1 = gc->transform.clipX1;
    xr = x + w;
    if ((x < clipX0) || (xr > clipX1)) {
	/*
	** Span needs to be scissored in some fashion
	*/
	if ((xr <= clipX0) || (x >= clipX1)) {
	    /* Scissor out the entire span */
	    gc->polygon.shader.done = GL_TRUE;
	    return GL_TRUE;
	}
	if (xr > clipX1) {
	    /*
	    ** Span is clipped by the right edge of the scissor.  This is 
	    ** easy, we will simply reduce the width of this span!
	    */
	    w = clipX1 - x;
	}
	if (x < clipX0) {
	    __GLstippleWord bit, outMask, *osp;
	    GLint count;

	    /*
	    ** Span is clipped by the left edge of the scissor.  This is hard.
	    ** We have two choices.
	    **
	    ** 1) We can stipple the first half of the span.
	    ** 2) We can bump all of the iterator values.
	    **
	    ** The problem with approach number 2 is that the routine 
	    ** which originally asks to have a span processed has assumed 
	    ** that the iterator values will not be munged.  So, if we 
	    ** wanted to implement 2 (which would make this case faster),
	    ** we would need to change that assumption, and make the higher
	    ** routine shadow all of the iterator values, which would slow
	    ** down all paths.  This is probably not a good trade to speed
	    ** this path up, since this path will only occur when the scissor
	    ** region (or window) is smaller than the viewport, and this span
	    ** happens to hit the left edge of the scissor region (or window).
	    **
	    ** Therefore, we choose number 1.
	    */
	    delta = clipX0 - x;

	    osp = gc->polygon.shader.stipplePat;
	    w2 = w;
	    while (w2) {
		count = w2;
		if (count > __GL_STIPPLE_BITS) {
		    count = __GL_STIPPLE_BITS;
		}
		w2 -= count;

		outMask = (__GLstippleWord) ~0;
		bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
		while (--count >= 0) {
		    if (delta > 0) {
			delta--;
			outMask &= ~bit;
		    }
#ifdef __GL_STIPPLE_MSB
		    bit >>= 1;
#else
		    bit <<= 1;
#endif
		}

		*osp++ = outMask;
	    }

	    stippled = GL_TRUE;
	}
    }
    assert(w <= __GL_MAX_MAX_VIEWPORT);

    gc->polygon.shader.length = w;

    return stippled;
}

/*
** Generate the polygon stipple for a span.
*/
GLboolean FASTCALL __glStippleSpan(__GLcontext *gc)
{
    __GLstippleWord stipple;
    __GLstippleWord *sp;
    GLint count;
    GLint shift;
    GLint w;

    w = gc->polygon.shader.length;

    if (gc->constants.yInverted) {
	stipple = gc->polygon.stipple[(gc->constants.height - 
		(gc->polygon.shader.frag.y - gc->constants.viewportYAdjust)-1) 
		& (__GL_STIPPLE_BITS-1)];
    } else {
	stipple = gc->polygon.stipple[gc->polygon.shader.frag.y & 
		(__GL_STIPPLE_BITS-1)];
    }
    shift = gc->polygon.shader.frag.x & (__GL_STIPPLE_BITS - 1);
#ifdef __GL_STIPPLE_MSB
    stipple = (stipple << shift) | (stipple >> (__GL_STIPPLE_BITS - shift));
#else
    stipple = (stipple >> shift) | (stipple << (__GL_STIPPLE_BITS - shift));
#endif
    if (stipple == 0) {
	/* No point in continuing */
	gc->polygon.shader.done = GL_TRUE;
	return GL_TRUE;
    }

    /* Replicate stipple word */
    count = w;
    sp = gc->polygon.shader.stipplePat;
    while (count > 0) {
	*sp++ = stipple;
	count -= __GL_STIPPLE_BITS;
    }

    return GL_TRUE;
}

/*
** Generate the polygon stipple for a stippled span.
*/
GLboolean FASTCALL __glStippleStippledSpan(__GLcontext *gc)
{
    __GLstippleWord stipple;
    __GLstippleWord *sp;
    GLint count;
    GLint shift;
    GLint w;

    w = gc->polygon.shader.length;

    if (gc->constants.yInverted) {
	stipple = gc->polygon.stipple[(gc->constants.height - 
		(gc->polygon.shader.frag.y - gc->constants.viewportYAdjust)-1) 
		& (__GL_STIPPLE_BITS-1)];
    } else {
	stipple = gc->polygon.stipple[gc->polygon.shader.frag.y & 
		(__GL_STIPPLE_BITS-1)];
    }
    shift = gc->polygon.shader.frag.x & (__GL_STIPPLE_BITS - 1);
#ifdef __GL_STIPPLE_MSB
    stipple = (stipple << shift) | (stipple >> (__GL_STIPPLE_BITS - shift));
#else
    stipple = (stipple >> shift) | (stipple << (__GL_STIPPLE_BITS - shift));
#endif
    if (stipple == 0) {
	/* No point in continuing */
	gc->polygon.shader.done = GL_TRUE;
	return GL_TRUE;
    }

    /* Replicate stipple word */
    count = w;
    sp = gc->polygon.shader.stipplePat;
    while (count > 0) {
	*sp++ &= stipple;
	count -= __GL_STIPPLE_BITS;
    }

    return GL_FALSE;
}

/************************************************************************/

/*
** Alpha test span uses a lookup table to do the alpha test function.
** Output a stipple with 1's where the test passed, and 0's where the
** test failed.
*/
GLboolean FASTCALL __glAlphaTestSpan(__GLcontext *gc)
{
    GLubyte *atft;
    GLint failed, count, ia;
    __GLstippleWord bit, outMask, *osp;
    __GLcolor *cp;
    GLint maxAlpha;
    GLint w;

    w = gc->polygon.shader.length;

    atft = &gc->frontBuffer.alphaTestFuncTable[0];
    cp = gc->polygon.shader.colors;
    maxAlpha = gc->constants.alphaTestSize - 1;
    osp = gc->polygon.shader.stipplePat;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    ia = (GLint)(gc->constants.alphaTableConv * cp->a);
	    if (ia < 0) ia = 0;
	    if (ia > maxAlpha) ia = maxAlpha;
	    if (!atft[ia]) {
		/* Test failed */
		outMask &= ~bit;
		failed++;
	    }
	    cp++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*osp++ = outMask;
    }

    if (failed == 0) {
	/* Call next span proc */
	return GL_FALSE;
    } else {
	if (failed != gc->polygon.shader.length) {
	    /* Call next stippled span proc */
	    return GL_TRUE;
	}
    }
    gc->polygon.shader.done = GL_TRUE;
    return GL_TRUE;
}

/*
** Stippled form of alpha test span that checks the stipple at each
** pixel and avoids the test where the stipple disallows it.
*/
GLboolean FASTCALL __glAlphaTestStippledSpan(__GLcontext *gc)
{
    GLubyte *atft;
    GLint count, ia, failed;
    __GLstippleWord bit, inMask, outMask, *isp;
    __GLcolor *cp;
    GLint maxAlpha;
    GLint w;

    w = gc->polygon.shader.length;
    isp = gc->polygon.shader.stipplePat;

    atft = &gc->frontBuffer.alphaTestFuncTable[0];
    cp = gc->polygon.shader.colors;
    maxAlpha = gc->constants.alphaTestSize - 1;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *isp;
	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		ia = (GLint)(gc->constants.alphaTableConv * cp->a);
		if (ia < 0) ia = 0;
		if (ia > maxAlpha) ia = maxAlpha;
		if (!atft[ia]) {
		    /* Test failed */
		    outMask &= ~bit;
		    failed++;
		}
	    } else failed++;
	    cp++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*isp++ = outMask & inMask;
    }

    if (failed != gc->polygon.shader.length) {
	/* Call next stippled span proc */
	return GL_FALSE;
    }
    return GL_TRUE;
}

/************************************************************************/

/*
** Perform stencil testing.  Apply test fail operation as we go.
** Generate a stipple with 1's where the test passed and 0's where the
** test failed.
*/
GLboolean FASTCALL __glStencilTestSpan(__GLcontext *gc)
{
    __GLstencilCell *tft, *sfb, *fail, cell;
    GLint count, failed;
    __GLstippleWord bit, outMask, *osp;
    GLint w;

    w = gc->polygon.shader.length;

    sfb = gc->polygon.shader.sbuf;
    tft = gc->stencilBuffer.testFuncTable;
#ifdef NT
    if (!tft)
        return GL_FALSE;
#endif // NT
    fail = gc->stencilBuffer.failOpTable;
    osp = gc->polygon.shader.stipplePat;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    cell = sfb[0];
	    /* test func table already anded cell values with mask */
	    if (!tft[cell]) {
		/* Test failed */
		outMask &= ~bit;
		sfb[0] = fail[cell];
		failed++;
	    }
	    sfb++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*osp++ = outMask;
    }

    if (failed == 0) {
	return GL_FALSE;
    } else {
	if (failed != gc->polygon.shader.length) {
	    /* Call next proc */
	    return GL_TRUE;
	}
    }
    gc->polygon.shader.done = GL_TRUE;
    return GL_TRUE;
}

/*
** Stippled form of stencil test.
*/
GLboolean FASTCALL __glStencilTestStippledSpan(__GLcontext *gc)
{
    __GLstencilCell *tft, *sfb, *fail, cell;
    GLint failed, count;
    __GLstippleWord bit, inMask, outMask, *sp;
    GLuint smask;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    sfb = gc->polygon.shader.sbuf;
    tft = gc->stencilBuffer.testFuncTable;
#ifdef NT
    if (!tft)
        return GL_FALSE;
#endif // NT
    fail = gc->stencilBuffer.failOpTable;
    smask = gc->state.stencil.mask;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp;
	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		cell = sfb[0];
		if (!tft[cell & smask]) {
		    /* Test failed */
		    outMask &= ~bit;
		    sfb[0] = fail[cell];
		    failed++;
		}
	    } else failed++;
	    sfb++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*sp++ = outMask & inMask;
    }

    if (failed != gc->polygon.shader.length) {
	/* Call next proc */
	return GL_FALSE;
    }
    return GL_TRUE;
}

/************************************************************************/

/*
** Depth test a span, when stenciling is disabled.
*/
GLboolean FASTCALL __glDepthTestSpan(__GLcontext *gc)
{
    __GLzValue z, dzdx, *zfb;
    GLint failed, count;
    GLboolean (FASTCALL *testFunc)( __GLzValue, __GLzValue * );
    GLint stride = gc->depthBuffer.buf.elementSize;
    __GLstippleWord bit, outMask, *osp;
    GLboolean writeEnabled, passed;
    GLint w;

    w = gc->polygon.shader.length;

    zfb = gc->polygon.shader.zbuf;
    testFunc = gc->procs.DTPixel;
    z = gc->polygon.shader.frag.z;
    dzdx = gc->polygon.shader.dzdx;
    writeEnabled = gc->state.depth.writeEnable;
    osp = gc->polygon.shader.stipplePat;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if( (*testFunc)(z, zfb) == GL_FALSE ) {
		outMask &= ~bit;
		failed++;
	    }
	    z += dzdx;
            (GLint) zfb += stride;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*osp++ = outMask;
    }

    if (failed == 0) {
	/* Call next span proc */
	return GL_FALSE;
    } else {
	if (failed != gc->polygon.shader.length) {
	    /* Call next stippled span proc */
	    return GL_TRUE;
	}
    }
    gc->polygon.shader.done = GL_TRUE;
    return GL_TRUE;
}

/*
** Stippled form of depth test span, when stenciling is disabled.
*/
GLboolean FASTCALL __glDepthTestStippledSpan(__GLcontext *gc)
{
    __GLzValue z, dzdx, *zfb;
    GLint failed, count;
    GLboolean (FASTCALL *testFunc)( __GLzValue, __GLzValue * );
    GLint stride = gc->depthBuffer.buf.elementSize;
    __GLstippleWord bit, inMask, outMask, *sp;
    GLboolean writeEnabled, passed;
    GLint w;

    sp = gc->polygon.shader.stipplePat;
    w = gc->polygon.shader.length;

    zfb = gc->polygon.shader.zbuf;
    testFunc = gc->procs.DTPixel;
    z = gc->polygon.shader.frag.z;
    dzdx = gc->polygon.shader.dzdx;
    writeEnabled = gc->state.depth.writeEnable;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp;
	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
	        if( (*testFunc)(z, zfb) == GL_FALSE ) {
		    outMask &= ~bit;
		    failed++;
                }
	    } else failed++;
	    z += dzdx;
            (GLubyte *) zfb += stride;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*sp++ = outMask & inMask;
    }

    if (failed != gc->polygon.shader.length) {
	/* Call next proc */
	return GL_FALSE;
    }
    return GL_TRUE;
}

/*
** Depth test a span when stenciling is enabled.
*/
GLboolean FASTCALL __glDepthTestStencilSpan(__GLcontext *gc)
{
    __GLstencilCell *sfb, *zPassOp, *zFailOp;
    __GLzValue z, dzdx, *zfb;
    GLint failed, count;
    GLboolean (FASTCALL *testFunc)( __GLzValue, __GLzValue * );
    GLint stride = gc->depthBuffer.buf.elementSize;
    __GLstippleWord bit, outMask, *osp;
    GLboolean writeEnabled, passed;
    GLint w;

    w = gc->polygon.shader.length;

    zfb = gc->polygon.shader.zbuf;
    sfb = gc->polygon.shader.sbuf;
    zFailOp = gc->stencilBuffer.depthFailOpTable;
#ifdef NT
    if (!zFailOp)
        return GL_FALSE;
#endif // NT
    zPassOp = gc->stencilBuffer.depthPassOpTable;
    testFunc = gc->procs.DTPixel;
    z = gc->polygon.shader.frag.z;
    dzdx = gc->polygon.shader.dzdx;
    writeEnabled = gc->state.depth.writeEnable;
    osp = gc->polygon.shader.stipplePat;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if( (*testFunc)(z, zfb) ) {
		sfb[0] = zPassOp[sfb[0]];
            } else {
		sfb[0] = zFailOp[sfb[0]];
		outMask &= ~bit;
		failed++;
            }
	    z += dzdx;
            (GLubyte *) zfb += stride;
	    sfb++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*osp++ = outMask;
    }

    if (failed == 0) {
	/* Call next span proc */
	return GL_FALSE;
    } else {
	if (failed != gc->polygon.shader.length) {
	    /* Call next stippled span proc */
	    return GL_TRUE;
	}
    }
    gc->polygon.shader.done = GL_TRUE;
    return GL_TRUE;
}

GLboolean FASTCALL __glDepthTestStencilStippledSpan(__GLcontext *gc)
{
    __GLstencilCell *sfb, *zPassOp, *zFailOp;
    __GLzValue z, dzdx, *zfb;
    GLint failed, count;
    GLboolean (FASTCALL *testFunc)( __GLzValue, __GLzValue * );
    GLint stride = gc->depthBuffer.buf.elementSize;
    __GLstippleWord bit, inMask, outMask, *sp;
    GLboolean writeEnabled, passed;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    zfb = gc->polygon.shader.zbuf;
    sfb = gc->polygon.shader.sbuf;
    testFunc = gc->procs.DTPixel;
    zFailOp = gc->stencilBuffer.depthFailOpTable;
#ifdef NT
    if (!zFailOp)
        return GL_FALSE;
#endif // NT
    zPassOp = gc->stencilBuffer.depthPassOpTable;
    z = gc->polygon.shader.frag.z;
    dzdx = gc->polygon.shader.dzdx;
    writeEnabled = gc->state.depth.writeEnable;
    failed = 0;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp;
	outMask = (__GLstippleWord) ~0;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
	        if( (*testFunc)(z, zfb) ) {
		    sfb[0] = zPassOp[sfb[0]];
                } else {
		    sfb[0] = zFailOp[sfb[0]];
		    outMask &= ~bit;
		    failed++;
                }
	    } else failed++;
	    z += dzdx;
            (GLubyte *) zfb += stride;
	    sfb++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
	*sp++ = outMask & inMask;
    }

    if (failed != gc->polygon.shader.length) {
	/* Call next proc */
	return GL_FALSE;
    }

    return GL_TRUE;
}

/*
** Apply stencil depth pass op when depth testing is off.
*/
GLboolean FASTCALL __glDepthPassSpan(__GLcontext *gc)
{
    __GLstencilCell *sfb, *zPassOp;
    GLint count;
    GLint w;

    w = gc->polygon.shader.length;

    sfb = gc->polygon.shader.sbuf;
    zPassOp = gc->stencilBuffer.depthPassOpTable;
#ifdef NT
    if (!zPassOp)
        return GL_FALSE;
#endif // NT
    count = w;
    while (--count >= 0) {
	sfb[0] = zPassOp[sfb[0]];
	sfb++;
    }

    return GL_FALSE;
}

/*
** Apply stencil depth pass op when depth testing is off.
*/
GLboolean FASTCALL __glDepthPassStippledSpan(__GLcontext *gc)
{
    __GLstencilCell *sfb, *zPassOp;
    GLint count;
    __GLstippleWord bit, inMask, *sp;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    sfb = gc->polygon.shader.sbuf;
    zPassOp = gc->stencilBuffer.depthPassOpTable;
#ifdef NT
    if (!zPassOp)
        return GL_FALSE;
#endif // NT
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		sfb[0] = zPassOp[sfb[0]];
	    }
	    sfb++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    /* Call next proc */
    return GL_FALSE;
}

/************************************************************************/

GLboolean FASTCALL __glShadeCISpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLfloat r, drdx;
    GLint w;

    w = gc->polygon.shader.length;

    r = gc->polygon.shader.frag.color.r;
    drdx = gc->polygon.shader.drdx;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	cp->r = r;
	r += drdx;
	cp++;
    }

    return GL_FALSE;
}

GLboolean FASTCALL __glShadeRGBASpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLfloat r, g, b, a;
    __GLfloat drdx, dgdx, dbdx, dadx;
    GLint w;

    w = gc->polygon.shader.length;

    r = gc->polygon.shader.frag.color.r;
    g = gc->polygon.shader.frag.color.g;
    b = gc->polygon.shader.frag.color.b;
    a = gc->polygon.shader.frag.color.a;
    drdx = gc->polygon.shader.drdx;
    dgdx = gc->polygon.shader.dgdx;
    dbdx = gc->polygon.shader.dbdx;
    dadx = gc->polygon.shader.dadx;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	cp->r = r;
	cp->g = g;
	cp->b = b;
	cp->a = a;
	r += drdx;
	g += dgdx;
	b += dbdx;
	a += dadx;
	cp++;
    }

    return GL_FALSE;
}

GLboolean FASTCALL __glFlatCISpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLfloat r;
    GLint w;

    w = gc->polygon.shader.length;

    r = gc->polygon.shader.frag.color.r;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	cp->r = r;
	cp++;
    }

    return GL_FALSE;
}

GLboolean FASTCALL __glFlatRGBASpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLfloat r, g, b, a;
    GLint w;

    w = gc->polygon.shader.length;

    r = gc->polygon.shader.frag.color.r;
    g = gc->polygon.shader.frag.color.g;
    b = gc->polygon.shader.frag.color.b;
    a = gc->polygon.shader.frag.color.a;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	cp->r = r;
	cp->g = g;
	cp->b = b;
	cp->a = a;
	cp++;
    }

    return GL_FALSE;
}

/************************************************************************/

GLboolean FASTCALL __glTextureSpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLfloat s, t, qw;
    __GLfragment tfrag;/*XXX*/
    GLint w;

    w = gc->polygon.shader.length;

    s = gc->polygon.shader.frag.s;
    t = gc->polygon.shader.frag.t;
    qw = gc->polygon.shader.frag.qw;
    tfrag.x = gc->polygon.shader.frag.x;/*XXX*/
    tfrag.y = gc->polygon.shader.frag.y;/*XXX*/
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	__GLfloat sw, tw, rho;

#ifdef NT
// XXX! (mf) could separate the loops for qw=0, qw!=0
	if( qw == (__GLfloat) 0.0 ) {
	    sw = tw = (__GLfloat) 0.0;
	}
	else {
	    sw = s / qw;
	    tw = t / qw;
	}
#else
	sw = s / qw;
	tw = t / qw;
#endif
	rho = (*gc->procs.calcPolygonRho)(gc, &gc->polygon.shader,
					    s, t, qw);
	(*gc->procs.texture)(gc, cp, sw, tw, rho);
	s += gc->polygon.shader.dsdx;
	t += gc->polygon.shader.dtdx;
	qw += gc->polygon.shader.dqwdx;
	tfrag.x++;
	cp++;
    }

    return GL_FALSE;
}

GLboolean FASTCALL __glTextureStippledSpan(__GLcontext *gc)
{
    __GLstippleWord inMask, bit, *sp;
    GLint count;
    __GLcolor *cp;
    __GLfloat s, t, qw;
    __GLfragment tfrag;/*XXX*/
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    s = gc->polygon.shader.frag.s;
    t = gc->polygon.shader.frag.t;
    qw = gc->polygon.shader.frag.qw;
    tfrag.x = gc->polygon.shader.frag.x;/*XXX*/
    tfrag.y = gc->polygon.shader.frag.y;/*XXX*/
    cp = gc->polygon.shader.colors;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = (__GLstippleWord) __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		__GLfloat sw, tw, rho;
#ifdef NT
	        if( qw == (__GLfloat) 0.0 ) {
	            sw = tw = (__GLfloat) 0.0;
	        }
	        else {
	            sw = s / qw;
	            tw = t / qw;
	        }
#else
	        sw = s / qw;
	        tw = t / qw;
#endif
		rho = (*gc->procs.calcPolygonRho)(gc, &gc->polygon.shader,
						    s, t, qw);
		(*gc->procs.texture)(gc, cp, sw, tw, rho);
	    }
	    s += gc->polygon.shader.dsdx;
	    t += gc->polygon.shader.dtdx;
	    qw += gc->polygon.shader.dqwdx;
	    tfrag.x++;
	    cp++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}

/************************************************************************/

#ifdef NT_DEADCODE_PICKSPAN
/*
** Dither CI span.
*/
GLboolean FASTCALL __glDitherCISpan(__GLcontext *gc)
{
    __GLcolor *cp;
    GLint ix, x, y, r, maxR, frac;
    GLint w;

    w = gc->polygon.shader.length;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = (1 << gc->modes.indexBits) - 1;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	ix = __GL_DITHER_INDEX(x, y);
	frac = __glDitherTable[ix];
	r = ((GLint) (cp->r * __GL_DITHER_PRECISION + __glHalf) + frac)
	    >> __GL_DITHER_BITS;
	if (r > maxR) r = maxR;
	cp->r = r;
	cp++;
	x++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Dither CI stippled span.
*/
GLboolean FASTCALL __glDitherCIStippledSpan(__GLcontext *gc)
{
    __GLstippleWord bit, inMask, *sp;
    __GLcolor *cp;
    GLint count, ix, x, y, r, maxR, frac;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = (1 << gc->modes.indexBits) - 1;
    cp = gc->polygon.shader.colors;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		ix = __GL_DITHER_INDEX(x, y);
		frac = __glDitherTable[ix];
		r = ((GLint) (cp->r * __GL_DITHER_PRECISION + __glHalf) + frac)
		    >> __GL_DITHER_BITS;
		if (r > maxR) r = maxR;
		cp->r = r;
	    }
	    cp++;
	    x++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Dither RGBA span.  Only works if incoming colors have already been
** scaled into their destination format.
*/
GLboolean FASTCALL __glDitherRGBASpan(__GLcontext *gc)
{
    __GLcolor *cp;
    GLint ix, x, y, r, g, b, a, frac;
    GLint maxR, maxG, maxB, maxA;
    GLint w;

    w = gc->polygon.shader.length;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = gc->frontBuffer.iRedScale;
    maxG = gc->frontBuffer.iGreenScale;
    maxB = gc->frontBuffer.iBlueScale;
    maxA = gc->frontBuffer.iAlphaScale;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	ix = __GL_DITHER_INDEX(x, y);
	/*
	** Convert the color into integers, keeping 4 bits of fractional
	** precision (and rounding up).
	*/
	frac = __glDitherTable[ix];
	r = ((GLint) (cp->r * __GL_DITHER_PRECISION + __glHalf) + frac)
	    >> __GL_DITHER_BITS;
	if (r > maxR) r = maxR;
	cp->r = r;

	g = ((GLint) (cp->g * __GL_DITHER_PRECISION + __glHalf) + frac)
	    >> __GL_DITHER_BITS;
	if (g > maxG) g = maxG;
	cp->g = g;

	b = ((GLint) (cp->b * __GL_DITHER_PRECISION + __glHalf) + frac)
	    >> __GL_DITHER_BITS;
	if (b > maxB) b = maxB;
	cp->b = b;

	a = ((GLint) (cp->a * __GL_DITHER_PRECISION + __glHalf) + frac)
	    >> __GL_DITHER_BITS;
	if (a > maxA) a = maxA;
	cp->a = a;

	cp++;
	x++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Dither RGBA stippled span.  Only works if incoming colors have already
** been scaled into their destination format.
*/
GLboolean FASTCALL __glDitherRGBAStippledSpan(__GLcontext *gc)
{
    __GLstippleWord bit, inMask, *sp;
    __GLcolor *cp;
    GLint count, ix, x, y, r, g, b, a, frac;
    GLint maxR, maxG, maxB, maxA;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = gc->frontBuffer.iRedScale;
    maxG = gc->frontBuffer.iGreenScale;
    maxB = gc->frontBuffer.iBlueScale;
    maxA = gc->frontBuffer.iAlphaScale;
    cp = gc->polygon.shader.colors;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		ix = __GL_DITHER_INDEX(x, y);
		/*
		** Convert the color into integers, keeping 4 bits of
		** fractional precision (and rounding up).
		*/
		frac = __glDitherTable[ix];
		r = ((GLint) (cp->r * __GL_DITHER_PRECISION + __glHalf) + frac)
		    >> __GL_DITHER_BITS;
		if (r > maxR) r = maxR;
		cp->r = r;

		g = ((GLint) (cp->g * __GL_DITHER_PRECISION + __glHalf) + frac)
		    >> __GL_DITHER_BITS;
		if (g > maxG) g = maxG;
		cp->g = g;

		b = ((GLint) (cp->b * __GL_DITHER_PRECISION + __glHalf) + frac)
		    >> __GL_DITHER_BITS;
		if (b > maxB) b = maxB;
		cp->b = b;

		a = ((GLint) (cp->a * __GL_DITHER_PRECISION + __glHalf) + frac)
		    >> __GL_DITHER_BITS;
		if (a > maxA) a = maxA;
		cp->a = a;
	    }
	    cp++;
	    x++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Round CI span.
*/
GLboolean __glRoundCISpan(__GLcontext *gc)
{
    __GLcolor *cp;
    GLint x, y, r, maxR;
    GLint w;

    w = gc->polygon.shader.length;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = (1 << gc->modes.indexBits) - 1;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	r = (GLint)(cp->r + __glHalf);
	if (r > maxR) { r = maxR; }
	cp->r = r;
	cp++;
	x++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Round CI stippled span.
*/
GLboolean FASTCALL __glRoundCIStippledSpan(__GLcontext *gc)
{
    __GLstippleWord bit, inMask, *sp;
    __GLcolor *cp;
    GLint count, x, y, r, maxR;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = (1 << gc->modes.indexBits) - 1;
    cp = gc->polygon.shader.colors;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		r = (GLint)(cp->r + __glHalf);
		if (r > maxR) { r = maxR; }
		cp->r = r;
	    }
	    cp++;
	    x++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Round RGBA span.  Only works if incoming colors have already been
** scaled into their destination format.
*/
GLboolean FASTCALL __glRoundRGBASpan(__GLcontext *gc)
{
    __GLcolor *cp;
    GLint x, y, r, g, b, a;
    GLint maxR, maxG, maxB, maxA;
    GLint w;

    w = gc->polygon.shader.length;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = gc->frontBuffer.iRedScale;
    maxG = gc->frontBuffer.iGreenScale;
    maxB = gc->frontBuffer.iBlueScale;
    maxA = gc->frontBuffer.iAlphaScale;
    cp = gc->polygon.shader.colors;
    while (--w >= 0) {
	r = (GLint) (cp->r + __glHalf);
	if (r > maxR) r = maxR;
	cp->r = r;
	g = (GLint) (cp->g + __glHalf);
	if (g > maxG) g = maxG;
	cp->g = g;
	b = (GLint) (cp->b + __glHalf);
	if (b > maxB) b = maxB;
	cp->b = b;
	a = (GLint) (cp->a + __glHalf);
	if (a > maxA) a = maxA;
	cp->a = a;
	cp++;
	x++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
/*
** Round RGBA stippled span.  Only works if incoming colors have already
** been scaled into their destination format.
*/
GLboolean FASTCALL __glRoundRGBAStippledSpan(__GLcontext *gc)
{
    __GLstippleWord bit, inMask, *sp;
    __GLcolor *cp;
    GLint count, x, y, r, g, b, a;
    GLint maxR, maxG, maxB, maxA;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    x = gc->polygon.shader.frag.x;
    y = gc->polygon.shader.frag.y;
    maxR = gc->frontBuffer.iRedScale;
    maxG = gc->frontBuffer.iGreenScale;
    maxB = gc->frontBuffer.iBlueScale;
    maxA = gc->frontBuffer.iAlphaScale;
    cp = gc->polygon.shader.colors;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		r = (GLint) (cp->r + __glHalf);
		if (r > maxR) r = maxR;
		cp->r = r;
		g = (GLint) (cp->g + __glHalf);
		if (g > maxG) g = maxG;
		cp->g = g;
		b = (GLint) (cp->b + __glHalf);
		if (b > maxB) b = maxB;
		cp->b = b;
		a = (GLint) (cp->a + __glHalf);
		if (a > maxA) a = maxA;
		cp->a = a;
	    }
	    cp++;
	    x++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

/************************************************************************/

#ifdef NT_DEADCODE_PICKSPAN
GLboolean FASTCALL __glLogicOpSpan(__GLcontext *gc)
{
    __GLcolor *cp, *fcp;
    GLint x, y, color, fbcolor;
    GLint w;

    w = gc->polygon.shader.length;

    cp = gc->polygon.shader.colors;
    fcp = gc->polygon.shader.fbcolors;
    y = gc->polygon.shader.frag.y;
    x = gc->polygon.shader.frag.x;
    while (--w >= 0) {
	color = (GLint) cp->r;
	fbcolor = (GLint) fcp->r;
	switch (gc->state.raster.logicOp) {
	  case GL_CLEAR:         color = 0; break;
	  case GL_AND:           color = color & fbcolor; break;
	  case GL_AND_REVERSE:   color = color & (~fbcolor); break;
	  case GL_COPY:          color = color; break;
	  case GL_AND_INVERTED:  color = (~color) & fbcolor; break;
	  case GL_NOOP:          color = fbcolor; break;
	  case GL_XOR:           color = color ^ fbcolor; break;
	  case GL_OR:            color = color | fbcolor; break;
	  case GL_NOR:           color = ~(color | fbcolor); break;
	  case GL_EQUIV:         color = ~(color ^ fbcolor); break;
	  case GL_INVERT:        color = ~fbcolor; break;
	  case GL_OR_REVERSE:    color = color | (~fbcolor); break;
	  case GL_COPY_INVERTED: color = ~color; break;
	  case GL_OR_INVERTED:   color = (~color) | fbcolor; break;
	  case GL_NAND:          color = ~(color & fbcolor); break;
	  case GL_SET:           color = ~0; break;
	}
	cp->r = color;
	cp++;
	fcp++;
	x++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
GLboolean FASTCALL __glLogicOpStippledSpan(__GLcontext *gc)
{
    __GLstippleWord bit, inMask, *sp;
    __GLcolor *cp, *fcp;
    GLint count, x, y, color, fbcolor;
    GLint w;

    w = gc->polygon.shader.length;
    sp = gc->polygon.shader.stipplePat;

    cp = gc->polygon.shader.colors;
    fcp = gc->polygon.shader.fbcolors;
    y = gc->polygon.shader.frag.y;
    x = gc->polygon.shader.frag.x;
    while (w) {
	count = w;
	if (count > __GL_STIPPLE_BITS) {
	    count = __GL_STIPPLE_BITS;
	}
	w -= count;

	inMask = *sp++;
	bit = __GL_STIPPLE_SHIFT(0);
	while (--count >= 0) {
	    if (inMask & bit) {
		color = (GLint) cp->r;
		fbcolor = (GLint) fcp->r;
		switch (gc->state.raster.logicOp) {
		  case GL_CLEAR:         color = 0; break;
		  case GL_AND:           color = color & fbcolor; break;
		  case GL_AND_REVERSE:   color = color & (~fbcolor); break;
		  case GL_COPY:          color = color; break;
		  case GL_AND_INVERTED:  color = (~color) & fbcolor; break;
		  case GL_NOOP:          color = fbcolor; break;
		  case GL_XOR:           color = color ^ fbcolor; break;
		  case GL_OR:            color = color | fbcolor; break;
		  case GL_NOR:           color = ~(color | fbcolor); break;
		  case GL_EQUIV:         color = ~(color ^ fbcolor); break;
		  case GL_INVERT:        color = ~fbcolor; break;
		  case GL_OR_REVERSE:    color = color | (~fbcolor); break;
		  case GL_COPY_INVERTED: color = ~color; break;
		  case GL_OR_INVERTED:   color = (~color) | fbcolor; break;
		  case GL_NAND:          color = ~(color & fbcolor); break;
		  case GL_SET:           color = ~0; break;
		}
		cp->r = color;
	    }
	    cp++;
	    fcp++;
	    x++;
#ifdef __GL_STIPPLE_MSB
	    bit >>= 1;
#else
	    bit <<= 1;
#endif
	}
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

/************************************************************************/

#ifdef NT_DEADCODE_PICKSPAN
GLboolean FASTCALL __glMaskRGBASpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLcolor *fcp;
    GLboolean rEnable, gEnable, bEnable, aEnable;
    GLint w;

    w = gc->polygon.shader.length;

    rEnable = gc->state.raster.rMask;
    gEnable = gc->state.raster.gMask;
    bEnable = gc->state.raster.bMask;
    aEnable = gc->state.raster.aMask;
    cp = gc->polygon.shader.colors;
    fcp = gc->polygon.shader.fbcolors;
    while (--w >= 0) {
	cp->r = rEnable ? cp->r : fcp->r;
	cp->g = gEnable ? cp->g : fcp->g;
	cp->b = bEnable ? cp->b : fcp->b;
	cp->a = aEnable ? cp->a : fcp->a;
	cp++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN

#ifdef NT_DEADCODE_PICKSPAN
GLboolean FASTCALL __glMaskCISpan(__GLcontext *gc)
{
    __GLcolor *cp;
    __GLcolor *fcp;
    GLuint keepMask, changeMask, r, fr;
    GLint w;

    w = gc->polygon.shader.length;
    changeMask = gc->state.raster.writeMask;
    keepMask = __GL_MASK_INDEXI(gc, ~changeMask);
    cp = gc->polygon.shader.colors;
    fcp = gc->polygon.shader.fbcolors;
    while (--w >= 0) {
	r = (GLuint) cp->r;
	fr = (GLuint) fcp->r;
	cp->r = (r & changeMask) | (fr & keepMask);
	cp++;
    }

    return GL_FALSE;
}
#endif // NT_DEADCODE_PICKSPAN
