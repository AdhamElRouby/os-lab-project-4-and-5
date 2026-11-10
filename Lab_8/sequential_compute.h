#ifndef SEQUENTIAL_COMPUTE_H
#define SEQUENTIAL_COMPUTE_H

#include "pipes_compute.h" // reuses count_lines, compute

unsigned long sequential_compute(char* filename, unsigned long (*f)(unsigned long,unsigned long)) {
    if (filename == NULL || f == NULL) {
        errno = EINVAL;
        return -1;
    }
    unsigned long n = count_lines(filename);
    if (n == (unsigned long)-1) return -1;
    if (n == 0) {
        errno = EINVAL;
        return -1;
    }
    return compute(filename, 0, n, f); // the whole file, starting at byte 0
}

#endif // SEQUENTIAL_COMPUTE_H
