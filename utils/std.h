/*
    Copyright (c) 2013 Insollo Entertainment, LLC. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_STD_INCLUDED
#define NN_STD_INCLUDED

#include <stddef.h>

#if defined (_WIN32) || defined (WIN64)
#define WINDOWS_PLATFORM
#endif

#ifdef WINDOWS_PLATFORM //是WINDOWS平台
    typedef __int8            int8_t;
    typedef __int16           int16_t;
    typedef __int32           int32_t;
    typedef __int64           int64_t;
    typedef unsigned __int8   uint8_t;
    typedef unsigned __int16  uint16_t;
    typedef unsigned __int32  uint32_t;
    typedef unsigned __int64  uint64_t;
#else
    #include <sys/types.h>
    typedef u_int8_t    uint8_t;
    typedef u_int16_t   uint16_t;
    typedef u_int32_t   uint32_t;
    typedef u_int64_t   uint64_t;
#endif

#ifdef __linux__
#include <linux/version.h>
#include <features.h>

/* Test for proc filesystem */
#define HAVE_PROC_STAT 1
/* Test for polling API */
#define HAVE_EPOLL 1
/* Test for backtrace */
#define NN_HAVE_BACKTRACE 1
#endif
    
#if defined __GNUC__ || defined __llvm__
#define NN_UNUSED __attribute__ ((unused))
#define nn_fast(x) __builtin_expect ((x), 1)
#define nn_slow(x) __builtin_expect ((x), 0)
#else
#define NN_UNUSED
#define nn_fast(x) (x)
#define nn_slow(x) (x)
#endif

/*  Takes a pointer to a member variable and computes pointer to the structure
    that contains it. 'type' is type of the structure, not the member. */
#define nn_cont(ptr, type, member) \
    (ptr ? ((type*) (((char*) ptr) - offsetof(type, member))) : NULL)

#endif
