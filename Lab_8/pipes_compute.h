#ifndef PIPES_COMPUTE_H
#define PIPES_COMPUTE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/types.h>

#define BUF_SIZE (1 << 16)

// Pass 1: count the numbers (one per line) without parsing them.
// Returns -1 on error.
unsigned long count_lines(char* filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return -1;
    char buf[BUF_SIZE];
    size_t got;
    unsigned long lines = 0;
    int in_line = 0;
    while ((got = fread(buf, 1, BUF_SIZE, file)) > 0) {
        for (size_t j = 0; j < got; j++) {
            if (buf[j] == '\n') {
                in_line = 0;
            } else if (!in_line) {
                in_line = 1;
                lines++;
            }
        }
    }
    fclose(file);
    return lines;
}

// Pass 2: offsets[i] = byte where line starts[i] begins.
// Returns -1 on error.
int find_offsets(char* filename, unsigned long* starts, off_t* offsets, int n_proc) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return -1;
    char buf[BUF_SIZE];
    size_t got;
    unsigned long number = 0;
    off_t pos = 0;
    int next = 0;
    int in_line = 0;
    while (next < n_proc && (got = fread(buf, 1, BUF_SIZE, file)) > 0) {
        for (size_t j = 0; j < got && next < n_proc; j++) {
            if (buf[j] == '\n') {
                in_line = 0;
            } else if (!in_line) {
                in_line = 1;
                // `while`: children with 0 numbers share the same start line
                while (next < n_proc && number == starts[next]) offsets[next++] = pos + j;
                number++;
            }
        }
        pos += got;
    }
    fclose(file);
    return 0;
}

// Reads the next number: skips empty lines, then the whole line must be digits only
// Returns 0 on success, -1 on invalid line or end of file.
int read_number(FILE* file, unsigned long* x) {
    int c;
    while ((c = getc_unlocked(file)) == '\n');      // skip empty lines
    char line[32];                                   // ULONG_MAX has 20 digits
    int len = 0;
    while (c != EOF && c != '\n') {
        if (len == (int)sizeof(line) - 1) return -1; // too long to be a valid number
        line[len++] = c;
        c = getc_unlocked(file);
    }
    if (len > 0 && line[len - 1] == '\r') len--;     // allow CRLF line endings
    line[len] = '\0';
    if (len == 0 || !isdigit((unsigned char)line[0])) return -1; // empty, spaces, '-', '+', letters
    char* end;
    errno = 0;
    *x = strtoul(line, &end, 10);
    if (*end != '\0' || errno == ERANGE) return -1;  // junk after the digits, or too big
    return 0;
}

// Folds `count` numbers starting at byte `offset` of the file.
// Returns -1 on error; errno = 0 on success.
unsigned long compute(char* filename, off_t offset, unsigned long count,
                      unsigned long (*f)(unsigned long,unsigned long)) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return -1;
    if (fseeko(file, offset, SEEK_SET) != 0) {
        int saved = errno;
        fclose(file);
        errno = saved;
        return -1;
    }
    unsigned long result, x;
    if (read_number(file, &result) == -1) {
        fclose(file);
        errno = EINVAL;
        return -1;
    }
    for (unsigned long i = 1; i < count; i++) {
        if (read_number(file, &x) == -1) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        result = f(result, x);
    }
    fclose(file);
    errno = 0;
    return result;
}

// Returns the result, or -1 with errno set on error; errno = 0 on success.
unsigned long pipes_compute(char* filename, int n_proc, unsigned long (*f)(unsigned long,unsigned long)) {
    if (filename == NULL || f == NULL || n_proc <= 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned long n = count_lines(filename);
    if (n == (unsigned long)-1) return -1;
    if (n == 0) {
        errno = EINVAL;
        return -1;
    }
    int fd[n_proc][2];            // pipe per child: [0] parent reads result, [1] child writes it
    unsigned long starts[n_proc]; // index of child i's first number (line) in the file
    unsigned long counts[n_proc]; // how many numbers (lines) child i processes
    off_t offsets[n_proc];        // byte offset in the file where child i starts reading
    unsigned long div = n / n_proc;
    unsigned long rem = n % n_proc;
    unsigned long start = 0;

    for (int i = 0; i < n_proc; i++) {
        counts[i] = div + ((unsigned long)i < rem);
        starts[i] = start;
        start += counts[i];
    }
    if (find_offsets(filename, starts, offsets, n_proc) == -1) return -1;

    for (int i = 0; i < n_proc; i++) {
        if (pipe(fd[i]) == -1) return -1;
        pid_t pid = fork();
        if (pid == -1) return -1;
        if (pid == 0) {
            // Child process
            close(fd[i][0]); // close read end in child
            if (counts[i] > 0) { // a child with no numbers sends nothing
                unsigned long result = compute(filename, offsets[i], counts[i], f);
                if (result == (unsigned long)-1 && errno != 0) _exit(1);
                if (write(fd[i][1], &result, sizeof(result)) != sizeof(result)) _exit(1);
            }
            close(fd[i][1]); // close write end in child
            _exit(0);
        } else {
            // Parent process
            close(fd[i][1]); // close write end in parent
        }
    }

    int failed = 0;
    unsigned long final_result = 0;
    for (int i = 0; i < n_proc; i++) {
        if (counts[i] > 0 && !failed) { // empty child: nothing to read
            unsigned long result;
            if (read(fd[i][0], &result, sizeof(result)) != sizeof(result)) {
                failed = 1;
            } else {
                final_result = (i == 0) ? result : f(final_result, result);
            }
        }
        close(fd[i][0]);
    }

    // Wait for all child processes to finish to avoid zombie processes
    for(int i = 0; i < n_proc; i++) {
        wait(NULL);
    }

    if (failed) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    return final_result;
}

#endif // PIPES_COMPUTE_H
