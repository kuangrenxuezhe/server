#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atomic.h"
#include "alloc.h"

#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)
#else
#if defined(__sun) || defined(__sparc) || defined(__sparc__)
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t))
#endif
#endif

/* Explicitly override malloc/free etc when using jemalloc. */
#if defined(USE_JEMALLOC)
#define malloc(size) je_malloc(size)
#define calloc(count,size) je_calloc(count,size)
#define realloc(ptr,size) je_realloc(ptr,size)
#define free(ptr) je_free(ptr)
#endif

#define update_alloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (nn_malloc_thread_safe) { \
        nn_atomic_inc(&nn_alloc_bytes, _n); \
        nn_atomic_inc(&nn_alloc_blocks, 1); \
    } else { \
        nn_alloc_bytes.n += _n; \
        nn_alloc_blocks.n ++; \
    } \
} while(0)

#define update_alloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (nn_malloc_thread_safe) { \
        nn_atomic_dec(&nn_alloc_bytes, _n); \
        nn_atomic_dec(&nn_alloc_blocks, 1); \
    } else { \
        nn_alloc_bytes.n -= _n; \
        nn_alloc_blocks.n --; \
    } \
} while(0)

#if defined NN_ATOMIC_MUTEX
static struct nn_atomic nn_alloc_bytes={0, 0};
static struct nn_atomic nn_alloc_blocks={0, 0};
#else
static struct nn_atomic nn_alloc_bytes={0};
static struct nn_atomic nn_alloc_blocks={0};
#endif

static int nn_malloc_thread_safe = 0;

static void nn_malloc_default_oom(size_t size) 
{
    fprintf(stderr, "nn_malloc: Out of memory trying to allocate %zu bytes\n",
            size);
    fflush(stderr);
    abort();
}

static void (*nn_malloc_oom_handler)(size_t) = nn_malloc_default_oom;

void nn_alloc_init(int safe, void(*oom_handler)(size_t))
{
    if(safe)
    {
        nn_malloc_thread_safe = 1;
        nn_atomic_init(&nn_alloc_bytes, 0);
        nn_atomic_init(&nn_alloc_blocks, 0);
    }
    if(oom_handler !=0)
        nn_malloc_oom_handler = oom_handler;
}

void nn_alloc_term()
{
    if(!nn_malloc_thread_safe)
        return;
    nn_atomic_term(&nn_alloc_bytes);
    nn_atomic_term(&nn_alloc_blocks);
}

void *nn_malloc(size_t size) 
{
    void *ptr = malloc(size+PREFIX_SIZE);

    if (!ptr) nn_malloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_alloc_stat_alloc(nn_alloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_alloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;
#endif
}

void *nn_calloc(size_t size) 
{
    void *ptr = calloc(1, size+PREFIX_SIZE);

    if (!ptr) nn_malloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_alloc_stat_alloc(nn_alloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_alloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;
#endif
}

void *nn_realloc(void *ptr, size_t size) 
{
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;

    if (ptr == NULL) return nn_malloc(size);
#ifdef HAVE_MALLOC_SIZE
    oldsize = nn_alloc_size(ptr);
    newptr = realloc(ptr,size);
    if (!newptr) nn_malloc_oom_handler(size);

    update_alloc_stat_free(oldsize);
    update_alloc_stat_alloc(nn_alloc_size(newptr));
    return newptr;
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) nn_malloc_oom_handler(size);

    *((size_t*)newptr) = size;
    update_alloc_stat_free(oldsize);
    update_alloc_stat_alloc(size);
    return (char*)newptr+PREFIX_SIZE;
#endif
}

/* Provide nn_alloc_size() for systems where this function is not provided by
 * malloc itself, given that in that case we store a header with this
 * information as the first bytes of every allocation. */
#ifndef HAVE_MALLOC_SIZE
size_t nn_alloc_size(void *ptr) 
{
    void *realptr = (char*)ptr-PREFIX_SIZE;
    size_t size = *((size_t*)realptr);
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1));
    return size+PREFIX_SIZE;
}
#endif

void nn_free(void *ptr) 
{
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif
    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    update_alloc_stat_free(nn_alloc_size(ptr));
    free(ptr);
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    update_alloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
#endif
}

char *nn_strdup(const char *s) 
{
    size_t l = strlen(s)+1;
    char *p = (char*)nn_malloc(l);

    memcpy(p,s,l);
    return p;
}

size_t nn_alloc_memory_state(int option) 
{
    size_t um = -1;
    switch (option)
    {
        case NN_USED_MEMORY:
            um = nn_alloc_bytes.n;
            break;
        case NN_USED_BLOCKS:
            um = nn_alloc_blocks.n;
            break;
        default:
            break;
    }

    return um;
}

#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

size_t nn_alloc_get_rss(void) {
    int page = sysconf(_SC_PAGESIZE);
    size_t rss;
    char buf[4096];
    char filename[256];
    int fd, count;
    char *p, *x;

    snprintf(filename,256,"/proc/%d/stat",getpid());
    if ((fd = open(filename,O_RDONLY)) == -1) return 0;
    if (read(fd,buf,4096) <= 0) {
        close(fd);
        return 0;
    }
    close(fd);

    p = buf;
    count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
    while(p && count--) {
        p = strchr(p,' ');
        if (p) p++;
    }
    if (!p) return 0;
    x = strchr(p,' ');
    if (!x) return 0;
    *x = '\0';

    rss = strtoll(p,NULL,10);
    rss *= page;
    return rss;
}
#else
size_t nn_alloc_get_rss(void) {
    /* If we can't get the RSS in an OS-specific way for this system just
     * return the memory usage we estimated in zmalloc()..
     *
     * Fragmentation will appear to be always 1 (no fragmentation)
     * of course... */
    return nn_alloc_memory_state(NN_USED_MEMORY);
}
#endif
