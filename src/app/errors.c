#include "SDL3/SDL_error.h"
#include <errno.h>
#include <error.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void clean_up(void);

void SDL_fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, ":%s\n", SDL_GetError());
    clean_up();
    exit(EXIT_FAILURE);
}

void msg_exit(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    clean_up();
    exit(EXIT_FAILURE);
}

void fatal_errno(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, ":%s\n", strerror(errno));
    clean_up();
    exit(EXIT_FAILURE);
}
