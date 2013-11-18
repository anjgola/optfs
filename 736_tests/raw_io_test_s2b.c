#define BLOCK_SIZE  4096
#define REPEATS     100
#define SEC_TO_NSEC 1000000000
#define Y_LIM       256
#define X_LIM       256

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // clock

int main (int argc, char *argv[])
{
    unsigned int    device, i, j, x, y, jump, seek, write_cnt;
    char            *buf;
    struct timespec start, end;
    long            local_nsec, acc_nsec;

    if (argc <2) {
        printf("Usage : <a.out> \"device/\"\n");
        exit(1);
    
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
    for (jump = X_LIM; jump > 1; jump = jump >> 1) { // jump

        lseek(device, 0, SEEK_SET); // Reset the seek, we are starting new write cycle.
        local_nsec = 0;
        write_cnt = 0;
        x=0;
        while (1) { // x
            for (y = 0; y < Y_LIM; y++) { // Y, increments everytime

                //printf("jump = %d, x = %d, X_LIM = %d, y = %d, Y_LIM = %d\n", jump, x, X_LIM, y, Y_LIM);
                for (seek = (y * X_LIM) + x; seek < ((y * X_LIM) + x + (X_LIM / jump)); seek++) { // seek
                   // printf("jump = %d, x = %d, X_LIM = %d, y = %d, Y_LIM = %d, seek = %d\n", jump, x, X_LIM, y, Y_LIM, seek);

                    lseek(device, seek * BLOCK_SIZE, SEEK_CUR); // Seek to next BLOCK_SIZEd block
                    clock_gettime(CLOCK_MONOTONIC, &start);
                    write(device, buf, BLOCK_SIZE); // Write BLOCK_SIZE bytes
                    clock_gettime(CLOCK_MONOTONIC, &end);
                    local_nsec += (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
                    write_cnt++;
                } // seek
            
                    printf("jump = %d, x = %d, X_LIM = %d, y = %d, Y_LIM = %d, seek = %d\n", jump, x, X_LIM, y, Y_LIM, seek);
            } //y
                
            x = x + (X_LIM / jump);
            if (x >= X_LIM) {
                break;
            }
        } // while(1)
        
        printf("Jump = %4d , write_cnt = %5d, Time taken = %10ld us\n", jump, write_cnt, local_nsec/1000);
    } // jump

    close(device);
    return;
}

