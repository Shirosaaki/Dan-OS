//
// Created by Shirosaaki on 19/09/25.
//

#ifndef TYPES_H
#define TYPES_H

/* Instead of using 'chars' to allocate non-character bytes,
 * we will use these new type with no semantic meaning */
typedef unsigned int   u32;
typedef          int   s32;
typedef unsigned short u16;
typedef          short s16;
typedef unsigned char  u8;
typedef          char  s8;

/* Standard fixed-width integer types - only define if not already defined */
#ifndef _STDINT_H
#ifndef __STDINT_H
#ifndef _GCC_STDINT_H
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long      uint64_t;
typedef signed long        int64_t;
typedef unsigned long      uintptr_t;
typedef signed long        intptr_t;
#endif
#endif
#endif

/* Size types - only define if not already defined */
#ifndef _STDDEF_H
#ifndef __STDDEF_H
#ifndef _GCC_STDDEF_H
typedef unsigned long      size_t;
typedef signed long        ptrdiff_t;
#endif
#endif
#endif

typedef signed long        ssize_t;

/* NULL definition */
#ifndef NULL
#define NULL ((void *)0)
#endif

#define low_16(address) (u16)((address) & 0xFFFF)
#define high_16(address) (u16)(((address) >> 16) & 0xFFFF)

#endif
