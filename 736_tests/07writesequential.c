#define BLOCK_SIZE  4096
#define SEC_TO_NSEC 1000000000
#define READ_SIZE 1024*256*1
#define TEST_ITER 30
#define RANGE 156301488 //to prevent exceeding limits
#include <stdio.h>          // printf
#include <stdlib.h>         // malloc
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // clock

int main (int argc, char *argv[])
{
    unsigned int    device, i, j, x, y, jump, seek, write_cnt = 0, test_iter;
    char            *buf;
    struct timespec start, end, start_sync, end_sync;
    long            local_nsec, acc_nsec, current_local_nsec, sync_nsec;
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
    srand(time(NULL));
    local_nsec = 0;
    x=0;
    for(test_iter = 0;test_iter < TEST_ITER; test_iter++)   {
        write_cnt = 0;current_local_nsec = 0; local_nsec=0;
        seek = rand() % RANGE;
        seek *= BLOCK_SIZE;
        if(lseek(device,seek, SEEK_SET) == -1) {
            printf("Seek failed for seek = %d \n",seek);
        }

        while(write_cnt < READ_SIZE)    {
            clock_gettime(CLOCK_MONOTONIC, &start);
            if(write(device, buf, BLOCK_SIZE)== -1) {
                printf("Write failed , exiting\n");
                exit(1);                    
            } // Write BLOCK_SIZE bytes
            clock_gettime(CLOCK_MONOTONIC, &end);
            if(lseek(device,BLOCK_SIZE,SEEK_CUR)==-1){
                printf("Seek Failed for location %d", seek);
                exit(1);
            }      

            current_local_nsec = (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
            local_nsec += current_local_nsec;
            //            printf("write_cnt = %d Time iaken = %10ld us\n", write_cnt,current_local_nsec/1000);
            write_cnt++;
        }
        clock_gettime(CLOCK_MONOTONIC, &start_sync);
        sync();
        clock_gettime(CLOCK_MONOTONIC, &end_sync);
        sync_nsec = (end_sync.tv_nsec - start_sync.tv_nsec) + ((end_sync.tv_sec - start_sync.tv_sec) * SEC_TO_NSEC);
        local_nsec /= READ_SIZE;
        printf("%10ld \t %10ld \n",local_nsec/1000,sync_nsec/1000);
    
    }


    // printf(" write_cnt = %5d, Time taken = %10ld us\n", write_cnt, local_nsec/1000);

    close(device);
    return;
}

