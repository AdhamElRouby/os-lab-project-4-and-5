// Part II - Question 1:
// If we fix n_proc and n_thread to the same value, then which of the 4 functions take the least time? At which value of N does this behavior persist? 

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE   // MAP_ANONYMOUS, getc_unlocked on Linux
#define _DARWIN_C_SOURCE  // same on macOS

#include <time.h>
#include <math.h>
#include "sequential_compute.h"
#include "mmap_compute.h"
#include "thread_compute.h"

#define INPUT_FILE "numbers.txt"
#ifndef POINTS
#define POINTS 40             // number of N values 
#endif
#ifndef MIN_N
#define MIN_N 1000UL
#endif
#ifndef MAX_N
#define MAX_N 2000000000UL    // 2e9 numbers = 4 GB file. Override with -DMAX_N=...
#endif
#ifndef N_WORKERS
#define N_WORKERS 10          // n_proc = n_thread = 10 (number of performance cores)
#endif
#define MAX_REPETITIONS 21
#define N_FUNCS 4


// used for primting 
const char* NAMES[N_FUNCS] = {"sequential", "pipes", "mmap", "threads"};


// number of repetitions per N , same as last experiment 
int repetitions_for(unsigned long N) {
    if (N < 100000000UL) return 21;
    if (N <= 1000000000UL) return 11;
    return 5;
}

// the aggregate function we chose 
unsigned long add(unsigned long a, unsigned long b) {
    return a + b;
}

// comparing doubles for wsort
int compare_doubles(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

// median of the first n values 
double median(double* values, int n) {
    qsort(values, n, sizeof(double), compare_doubles);
    return (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

// calculating the elapsed time in seconds
double elapsed_seconds(struct timespec start, struct timespec end) 
{
    // converting the difference in seconds and nanoseconds into seconds
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

// generating an input file containing N 1's 

int generate_input(unsigned long N) {
    // opening the input file in write mode
    FILE* file = fopen(INPUT_FILE, "w");
    if (file == NULL) {
        perror("Opening input file");
        return -1;
    }
    static char chunk[1 << 20];
    const unsigned long lines_per_chunk = sizeof(chunk) / 2;
    for (unsigned long i = 0; i < lines_per_chunk; i++) {
        chunk[2 * i] = '1';
        chunk[2 * i + 1] = '\n';
    }
     //writing N numbers to the file in big chunks (fprintf per line is too slow for N = 8e9)
    for (unsigned long written = 0; written < N; written += lines_per_chunk) {
        unsigned long lines = (N - written < lines_per_chunk) ? N - written : lines_per_chunk;
        if (fwrite(chunk, 2, lines, file) != lines) {
            perror("Writing input file");
            fclose(file);
            return -1;
        }
    }

    // close the file 
    if (fclose(file) != 0) {
        perror("Closing input file");
        return -1;
    }
    return 0;
}

// runs function (which) once  --> either one of the 4
unsigned long run(int which) {
    switch (which) {
        case 0:  return sequential_compute(INPUT_FILE, add);
        case 1:  return pipes_compute(INPUT_FILE, N_WORKERS, add);
        case 2:  return mmap_compute(INPUT_FILE, N_WORKERS, add);
        default: return thread_compute(INPUT_FILE, N_WORKERS, add);
    }
}

// times function (which) once, in seconds
double time_run(int which) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned long result = run(which);
    clock_gettime(CLOCK_MONOTONIC, &end);
    (void)result;
    return elapsed_seconds(start, end);
}

// checking that all functions must return N
int verify_results(unsigned long N) {
    int ok = 1;
    for (int k = 0; k < N_FUNCS; k++) {
        unsigned long r = run(k);
        if (r != N) {
            fprintf(stderr, "ERROR: N=%lu, %s returned %lu\n", N, NAMES[k], r);
            ok = 0;
        }
    }
    return ok;
}

int main(void) {
    FILE* out = fopen("part2_1_raw.csv", "w");        // contains every single rum
    FILE* medians = fopen("part2_1_median.csv", "w"); // one row per N (for Excel)
    if (out == NULL || medians == NULL) {
        perror("Opening results CSV");
        return 1;
    }
    fprintf(out, "N,n_workers,repetition,sequential_s,pipes_s,mmap_s,threads_s\n");
    fprintf(medians, "N,n_workers,repetitions,sequential_median_s,pipes_median_s,mmap_median_s,threads_median_s,fastest\n");

    fflush(out);
    fflush(medians);

    printf("Part II (1): fixed n_proc = n_thread = %d\n", N_WORKERS);
    printf("N range: %lu to %lu (%d log-spaced points)\n", MIN_N, MAX_N, POINTS);
    printf("Repetitions per N: 21 (N < 1e8), 11 (1e8 <= N <= 1e9), 5 (N > 1e9)\n");
    fflush(stdout);

    unsigned long all_N[POINTS];
    int fastest[POINTS];
    unsigned long previous_N = 0;

    for (int point = 0; point < POINTS; point++) {
        unsigned long N = (unsigned long)llround((double)MIN_N *
                          pow((double)MAX_N / MIN_N, (double)point / (POINTS - 1)));
        if (N <= previous_N) N = previous_N + 1;
        previous_N = N;
        int repetitions = repetitions_for(N);

        if (generate_input(N) != 0) return 1;
        if (!verify_results(N)) {
            fprintf(stderr, "Verification failed for N=%lu\n", N);
            return 1;
        }
        printf("\n========================================\n");
        printf("Point %d/%d | N = %lu | n_workers = %d | repetitions = %d\n",
               point + 1, POINTS, N, N_WORKERS, repetitions);
        printf("========================================\n");

        double times[N_FUNCS][MAX_REPETITIONS];
        for (int trial = 0; trial < repetitions; trial++) {
            // rotate the order every repetition 
            for (int j = 0; j < N_FUNCS; j++) {
                int k = (trial + j) % N_FUNCS;
                times[k][trial] = time_run(k);
            }
            printf("Rep %2d | seq %.6f | pipes %.6f | mmap %.6f | threads %.6f\n", trial + 1,
                   times[0][trial], times[1][trial], times[2][trial], times[3][trial]);
            fprintf(out, "%lu,%d,%d,%.9f,%.9f,%.9f,%.9f\n", N, N_WORKERS, trial + 1,
                    times[0][trial], times[1][trial], times[2][trial], times[3][trial]);
            fflush(out);
            fflush(stdout);
        }

        // smoothing: median of the repetitions (ignores outliers, unlike the mean)
        double med[N_FUNCS];
        int best = 0;
        for (int k = 0; k < N_FUNCS; k++) {
            med[k] = median(times[k], repetitions);
            if (med[k] < med[best]) best = k;
        }
        all_N[point] = N;
        fastest[point] = best;

        printf("Median | seq %.6f | pipes %.6f | mmap %.6f | threads %.6f | fastest: %s\n",
               med[0], med[1], med[2], med[3], NAMES[best]);
        fprintf(medians, "%lu,%d,%d,%.9f,%.9f,%.9f,%.9f,%s\n", N, N_WORKERS, repetitions,
                med[0], med[1], med[2], med[3], NAMES[best]);
        fflush(medians);
    }

    fclose(out);
    fclose(medians);
    remove(INPUT_FILE); 

    // answer to the question: the overall winner at the largest N,
    // and the smallest N from which it wins at every larger N too
    int winner = fastest[POINTS - 1];
    int from = POINTS - 1;
    while (from > 0 && fastest[from - 1] == winner) from--;
    printf("\n========================================\n");
    printf("Fastest at the largest N: %s\n", NAMES[winner]);
    printf("It stays the fastest for every N >= %lu\n", all_N[from]);
    printf("========================================\n");
    printf("Results saved to part2_1_raw.csv and part2_1_median.csv\n");
    return 0;
}
