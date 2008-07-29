/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 * Copyright 2007 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */
#ifndef VA_COPY_H
#define VA_COPY_H

#include <stdarg.h>

#ifdef HAVE_VA_COPY
  /* C99: use va_copy(), and match it with calls to va_end(). */
  #define VA_COPY(dest, src)      va_copy(dest, src)
  #define VA_COPY_END(dest)       va_end(dest)
#elif defined(HAVE_UNDERSCORE_UNDERSCORE_VA_COPY)
  /* Earlier drafts of the C99 standard used __va_copy(). */
  #define VA_COPY(dest, src)      __va_copy(dest, src)
  #define VA_COPY_END(dest)       va_end(dest)
#else
  /* Pre-C99: the best we can do is to assume that va_list
     values can be freely copied.  This works on most (but
     not all) pre-C99 C implementations. */
  #define VA_COPY(dest, src)      ((dest) = (src), (void) 0)
  #define VA_COPY_END(dest)       ((void) 0)
#endif

#endif /* VA_COPY_H */
