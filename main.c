//
//  main.c
//  MemoryManager
//
//  Created by Thomas Foster on 7/24/23.
//

#include "memman.h"
#include <stdio.h>
#include <SDL2/SDL.h>

void TestPerformance(void)
{
    unsigned count = 1000000;

    int start = SDL_GetTicks();;
    for ( unsigned i = 0; i < count; i++ ) {
        int * a = MM_malloc(sizeof(*a));
        *a = 42;
        MM_free(a);
    }
    printf("\nCustom allocator took %d ms\n", SDL_GetTicks() - start);

    start = SDL_GetTicks();;
    for ( unsigned i = 0; i < count; i++ ) {
        int * a = malloc(sizeof(*a));
        *a = 42;
        free(a);
    }
    printf("malloc took %d ms\n", SDL_GetTicks() - start);
}

int main(void)
{
    if ( !MM_Init(256) ) {
        return EXIT_FAILURE;
    }

//    MM_malloc(20);
//    MM_malloc(40);
//    MM_malloc(60);
//    MM_malloc(65);
//    MM_malloc(70);

    for ( int i = 0; i < 10; i++ ) {
        TestPerformance();
    }

    return 0;
}
