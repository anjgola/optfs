#define READ    1
#define WRITE   0

#define SEQ     0
#define RANDOM  1


#define BLOCK_SIZE  1
#define REPEATS     10
#define RANGE       (1024 * 1024)
#define SLEEP       35
#define SEC_TO_NSEC 1000000000

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // clock

int main (int argc, char *argv[])
{
    unsigned int    device, i, j, op_cnt;
    char            *buf;
    struct timespec start, end;
    long            local_nsec;

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
    if ((buf = (char *)malloc(BLOCK_SIZE * RANGE)) == NULL) {
        printf("Could not allocate buffer, exiting\n");
        exit(1);
    }

    printf("Base block size = %d\n", BLOCK_SIZE);

    // Start workload
    for (i = 1; i < RANGE; i = (i << 1)) {

        srand (time(NULL));

        if ((lseek(device, 0, SEEK_SET)) == -1) {
            printf("Seek failed, exiting\n");
            exit (1);
        }
        read(device, buf, 1); // Warm-up

        for (j = 0, local_nsec = 0, op_cnt = 0; j < (RANGE / i); j++) {
            clock_gettime(CLOCK_MONOTONIC, &start);
#if READ
            read(device, buf, i * BLOCK_SIZE);
#endif
#if WRITE
            write(device, buf, i * BLOCK_SIZE);
#endif
            clock_gettime(CLOCK_MONOTONIC, &end);
            local_nsec += (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
            op_cnt++;
#if SEQ
            if ((lseek(device, i * BLOCK_SIZE, SEEK_CUR)) == -1) {
#endif
#if RANDOM
            if ((lseek(device, rand() % (RANGE * BLOCK_SIZE), SEEK_SET)) == -1) {
#endif
                printf("Seek failed, exiting\n");
                exit (1);
            }
        }

        printf("Block Multiplication = %5d\tOps = %5u\tTime Taken(ns)= %10ld\n", i, op_cnt, local_nsec);
#if WRITE
        sleep(SLEEP);
#endif
    }

    close(device);
    return;
}

