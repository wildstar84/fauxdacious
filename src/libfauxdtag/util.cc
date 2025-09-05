/*
 * util.c
 * Copyright 2009-2014 Paula Stanciu, Tony Vroon, and John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include "util.h"

const char *convert_numericgenre_to_text(int numericgenre)
{
    if (numericgenre < 0 || numericgenre > ID3v1_GENRE_MAX)
        return nullptr;

    return id3v1_genres[numericgenre];
}

uint32_t unsyncsafe32 (uint32_t x)
{
    return (x & 0x7f) | ((x & 0x7f00) >> 1) | ((x & 0x7f0000) >> 2) | ((x & 0x7f000000) >> 3);
}

uint32_t syncsafe32 (uint32_t x)
{
    return (x & 0x7f) | ((x & 0x3f80) << 1) | ((x & 0x1fc000) << 2) | ((x & 0xfe00000) << 3);
}
