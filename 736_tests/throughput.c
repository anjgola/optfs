/* Compile Using
 * gcc throughput.c -pthread
 */

#define DEVICE      "/mnt/mydisk/test.img"
#define BLOCK_SIZE  4096
#define SLEEP_TIME  1
#define BLOCK_RANGE (256 * 1024)    // 10GB file

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc, rand
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <pthread.h>        // pthread
#include <stdbool.h>        // bool
#define SEQ_R       1
#define RANDOM_R    2
#define SEQ_W       3
#define RANDOM_W    4

void thread_start (int);
unsigned long   op_cnt;
char            buf[BLOCK_SIZE + 1];
bool            thr_terminate;

int main (void)
{
    unsigned int    i, device;
    unsigned long   op_accm;
    pthread_t       thread;
    void            *join;

    printf("Using path %s\n", DEVICE);

    for (i = SEQ_R; i <= RANDOM_R; i++) {
        if ((device = open(DEVICE, O_RDWR)) == -1) {
            printf(0, "Could not open device, exiting\n");
            exit(1);
        }
        
        sync(device);
        close(device);
        op_cnt = 0;
        thr_terminate = false;

        if(pthread_create(&thread, NULL, &thread_start, i) != 0) {
            printf("Could not create thread\n");
            exit (1);
        }

        sleep(SLEEP_TIME);
        thr_terminate = true;
#if 0
        if(pthread_cancel(thread) != 0) {
            printf("Could not cancel thread\n");
        }
#endif
        pthread_join(thread, &join);

        switch(i) {
            case SEQ_R:
            printf("Sequential Read throughput = %lu\n", op_cnt / SLEEP_TIME);
            break;

            case RANDOM_R:
            printf("Random Read throughput = %lu\n", op_cnt / SLEEP_TIME);
            break;

            case SEQ_W:
            printf("Sequential Write throughput = %lu\n", op_cnt / SLEEP_TIME);
            break;

            case RANDOM_W:
            printf("Random Write throughput = %lu\n", op_cnt / SLEEP_TIME);
            break;

            default:
            printf("Unknown operation. Should probably panic.\n");
        }
    }
   
    return(0);
}

void thread_start (int mode) {

    //pthread stuff
    printf("Pthread enter\n");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    unsigned int    device, i, ret;

    // Open device
    if ((device = open(DEVICE, O_RDWR)) == -1) {
        fprintf(0, "Could not open device, exiting\n");
        exit(1);
    }   

    // Start workload
    while (1) {

        if (thr_terminate) {
            break;
        }

        if ((mode == SEQ_W) || (mode == RANDOM_W)) {
            if(write(device, buf, BLOCK_SIZE) == -1) { // Write BLOCK_SIZE bytes
                printf("Write Failed. op_cnt = %lu\n", op_cnt);
                exit(1);
            }
        } else {
            if(read(device, buf, BLOCK_SIZE) == -1) { // Write BLOCK_SIZE bytes
                printf("Read Failed. op_cnt = %lu\n", op_cnt);
                exit(1);
            }
        }
        
        op_cnt++;

        if ((mode == SEQ_W) || (mode == SEQ_R)) {
            if(lseek(device, BLOCK_SIZE, SEEK_CUR)==-1) {
                printf("Seek failed\n");
                exit(1);
            }// Seek to next BLOCK_SIZEd block.
        } else {
            if(lseek(device, (rand() % BLOCK_RANGE) * BLOCK_SIZE, SEEK_SET)==-1) {
                printf("Seek failed\n");
            }
        }
    }

    close(device);
    pthread_exit(&ret);
}

