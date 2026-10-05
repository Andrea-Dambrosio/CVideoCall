#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>
bool VERBOSE = 0;
int fprintVerbose(FILE *fp, char *format, ...) {
    if (VERBOSE == 0)
        return 0;
    va_list args;
    va_start(args, format);
    int result = vfprintf(fp, format, args);
    va_end(args);
    return result;
}
int printVerbose(char *format, ...) {
    if (VERBOSE == 0)
        return 0;
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}

int xioctl(int fp, int request, void *arg) {
    int r;
    do {
        r = ioctl(fp, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}
int sum(int v[], int n) {
    assert(v != NULL);
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += v[i];
    return sum;
}
