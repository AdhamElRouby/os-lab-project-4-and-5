/* Enable POSIX functions, including clock_gettime() */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE   // MAP_ANONYMOUS on Linux
#define _DARWIN_C_SOURCE  // MAP_ANON on macOS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/* Functions implemented in sequential_compute.h, pipes_compute.h, thread_compute.h and mmap_compute.h */
#include "sequential_compute.h"
#include "pipes_compute.h"
#include "thread_compute.h"
#include "mmap_compute.h"


// benchmarks

#define INPUT_FILE "numbers.txt"
#ifndef FIXED_N
#define FIXED_N 10000000UL    // keeping N constant (big enough that the parallel work dominates the fork / thread overhead)
#endif
#define MIN_PROCESSES 1       // Min processes (threads) is 1
#ifndef MAX_PROCESSES
#define MAX_PROCESSES 640     // testing until 640 processes (threads), past the point where they cross sequential
#endif
#ifndef REPETITIONS
#define REPETITIONS 51        // repeating 51 times for each n_proc (odd for a clean median)
#endif

// the aggregate function to test
unsigned long add(unsigned long a, unsigned long b)
{
    return a + b;
}

// next n_proc to test: step 1 up to 32, step 8 up to 128, then step 32
int next_n_proc(int n_proc)
{
    if (n_proc < 32) return n_proc + 1;
    if (n_proc < 128) return n_proc + 8;
    return n_proc + 32;
}

// calculating the elapsed time in seconds
double elapsed_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

// comparing doubles for qsort
int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

// median of the first n values (sorts the array in place)
double median(double *values, int n)
{
    qsort(values, n, sizeof(double), compare_doubles);
    return (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

// generating an input file containing N 1's
int generate_input(void)
{
    FILE *file = fopen(INPUT_FILE, "w");
    if (file == NULL)
    {
        perror("Opening input file");
        return -1;
    }

    //writing N numbers to the file in big chunks
    static char chunk[1 << 20];
    const unsigned long lines_per_chunk = sizeof(chunk) / 2;
    for (unsigned long i = 0; i < lines_per_chunk; i++) {
        chunk[2 * i] = '1';
        chunk[2 * i + 1] = '\n';
    }
    for (unsigned long written = 0; written < FIXED_N; written += lines_per_chunk) {
        unsigned long lines = (FIXED_N - written < lines_per_chunk) ? FIXED_N - written : lines_per_chunk;
        if (fwrite(chunk, 2, (size_t)lines, file) != (size_t)lines) {
            perror("Writing input file");
            fclose(file);
            return -1;
        }
    }

    if (fclose(file) != 0)
    {
        perror("Closing input file");
        return -1;
    }

    return 0;
}

//function to verify that the 4 functions are returning the same number
int verify_results(int n_proc)
{
    unsigned long sequential = sequential_compute(INPUT_FILE, add);
    unsigned long pipes = pipes_compute(INPUT_FILE, n_proc, add);
    unsigned long thread = thread_compute(INPUT_FILE, n_proc, add);
    unsigned long mapped = mmap_compute(INPUT_FILE, n_proc, add);

    // The sum is supposed to be N so we check whether they all return N
    if (sequential != FIXED_N || pipes != FIXED_N || thread != FIXED_N || mapped != FIXED_N) {
        fprintf(stderr,"ERROR: n_proc=%d, sequential=%lu, pipes=%lu, thread=%lu, mmap=%lu\n",n_proc, sequential, pipes, thread, mapped);
        return 0;
    }
    return 1;
}

// measuring the execution time of the sequential
double time_sequential(void)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned long result = sequential_compute(INPUT_FILE, add);
    clock_gettime(CLOCK_MONOTONIC, &end);
    (void)result;
    return elapsed_seconds(start, end);
}

// measuring the execution time of the pipes
double time_pipes(int n_proc)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned long result = pipes_compute(INPUT_FILE, n_proc, add);
    clock_gettime(CLOCK_MONOTONIC, &end);
    (void)result;
    return elapsed_seconds(start, end);
}

// measuring the execution time of the threads
double time_thread(int n_thread)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned long result = thread_compute(INPUT_FILE, n_thread, add);
    clock_gettime(CLOCK_MONOTONIC, &end);
    (void)result;
    return elapsed_seconds(start, end);
}

// measuring the execution time of the mmap
double time_mmap(int n_proc)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned long result = mmap_compute(INPUT_FILE, n_proc, add);
    clock_gettime(CLOCK_MONOTONIC, &end);
    (void)result;
    return elapsed_seconds(start, end);
}


int main(void)
{
    // raw CSV: every single reading
    FILE *out = fopen("part2_2_raw.csv", "w");
    // median CSV: one row per n_proc (used to build the Excel sheet)
    FILE *medians = fopen("part2_2_median.csv", "w");

    if (out == NULL || medians == NULL)
    {
        perror("Opening results CSV");
        return 1;
    }

    if (fprintf(out,"N,n_proc,repetition,sequential_seconds,pipes_seconds,thread_seconds,mmap_seconds\n") < 0 ||
        fprintf(medians,"N,n_proc,repetitions,sequential_median_seconds,pipes_median_seconds,thread_median_seconds,mmap_median_seconds\n") < 0)
    {
        perror("Writing CSV header");
        return 1;
    }

    // flushing the headers before any fork, otherwise the children re-write the buffered text on exit
    if (fflush(out) != 0 || fflush(medians) != 0)
    {
        perror("Flushing CSV header");
        return 1;
    }

    // generating the input file once
    if (generate_input() != 0) {
        return 1;
    }

    printf("Part 2 (2): fixed N = %lu\n", FIXED_N);
    printf("Processes (threads) tested: %d to %d\n",MIN_PROCESSES, MAX_PROCESSES);
    printf("Repetitions per process count: %d\n", REPETITIONS);
    fflush(stdout);

    // increasing the number of processes (threads)
    // dense where the curve changes fast, sparse where it rises linearly:
    // 1..32 step 1 (around the 10 cores), 40..128 step 8, 160..640 step 32
    for (int n_proc = MIN_PROCESSES; n_proc <= MAX_PROCESSES; n_proc = next_n_proc(n_proc)) {

        // confirming correctness (also an untimed warm-up run)
        if (!verify_results(n_proc)) {
            fprintf(stderr,"Verification failed for n_proc=%d\n", n_proc);
            return 1;
        }
        printf("\n========================================\n");
        printf("N = %lu | n_proc = %d\n", FIXED_N, n_proc);
        printf("========================================\n");

        double sequential_times[REPETITIONS];
        double pipes_times[REPETITIONS];
        double thread_times[REPETITIONS];
        double mmap_times[REPETITIONS];

        for (int trial = 0; trial < REPETITIONS; trial++) {

            double sequential_time = 0;
            double pipes_time = 0;
            double thread_time = 0;
            double mmap_time = 0;

            // rotating the order so no function always runs first
            for (int k = 0; k < 4; k++) {
                switch ((trial + k) % 4) {
                    case 0: sequential_time = time_sequential(); break;
                    case 1: pipes_time = time_pipes(n_proc); break;
                    case 2: thread_time = time_thread(n_proc); break;
                    case 3: mmap_time = time_mmap(n_proc); break;
                }
            }

            // keeping the times for the median
            sequential_times[trial] = sequential_time;
            pipes_times[trial] = pipes_time;
            thread_times[trial] = thread_time;
            mmap_times[trial] = mmap_time;

            /* Print this repetition's results */
            printf("Repetition %3d | Sequential: %.9f s"" | Pipes: %.9f s"" | Thread: %.9f s"" | Mmap: %.9f s\n",trial + 1, sequential_time, pipes_time, thread_time, mmap_time);
            fflush(stdout);

            // saving individual measurements to the csv
            if (fprintf(out, "%lu,%d,%d,%.9f,%.9f,%.9f,%.9f\n",FIXED_N, n_proc, trial + 1,sequential_time, pipes_time, thread_time, mmap_time) < 0)
             {
                perror("Writing measurement");
                return 1;
            }
            fflush(out);
        }
        double sequential_median = median(sequential_times, REPETITIONS);
        double pipes_median = median(pipes_times, REPETITIONS);
        double thread_median = median(thread_times, REPETITIONS);
        double mmap_median = median(mmap_times, REPETITIONS);

        printf("----------------------------------------\n");
        printf("Median | Sequential: %.9f s"" | Pipes: %.9f s"" | Thread: %.9f s"" | Mmap: %.9f s\n", sequential_median, pipes_median, thread_median, mmap_median);
        fflush(stdout);

        // adding median row
        if (fprintf(medians, "%lu,%d,%d,%.9f,%.9f,%.9f,%.9f\n",FIXED_N, n_proc, REPETITIONS, sequential_median, pipes_median, thread_median, mmap_median) < 0)
         {
            perror("Writing median");
            return 1;
        }
        fflush(medians);
    }
    if (fclose(out) != 0 || fclose(medians) != 0)
    {
        perror("Closing results CSV");
        return 1;
    }
    remove(INPUT_FILE);

    printf("\nTest 2 completed successfully :) \n");
    printf("Results saved to part2_2_raw.csv and part2_2_median.csv!!\n");
    return 0;
}
