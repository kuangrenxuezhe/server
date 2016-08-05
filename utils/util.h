#ifndef NN_UTIL_INCLUDED
#define NN_UTIL_INCLUDED

#include <stddef.h>

/*  Platform independent implementation of sleeping. */
void nn_sleep (int milliseconds);

/*  Seeds the pseudorandom number generator. */
void nn_random_seed ();

/*  Generate a pseudorandom byte sequence. */
void nn_random_generate (void *buf, size_t len);


#endif
