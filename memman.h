#ifndef MEMMAN_H
#define MEMMAN_H

#include <stdbool.h>
#include <stdlib.h>

#define MM_VERBOSE

bool    MM_Init(size_t size);

void *  MM_malloc(size_t size);
void *  MM_calloc(size_t count, size_t size);
void    MM_free(void * memory);

#endif /* MEMMAN_H */
