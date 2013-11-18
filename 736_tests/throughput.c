/* Compile Using
 * gcc throughput.c -pthread
 */

#define DEVICE      "/dev/sdc"
#define BLOCK_SIZE  4096
#define SLEEP_TIME  100
#define READ        0
#define WRITE       1

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc, rand
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <pthread.h>        // pthread

void thread_start (void);
unsigned long write_cnt, read_cnt;

int main (void)
{

    unsigned long   op_accm;
    pthread_t       thread;

    //Create pthreads
    if(pthread_create(&thread, NULL, &thread_start, NULL) != 0) {
        printf("Could not create thread\n");
        exit (1);
    }
   
    sleep(SLEEP_TIME);

    if(pthread_cancel(thread) != 0) {
        printf("Could not cancel thread\n");
    }

#if READ
    printf("Read throughput = %lu\n", read_cnt / SLEEP_TIME);
#endif
#if WRITE
    printf("Write throughput = %lu\n", write_cnt / SLEEP_TIME);
#endif
    return(0);
}

void thread_start (void) {

    //pthread stuff
    printf("Pthread enter\n");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    unsigned int    device, i;
    char            *buf;

    // Open device
    if ((device = open(DEVICE, O_RDWR)) == -1) {
        fprintf(0, "Could not open device, exiting\n");
        exit(1);
    }   
    
    // Allocate buffer
    if ((buf = (char *)malloc(BLOCK_SIZE)) == NULL) {
        printf("Could not allocate buffer, exiting\n");
        exit(1);
    }   
    
    // Fill buffer 
    for (i = 0; i < BLOCK_SIZE; i++) {
        *(buf + i) = (char) (i & 0xFF);
    }   

    if (lseek(device, 0, SEEK_SET) == -1) {
        printf("First seek failed\n");
    }
    
    // Start workload
    while (1) {

#if WRITE
        if(write(device, buf, BLOCK_SIZE) == -1) { // Write BLOCK_SIZE bytes
            printf("Write Failed. write_cnt = %lu\n", write_cnt);
        }
        write_cnt++;
#endif
#if READ
        if(read(device, buf, BLOCK_SIZE) == -1) { // Write BLOCK_SIZE bytes
            printf("Read Failed. write_cnt = %lu\n", write_cnt);
        }
        read_cnt++;
#endif
        if(lseek(device, BLOCK_SIZE, SEEK_CUR)==-1) {
            printf("Seek failed\n");
        }// Seek to next BLOCK_SIZEd block.
    }

    close(device);
    return;
}

