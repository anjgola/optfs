#define BLOCK_SIZE  4096
#define REPEATS     10
#define RANGE       156301488
#define SLEEP       30
#define SEC_TO_NSEC 1000000000

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // clock

int main (int argc, char *argv[])
{
    unsigned int    device, i, write_cnt, seek;
    char            *buf;
    struct timespec start, end;
    long            local_nsec, acc_nsec, current_local_nsec;

    srand(time(NULL));

    if (argc < 2) {
        printf("Usage <a.out> \"device/file path\"\n");
        exit (1);
    }

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
    for (i = 0, write_cnt = 0, local_nsec = 0; i < REPEATS; i++) { // jump

        seek = rand() % RANGE;
        seek *= BLOCK_SIZE;
        if(lseek(device, seek, SEEK_SET)==-1) {
            printf("Seek Failed. Seek = %d \n",seek);
            exit(1);
        } // Seek to next BLOCK_SIZEd block.
        clock_gettime(CLOCK_MONOTONIC, &start);
        if(write(device, buf, BLOCK_SIZE)==-1){
            printf("Write Failed. Seek = %d \n",seek);
            exit(1);
        }// Write BLOCK_SIZE bytes
        clock_gettime(CLOCK_MONOTONIC, &end);
        current_local_nsec = (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
        local_nsec += current_local_nsec;
        write_cnt++;
        printf("seek = %d, current_local_nsec = %10ld us\n", seek, current_local_nsec/1000);
        sleep(SLEEP);
    } 
    
    printf("write_cnt = %5d, Time taken = %10ld us\n", write_cnt, local_nsec/1000);

    close(device);
    return;
}

