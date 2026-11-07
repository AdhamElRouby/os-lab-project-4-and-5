#include "thread_compute.h"

#define N_THREAD 4
#define FILE_PATH "input.txt"

unsigned long add(unsigned long a, unsigned long b) {
    return a + b;
}

// Overwrites the file with n lines of "1". Returns -1 on error.
int write_ones(char* filename, unsigned long n) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) return -1;
    for (unsigned long i = 0; i < n; i++) {
        if (fputs("1\n", file) == EOF) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file);
}

// Usage: ./test_thread_compute [N]   (if N is given, input.txt is overwritten with N ones first)
int main(int argc, char* argv[]) {
    if (argc > 1) {
        char* end;
        errno = 0;
        unsigned long n = strtoul(argv[1], &end, 10);
        if (argv[1][0] == '-' || *end != '\0' || errno != 0) {
            fprintf(stderr, "invalid N: %s\n", argv[1]);
            return 1;
        }
        if (write_ones(FILE_PATH, n) == -1) {
            perror("write_ones");
            return 1;
        }
    }
    unsigned long result = thread_compute(FILE_PATH, N_THREAD, add);
    if (result == (unsigned long)-1 && errno != 0) {
        perror("thread_compute");
        return 1;
    }
    printf("Final result: %lu\n", result);
    return 0;
}
