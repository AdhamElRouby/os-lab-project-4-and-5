#include "sequential_compute.h"

#define FILE_PATH "input.txt"

// the aggregate function we're using 
unsigned long add(unsigned long a, unsigned long b) {
    return a + b;
}

// overwriting the file with n lines of "1"
// Returns -1 on error
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
    unsigned long result = sequential_compute(FILE_PATH, add);
    if (result == (unsigned long)-1 && errno != 0) {
        perror("sequential_compute");
        return 1;
    }
    printf("Final result: %lu\n", result);
    return 0;
}