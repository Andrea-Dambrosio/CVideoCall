#ifndef ERRORS_HANDLING_H
#define ERRORS_HANDLING_H

void SDL_fatal(const char *format, ...) __attribute__((format(printf, 1, 2)));

void msg_exit(const char *format, ...) __attribute__((format(printf, 1, 2)));

void fatal_errno(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
