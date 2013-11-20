/* Compile Using
 * gcc throughput.c -pthread
 */

#define DEVICE      "/mnt/mydisk/test.img"
#define BLOCK_SIZE  4096
#define TIMER  1
#define BLOCK_RANGE (256 * 1024)    // 10GB file

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

void thread_start (int);
unsigned long   op_cnt;
char            buf[BLOCK_SIZE + 1];
bool            thr_terminate;

int main (void)
{
    unsigned int    mode, device;
    unsigned long   op_accm;
    pthread_t       thread;
    void            *join;
    time_t          timer;

    printf("Using path %s\n", DEVICE);

    for (mode = SEQ_R; mode <= RANDOM_R; mode++) {
        if ((device = open(DEVICE, O_RDWR)) == -1) {
            printf("Could not open device, exiting\n");
            exit(1);
        }
        
        op_cnt = 0;
        thr_terminate = false;
        timer = time(NULL);

        while (timer + TIMER < time(NULL)) {
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
                }
            } else {
                if(lseek(device, (rand() % BLOCK_RANGE) * BLOCK_SIZE, SEEK_SET)==-1) {
                    printf("Seek failed\n");
                    exit(1);
                }
            }
        }
        
        switch(mode) {
            case SEQ_R:
            printf("Sequential Read throughput = %lu\n", op_cnt / TIMER);
            break;

            case RANDOM_R:
            printf("Random Read throughput = %lu\n", op_cnt / TIMER);
            break;

            case SEQ_W:
            printf("Sequential Write throughput = %lu\n", op_cnt / TIMER);
            break;

            case RANDOM_W:
            printf("Random Write throughput = %lu\n", op_cnt / TIMER);
            break;

            default:
            printf("Unknown operation. Should probably panic.\n");
        }

        sync(device);
        close(device);
    }
   
    return(0);
}

