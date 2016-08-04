#ifndef NN_ALLOC_INCLUDED
#define NN_ALLOC_INCLUDED

#include <stddef.h>

/*  These functions allow for interception of memory allocation-related
    functionality. */
    
#define __xstr(s) __str(s)
#define __str(s) #s

#if defined(USE_JEMALLOC)
#define NN_MALLOC_LIB ("jemalloc-" __xstr(JEMALLOC_VERSION_MAJOR) "." __xstr(JEMALLOC_VERSION_MINOR) "." __xstr(JEMALLOC_VERSION_BUGFIX))
#include "jemalloc/jemalloc.h"
#if (JEMALLOC_VERSION_MAJOR == 2 && JEMALLOC_VERSION_MINOR >= 1) || (JEMALLOC_VERSION_MAJOR > 2)
#define HAVE_MALLOC_SIZE 1
#define nn_alloc_size(p) je_malloc_usable_size(p)
#else
#error "Newer version of jemalloc required"
#endif

#elif defined(__APPLE__)
#include <malloc/malloc.h>
#define HAVE_MALLOC_SIZE 1
#define nn_alloc_size(p) malloc_size(p)
#endif

#ifndef NN_MALLOC_LIB
#define NN_MALLOC_LIB "libc"
#endif

#define NN_USED_MEMORY 1
#define NN_USED_BLOCKS 2
#define nn_alloc(size) nn_malloc (size)

//default safe = 0  oom_handler = 0
void nn_alloc_init(int safe, void (*oom_handler)(size_t));
void nn_alloc_term();

void *nn_malloc(size_t size);
void *nn_calloc(size_t size);
void *nn_realloc(void *ptr, size_t size);
void nn_free(void *ptr);

size_t nn_alloc_memory_state(int option); 
size_t nn_alloc_get_rss(void);

char *nn_strdup(const char *s);
#ifndef HAVE_MALLOC_SIZE
size_t nn_alloc_size(void *ptr);
#endif

#endif

