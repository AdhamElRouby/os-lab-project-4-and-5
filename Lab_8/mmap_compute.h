#ifndef MMAP_COMPUTE_H
#define MMAP_COMPUTE_H

#include <sys/mman.h> // for mmap() and munmap()
#include "pipes_compute.h" // reuses count_lines, find_offsets, compute

// #ifndef MAP_ANONYMOUS
// #define MAP_ANONYMOUS MAP_ANON
// #endif


// one slot per child in the shared region
// every child writes to its own slot 
typedef struct {
    unsigned long result; // the created child's partial result
    int done;             // = 1 means the child has finished and result is valid
} SharedSlot;


// function returns an unsigned long 
// takes as inputs the path to the file with the numbers, the number of processes, and a pointer to the aggregate function
unsigned long mmap_compute(char* filename, int n_proc, unsigned long (*f)(unsigned long,unsigned long)) {

    // validating the inputs and returning 
    if (filename == NULL || f == NULL || n_proc <= 0) {
        errno = EINVAL; // invalid arguments 
        return -1;
    }

    // counting the number of lines ( change to handle commas and white spaces!!)
    unsigned long n = count_lines(filename);

    //if we couldn't open the file 
    if (n == (unsigned long)-1) return -1;  // ( unsigned long) - 1 is the value of (-1) when converted to unsigned long 
    if (n == 0) // handles empty files
    {
        errno = EINVAL;
        return -1;
    }
    unsigned long starts[n_proc]; // starts[i] is the index of child i's first number in the file
    unsigned long counts[n_proc]; // how many numbers child i processes
    off_t offsets[n_proc];        // byte position in the file where the num starts so the child can jump there

    // calculating the quotient and the remainder of n/n_proc
    unsigned long div = n / n_proc; // how many numbers every child gets
    unsigned long rem = n % n_proc; // how many numbers remain
    unsigned long start = 0;

    for (int i = 0; i < n_proc; i++) {
        counts[i] = div + ((unsigned long)i < rem); //if there is a remainder , give one for each child from the beginning  
        starts[i] = start;
        start += counts[i];
    }
    // reads the file to turn the starts[i] into offsets[i] ( from index ---> byte position)
    if (find_offsets(filename, starts, offsets, n_proc) == -1) return -1;



    // how much memory ---> number of processes * number of bytes per slot 
    size_t length = (size_t)n_proc * sizeof(SharedSlot);
    // creating the shared memory 
    // arguments --> addr = null ( placed anywhere) , allows for read and write 
    // MAP_SHARED means the parent and children see the same memory 
    // returns a pointer to the start of the block
    SharedSlot* shared = mmap(NULL, length, PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);


    // checking if it failed 
    if (shared == MAP_FAILED) return -1;

    // imitalize the memory before fork
    for (int i = 0; i < n_proc; i++) {
        shared[i].result = 0;
        shared[i].done = 0;
    }

    // forking the children 
    int n_forked = 0; // number of children created 
    int failed = 0; // flag for an error 


    for (int i = 0; i < n_proc; i++) {
        if (counts[i] == 0) continue; // a child with no numbers is never needed
        pid_t pid = fork();

        // if the fork fails --> stop creating children
        if (pid == -1) {
            failed = 1;
            break; // stop forking, but still wait for the children already running
        }

        if (pid == 0) {
            // for every child process i , the compute will open the file , jump to offsets[i] and applies the aggregate function to the numbers
            unsigned long result = compute(filename, offsets[i], counts[i], f);

            if (result == (unsigned long)-1 && errno != 0) _exit(1);

            shared[i].result = result; // writes the result in its own slot 
            shared[i].done = 1; // mark it as done 
            _exit(0); // ending the child 
        }
        n_forked++; // count the child 
    }

    // waiting for all the children before reading the slots 
    // if the wait fails or if the child didnt exist normally (WIFEXITED) or compute itself failed --> srt failed = 1
    for (int i = 0; i < n_forked; i++) {
        int status;
        if (wait(&status) == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) 
            failed = 1;
    }

    // Combine the partial results in order 
    unsigned long final_result = 0;
    int has_result = 0;
    // if it did not fail
    if (!failed) {
        // loop over the number of children
        for (int i = 0; i < n_proc; i++) {
            if (counts[i] == 0) continue; // skipping the children that dont have numbers

            // check if any slot isnt marked ---> break
            if (!shared[i].done) { 
                failed = 1; 
                break; 
            }
            //the first result is taken as is and then the rest are combined with the previous
            final_result = has_result ? f(final_result, shared[i].result) : shared[i].result;
            has_result = 1;
        }
    }

    munmap(shared, length); // release the shared region
    if (failed) {
        errno = EINVAL;
        return -1;
    }
    // worked :)
    errno = 0;
    return final_result;
}

#endif // MMAP_COMPUTE_H
