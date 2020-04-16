/*
** Copyright 1995-2095, Silicon Graphics, Inc.
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

#include "glslib.h"

const GLubyte __glsCharType[256] = {
    /* nul 0x00 */    0,
    /* soh 0x01 */    0,
    /* stx 0x02 */    0,
    /* etx 0x03 */    0,
    /* eot 0x04 */    0,
    /* enq 0x05 */    0,
    /* ack 0x06 */    0,
    /* bel 0x07 */    0,
    /* bs  0x08 */    0,
    /* ht  0x09 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* nl  0x0A */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* vt  0x0B */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* np  0x0C */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* cr  0x0D */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* so  0x0E */    0,
    /* si  0x0F */    0,
    /* dle 0x10 */    0,
    /* dc1 0x11 */    0,
    /* dc2 0x12 */    0,
    /* dc3 0x13 */    0,
    /* dc4 0x14 */    0,
    /* nak 0x15 */    0,
    /* syn 0x16 */    0,
    /* etb 0x17 */    0,
    /* can 0x18 */    0,
    /* em  0x19 */    0,
    /* sub 0x1A */    0,
    /* esc 0x1B */    0,
    /* fs  0x1C */    0,
    /* gs  0x1D */    0,
    /* rs  0x1E */    0,
    /* us  0x1F */    0,
    /* sp  0x20 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_SPACE_BIT,
    /* !   0x21 */    __GLS_CHAR_GRAPHIC_BIT,
    /* "   0x22 */    __GLS_CHAR_GRAPHIC_BIT,
    /* #   0x23 */    __GLS_CHAR_GRAPHIC_BIT,
    /* $   0x24 */    __GLS_CHAR_GRAPHIC_BIT,
    /* %   0x25 */    __GLS_CHAR_GRAPHIC_BIT,
    /* &   0x26 */    __GLS_CHAR_GRAPHIC_BIT,
    /* '   0x27 */    __GLS_CHAR_GRAPHIC_BIT,
    /* (   0x28 */    __GLS_CHAR_GRAPHIC_BIT,
    /* )   0x29 */    __GLS_CHAR_GRAPHIC_BIT,
    /* *   0x2A */    __GLS_CHAR_GRAPHIC_BIT,
    /* +   0x2B */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* ,   0x2C */    __GLS_CHAR_GRAPHIC_BIT,
    /* -   0x2D */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* .   0x2E */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* /   0x2F */    __GLS_CHAR_GRAPHIC_BIT,
    /* 0   0x30 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 1   0x31 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 2   0x32 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 3   0x33 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 4   0x34 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 5   0x35 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 6   0x36 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 7   0x37 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 8   0x38 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* 9   0x39 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* :   0x3A */    __GLS_CHAR_GRAPHIC_BIT,
    /* ;   0x3B */    __GLS_CHAR_GRAPHIC_BIT,
    /* <   0x3C */    __GLS_CHAR_GRAPHIC_BIT,
    /* =   0x3D */    __GLS_CHAR_GRAPHIC_BIT,
    /* >   0x3E */    __GLS_CHAR_GRAPHIC_BIT,
    /* ?   0x3F */    __GLS_CHAR_GRAPHIC_BIT,
    /* @   0x40 */    __GLS_CHAR_GRAPHIC_BIT,
    /* A   0x41 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* B   0x42 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* C   0x43 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* D   0x44 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* E   0x45 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* F   0x46 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* G   0x47 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* H   0x48 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* I   0x49 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* J   0x4A */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* K   0x4B */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* L   0x4C */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* M   0x4D */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* N   0x4E */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* O   0x4F */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* P   0x50 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* Q   0x51 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* R   0x52 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* S   0x53 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* T   0x54 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* U   0x55 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* V   0x56 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* W   0x57 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* X   0x58 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* Y   0x59 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* Z   0x5A */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* [   0x5B */    __GLS_CHAR_GRAPHIC_BIT,
    /* \   0x5C */    __GLS_CHAR_GRAPHIC_BIT,
    /* ]   0x5D */    __GLS_CHAR_GRAPHIC_BIT,
    /* ^   0x5E */    __GLS_CHAR_GRAPHIC_BIT,
    /* _   0x5F */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* `   0x60 */    __GLS_CHAR_GRAPHIC_BIT,
    /* a   0x61 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* b   0x62 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* c   0x63 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* d   0x64 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* e   0x65 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* f   0x66 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* g   0x67 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* h   0x68 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* i   0x69 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* j   0x6A */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* k   0x6B */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* l   0x6C */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* m   0x6D */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* n   0x6E */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* o   0x6F */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* p   0x70 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* q   0x71 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* r   0x72 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* s   0x73 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* t   0x74 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* u   0x75 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* v   0x76 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* w   0x77 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* x   0x78 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* y   0x79 */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* z   0x7A */    __GLS_CHAR_GRAPHIC_BIT | __GLS_CHAR_TOKEN_BIT,
    /* {   0x7B */    __GLS_CHAR_GRAPHIC_BIT,
    /* |   0x7C */    __GLS_CHAR_GRAPHIC_BIT,
    /* }   0x7D */    __GLS_CHAR_GRAPHIC_BIT,
    /* ~   0x7E */    __GLS_CHAR_GRAPHIC_BIT,
    /* del 0x7F */    0,
    /*     0x80 */    0,
    /*     0x81 */    0,
    /*     0x82 */    0,
    /*     0x83 */    0,
    /*     0x84 */    0,
    /*     0x85 */    0,
    /*     0x86 */    0,
    /*     0x87 */    0,
    /*     0x88 */    0,
    /*     0x89 */    0,
    /*     0x8A */    0,
    /*     0x8B */    0,
    /*     0x8C */    0,
    /*     0x8D */    0,
    /*     0x8E */    0,
    /*     0x8F */    0,
    /*     0x90 */    0,
    /*     0x91 */    0,
    /*     0x92 */    0,
    /*     0x93 */    0,
    /*     0x94 */    0,
    /*     0x95 */    0,
    /*     0x96 */    0,
    /*     0x97 */    0,
    /*     0x98 */    0,
    /*     0x99 */    0,
    /*     0x9A */    0,
    /*     0x9B */    0,
    /*     0x9C */    0,
    /*     0x9D */    0,
    /*     0x9E */    0,
    /*     0x9F */    0,
    /*     0xA0 */    0,
    /*     0xA1 */    0,
    /*     0xA2 */    0,
    /*     0xA3 */    0,
    /*     0xA4 */    0,
    /*     0xA5 */    0,
    /*     0xA6 */    0,
    /*     0xA7 */    0,
    /*     0xA8 */    0,
    /*     0xA9 */    0,
    /*     0xAA */    0,
    /*     0xAB */    0,
    /*     0xAC */    0,
    /*     0xAD */    0,
    /*     0xAE */    0,
    /*     0xAF */    0,
    /*     0xB0 */    0,
    /*     0xB1 */    0,
    /*     0xB2 */    0,
    /*     0xB3 */    0,
    /*     0xB4 */    0,
    /*     0xB5 */    0,
    /*     0xB6 */    0,
    /*     0xB7 */    0,
    /*     0xB8 */    0,
    /*     0xB9 */    0,
    /*     0xBA */    0,
    /*     0xBB */    0,
    /*     0xBC */    0,
    /*     0xBD */    0,
    /*     0xBE */    0,
    /*     0xBF */    0,
    /*     0xC0 */    0,
    /*     0xC1 */    0,
    /*     0xC2 */    0,
    /*     0xC3 */    0,
    /*     0xC4 */    0,
    /*     0xC5 */    0,
    /*     0xC6 */    0,
    /*     0xC7 */    0,
    /*     0xC8 */    0,
    /*     0xC9 */    0,
    /*     0xCA */    0,
    /*     0xCB */    0,
    /*     0xCC */    0,
    /*     0xCD */    0,
    /*     0xCE */    0,
    /*     0xCF */    0,
    /*     0xD0 */    0,
    /*     0xD1 */    0,
    /*     0xD2 */    0,
    /*     0xD3 */    0,
    /*     0xD4 */    0,
    /*     0xD5 */    0,
    /*     0xD6 */    0,
    /*     0xD7 */    0,
    /*     0xD8 */    0,
    /*     0xD9 */    0,
    /*     0xDA */    0,
    /*     0xDB */    0,
    /*     0xDC */    0,
    /*     0xDD */    0,
    /*     0xDE */    0,
    /*     0xDF */    0,
    /*     0xE0 */    0,
    /*     0xE1 */    0,
    /*     0xE2 */    0,
    /*     0xE3 */    0,
    /*     0xE4 */    0,
    /*     0xE5 */    0,
    /*     0xE6 */    0,
    /*     0xE7 */    0,
    /*     0xE8 */    0,
    /*     0xE9 */    0,
    /*     0xEA */    0,
    /*     0xEB */    0,
    /*     0xEC */    0,
    /*     0xED */    0,
    /*     0xEE */    0,
    /*     0xEF */    0,
    /*     0xF0 */    0,
    /*     0xF1 */    0,
    /*     0xF2 */    0,
    /*     0xF3 */    0,
    /*     0xF4 */    0,
    /*     0xF5 */    0,
    /*     0xF6 */    0,
    /*     0xF7 */    0,
    /*     0xF8 */    0,
    /*     0xF9 */    0,
    /*     0xFA */    0,
    /*     0xFB */    0,
    /*     0xFC */    0,
    /*     0xFD */    0,
    /*     0xFE */    0,
    /*     0xFF */    0,
};

GLSenum __glsBinCommand_BeginGLS_getType(
    __GLSbinCommand_BeginGLS *inCommand, __GLSversion *outVersion
) {
    GLuint countLarge = inCommand->head.countLarge;
    GLuint opLarge = inCommand->head.opLarge;
    __GLSversion version = inCommand->version;

    if (inCommand->head.opSmall || inCommand->head.countSmall) return GLS_NONE;
    if (
        opLarge == GLS_OP_glsBeginGLS &&
        countLarge == sizeof(*inCommand) / 4 &&
        (!version.major || version.major == __GLS_VERSION_MAJOR)
    ) {
        *outVersion = version;
        return __GLS_BINARY_SWAP0;
    }
    __glsSwap4(&opLarge);
    __glsSwap4(&countLarge);
    __glsSwap4(&version.major);
    __glsSwap4(&version.minor);
    if (
        opLarge == GLS_OP_glsBeginGLS &&
        countLarge == sizeof(*inCommand) / 4 &&
        (!version.major || version.major == __GLS_VERSION_MAJOR)
    ) {
        *outVersion = version;
        return __GLS_BINARY_SWAP1;
    }
    return GLS_NONE;
}
