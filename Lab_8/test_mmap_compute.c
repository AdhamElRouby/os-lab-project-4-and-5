#include "mmap_compute.h"

#define N_PROC 6
#define FILE_PATH "input.txt"

unsigned long add(unsigned long a, unsigned long b) {
    return a + b;
}

int main() {
    unsigned long result = mmap_compute(FILE_PATH, N_PROC, add);
    if (result == (unsigned long)-1 && errno != 0) {
        perror("mmap_compute");
        return 1;
    }
    printf("Final result: %lu\n", result);
    return 0;
}
