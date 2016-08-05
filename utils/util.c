#include "sleep.h"
#include "err.h"
#include "random.h"
#include "clock.h"
#include "std.h"
#include <string.h>

#ifdef WINDOWS_PLATFORM
#include "win.h"
#else
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#endif

static uint64_t nn_random_state;

void nn_sleep (int milliseconds)
{
#ifdef WINDOWS_PLATFORM
    Sleep (milliseconds);
#else
    int rc;
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = milliseconds % 1000 * 1000000;
    rc = nanosleep (&ts, NULL);
    errno_assert (rc == 0);    
#endif
}

void nn_random_seed ()
{
    uint64_t pid;

#ifdef WINDOWS_PLATFORM
    pid = (uint64_t) GetCurrentProcessId ();
#else
    pid = (uint64_t) getpid ();
#endif

    /*  The initial state for pseudo-random number generator is computed from
        the exact timestamp and process ID. */
    memcpy (&nn_random_state, "\xfa\x9b\x23\xe3\x07\xcc\x61\x1f", 8);
    nn_random_state ^= pid + nn_clock_ms();
}

void nn_random_generate (void *buf, size_t len)
{
    uint8_t *pos;

    pos = (uint8_t*) buf;

    while (1) {

        /*  Generate a pseudo-random integer. */
        nn_random_state = nn_random_state * 1103515245 + 12345;

        /*  Move the bytes to the output buffer. */
        memcpy (pos, &nn_random_state, len > 8 ? 8 : len);
        if (nn_fast (len <= 8))
            return;
        len -= 8;
        pos += 8;
    }
}

