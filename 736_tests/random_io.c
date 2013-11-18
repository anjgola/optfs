//#define DEVICE      "/dev/sdc"
#define BLOCK_SIZE  4096
#define SEC_TO_NSEC 1000000000
#define RANGE       156301488
#define REPEATS     READ_SIZE        //            121683968
#define TEST_ITER   10
#include <stdio.h>          // printf
#include <stdlib.h>         // malloc, rand
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // clock

int main (int argc, char *argv[])
{
    unsigned int    device, i, j, x, y, jump, seek, write_cnt;
    char            *buf;
    struct timespec start, end;
    long            local_nsec, acc_nsec,current_local_nsec;

    // Open device
    if ((device = open(argv[1], O_RDWR)) == -1) {
        printf("Could not open device, exiting\n");
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

    // Start workload
    for (test_iter = 0; test_iter < TEST_ITER; test_iter++) {
        write_cnt = 0;current_local_nsec = 0; local_nsec=0;      
        while(write_cnt < READ_SIZE) {
            seek = rand() % RANGE;
            seek *=BLOCK_SIZE;
            if(lseek(device, seek, SEEK_SET)==-1) {
                printf("Seek failed for write_cnt = %d, seek = %d \n",write_cnt,seek);
                break;
            }// Seek to next BLOCK_SIZEd block.

            clock_gettime(CLOCK_MONOTONIC, &start);
            if(write(device, buf, BLOCK_SIZE)==-1) { // Write BLOCK_SIZE bytes
                printf("Write Failed. write_cnt = %d, seek = %d \n",write_cnt,seek);
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            current_local_nsec = (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
            local_nsec +=current_local_nsec;
            //printf("write_cnt = %5d, Time taken = %10ld us\n", write_cnt, current_local_nsec/1000);
//            printf("%10ld\n",current_local_nsec/1000);
            write_cnt++; 
        }

        printf("test iter = %d time = %10ld\n",test_iter,local_nsec/1000);
    } 
    printf("write_cnt = %5d,Total Time taken = %10ld us\n", write_cnt, local_nsec/1000);

    close(device);
    return;
}

