#ifndef THREAD_COMPUTE_H
#define THREAD_COMPUTE_H

#include <pthread.h>
#include "pipes_compute.h" // reuses count_lines, find_offsets, compute

typedef struct {
    char* filename;
    off_t offset;                                      // byte where this thread starts reading
    unsigned long count;                               // how many numbers this thread processes
    unsigned long (*f)(unsigned long,unsigned long);
    unsigned long* result;                             // shared: final result (owned by main thread)
    int* has_result;                                   // shared: 1 once result holds a real value
    pthread_mutex_t* lock;                             // shared: protects result and has_result
    int failed;                                        // per-thread: 1 if compute failed
} ThreadArgs;

void* worker(void* arg) {
    ThreadArgs* a = arg;
    unsigned long local = compute(a->filename, a->offset, a->count, a->f);
    if (local == (unsigned long)-1 && errno != 0) {
        a->failed = 1;
        return NULL;
    }
    pthread_mutex_lock(a->lock);
    if (!*a->has_result) {
        *a->result = local;
        *a->has_result = 1;
    } else {
        *a->result = a->f(*a->result, local);
    }
    pthread_mutex_unlock(a->lock);
    return NULL;
}

// Returns the result, or -1 with errno set on error; errno = 0 on success.
// f must be associative and commutative (threads finish in any order).
unsigned long thread_compute(char* filename, int n_thread, unsigned long (*f)(unsigned long,unsigned long)) {
    if (filename == NULL || f == NULL || n_thread <= 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned long n = count_lines(filename);
    if (n == (unsigned long)-1) return -1;
    if (n == 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned long starts[n_thread]; // index of thread i's first number (line) in the file
    unsigned long counts[n_thread]; // how many numbers (lines) thread i processes
    off_t offsets[n_thread];        // byte offset in the file where thread i starts reading
    unsigned long div = n / n_thread;
    unsigned long rem = n % n_thread;
    unsigned long start = 0;

    for (int i = 0; i < n_thread; i++) {
        counts[i] = div + ((unsigned long)i < rem);
        starts[i] = start;
        start += counts[i];
    }
    if (find_offsets(filename, starts, offsets, n_thread) == -1) return -1;

    unsigned long result = 0;
    int has_result = 0;
    pthread_mutex_t lock;
    int rc = pthread_mutex_init(&lock, NULL);
    if (rc != 0) {
        errno = rc;
        return -1;
    }

    ThreadArgs args[n_thread]; // one struct per thread
    pthread_t tids[n_thread];
    int n_created = 0;         // threads actually started (threads with no numbers are skipped)

    for (int i = 0; i < n_thread; i++) {
        if (counts[i] == 0) continue;
        args[i].filename = filename;
        args[i].offset = offsets[i];
        args[i].count = counts[i];
        args[i].f = f;
        args[i].result = &result;
        args[i].has_result = &has_result;
        args[i].lock = &lock;
        args[i].failed = 0;

        rc = pthread_create(&tids[n_created], NULL, worker, &args[i]);
        if (rc != 0) {
            // running threads use main thread's locals: wait for them before returning
            for (int j = 0; j < n_created; j++) pthread_join(tids[j], NULL);
            pthread_mutex_destroy(&lock);
            errno = rc;
            return -1;
        }
        n_created++;
    }

    // created threads are exactly 0..n_created-1 (empty ones are always last), so args[j] matches tids[j]
    int failed = 0;
    for (int j = 0; j < n_created; j++) {
        pthread_join(tids[j], NULL);
        if (args[j].failed) failed = 1;
    }
    pthread_mutex_destroy(&lock);

    if (failed) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    return result;
}

#endif // THREAD_COMPUTE_H
