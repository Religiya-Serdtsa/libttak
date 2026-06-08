#ifndef BAREMETAL_STDLIB_H
#define BAREMETAL_STDLIB_H

#if defined(EMBEDDED_BAREMETAL)

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void abort(void);

#define RAND_MAX 32767
int rand(void);
void srand(unsigned int seed);

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#else
#  if defined(__has_include_next)
#    if __has_include_next(<stdlib.h>)
#      include_next <stdlib.h>
#    endif
#  else
#    include_next <stdlib.h>
#  endif
#endif

#endif /* BAREMETAL_STDLIB_H */
