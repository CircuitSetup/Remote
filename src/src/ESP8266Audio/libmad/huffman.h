/*
 * libmad - MPEG audio decoder library
 * Copyright (C) 2000-2004 Underbit Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: huffman.h,v 1.11 2004/01/23 09:41:32 rob Exp $
 */

# ifndef LIBMAD_HUFFMAN_H
# define LIBMAD_HUFFMAN_H

# if defined(ESP32)
# include <stdint.h>
// ESP32 supports direct 16-bit flash reads; keep the original layout elsewhere.
typedef uint16_t mad_huffval_t;
# else
typedef unsigned int mad_huffval_t;
# endif

// Use scalar fields instead of bitfields. This'll make the structure way larger, but allow
// for easy direct access w/o any helper functions when placed in PROGMEM

union huffquad {
  struct {
    mad_huffval_t final;
    mad_huffval_t bits;
    mad_huffval_t offset;
  } ptr;
  struct {
    mad_huffval_t final;
    mad_huffval_t hlen;
    mad_huffval_t v;
    mad_huffval_t w;
    mad_huffval_t x;
    mad_huffval_t y;
  } value;
  mad_huffval_t final ;
};

union huffpair {
  struct {
    mad_huffval_t final;
    mad_huffval_t bits;
    mad_huffval_t offset;
  } ptr;
  struct {
    mad_huffval_t final;
    mad_huffval_t hlen;
    mad_huffval_t x;
    mad_huffval_t y;
  } value;
  mad_huffval_t final;
};

struct hufftable {
  union huffpair const *table;
  mad_huffval_t linbits;
  mad_huffval_t startbits;
};

extern union huffquad const *const mad_huff_quad_table[2];
extern struct hufftable const mad_huff_pair_table[32];

# endif
