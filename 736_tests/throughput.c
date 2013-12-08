
#define BLOCK_SIZE  4096
#define TIMER       600
#define BLOCK_RANGE (1024 * 1024 * 10)

#include <stdio.h>          // printf
#include <stdlib.h>         // malloc, rand
#include <sys/types.h>      // block IO
#include <sys/stat.h>       // block IO
#include <fcntl.h>          // block IO
#include <time.h>           // time
#include <stdbool.h>        // bool
#define SEQ_R       1
#define RANDOM_R    2
#define SEQ_W       3
#define RANDOM_W    4

unsigned long   op_cnt;
char            buf[BLOCK_SIZE + 1];

int main (int argc, char *argv[])
{
    unsigned int    mode, device, seek;
    unsigned long   op_accm;
    pthread_t       thread;
    void            *join;
    time_t          timer;

    if (argc < 2) {
        printf("Usage <a.out> \"device/file path\"\n");
        exit (1);
    }
    srand(time(NULL));

    for (mode = RANDOM_R; mode <= RANDOM_R; mode++) {
        if ((device = open(argv[1], O_RDWR, O_SYNC)) == -1) {
            printf("Could not open device, exiting\n");
            exit(1);
        }
        
        if (system("hdparm -F /dev/sdc") == -1) {
            printf("hdparm -F /dev/sdc failed\n");
        }
         
        if (system("echo 3 > /proc/sys/vm/drop_caches") == -1) {
            printf("drop_caches failed\n");
        }

        op_cnt = 0;
        seek = 0;
        timer = time(NULL);

        while (timer + TIMER > time(NULL)) {
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
                seek += 1;
                if (seek >= BLOCK_RANGE) {
                    seek = 0;
                }
            } else {
                 seek = rand() % BLOCK_RANGE;
            }

            if(lseek(device, seek * BLOCK_SIZE, SEEK_SET) == -1) {
                printf("Seek failed\n");
                exit(1);
            }
        }
        
        switch(mode) {
            case SEQ_R:
            printf("Sequential Read throughput (kbps) = %lu\n", (op_cnt * BLOCK_SIZE) / (1024 * TIMER));
            break;

            case RANDOM_R:
            printf("Random Read throughput (kbps) = %lu\n", (op_cnt * BLOCK_SIZE) / (1024 *  TIMER));
            break;

            case SEQ_W:
            printf("Sequential Write throughput (kbps) = %lu\n", (op_cnt * BLOCK_SIZE) / (1024 * TIMER));
            break;

            case RANDOM_W:
            printf("Random Write throughput (kbps) = %lu\n", (op_cnt * BLOCK_SIZE) / (1024 * TIMER));
            break;

            default:
            printf("Unknown operation. Should probably panic.\n");
        }

        sync(device);
        close(device);
    }
   
    return(0);
}

