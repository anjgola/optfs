#define READ    1
#define WRITE   0

#define SEQ     0
#define RANDOM  1

#define DEVICE  1

#define BLOCK_START 64
#define BLOCK_END   (4096 * 128)
#define FILE_RANGE  (1024 * 1024 * 10) // File size in bytes
#define SLEEP       35
#define SEC_TO_NSEC 1000000000
#define RES_FILE    "./results.txt"
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
    FILE            *res_file;

    if (argc < 2) {
        printf("Usage <a.out> \"device/file path\"\n");
        exit (1);
    }

    if (unlink (RES_FILE) == -1) {
        printf("remove result file failed, hopefully did not exist\n");
    }

    if ((res_file = fopen(RES_FILE, "w")) == NULL) {
        printf("Could not create result file, exiting\n");
        exit(1);
    }

    srand (time(NULL));

    if ((buf = (char *)malloc(BLOCK_END)) == NULL) {
        printf("Could not allocate buffer, exiting\n");
        exit(1);
    }

#if DEVICE // check if using the DEVICE flag with a file system test. Results can be very bad !!!!
    if ((strcmp(argv[1], "/dev/sdc") != 0) && (strcmp(argv[1], "/dev/sdb") != 0)) {
        printf("DEVICE is 1, while Input is not a block device, update code or use correct path, exiting\n");
        exit(1);
    }
#endif

    printf("Read = %d, Write = %d, Seq = %d, Random = %d, File Range = %d\n", READ, WRITE, SEQ, RANDOM, FILE_RANGE);

    // Start workload
    for (i = BLOCK_START; i <= BLOCK_END; i = (i << 1)) {

        if (system("hdparm -F /dev/sdc") == -1) {
            printf("hdparm -F /dev/sdc failed\n");
        }

        if (system("echo 3 > /proc/sys/vm/drop_caches") == -1) {
            printf("drop_caches failed\n");
        }

#if WRITE
#if DEVICE
         if ((system("umount /mnt/mydisk") == -1) || (system("mkfs.ext4 -F /dev/sdc") == -1) || (system("mount -t ext4 /dev/sdc /mnt/mydisk") == -1)) {
             printf("Could not make ext4 for trimming, exiting\n");
             exit(1);
         }
#endif // DEVICE
        // For write tests start with a neat trimmed SSD
        if (system("fstrim /mnt/mydisk") == -1) {
            printf("fstrim failed\n");
        }

        if (system("/optfs/736_tests/wiper-3.5/wiper.sh --commit /mnt/mydisk/") == -1) {
            printf("wiper failed\n");
        }

        // Sleep a bit after trimming. Not really sure why
        sleep(SLEEP);
#endif // READ

#if DEVICE // Well just open and read write, no concern for files.
        if ((device = open(argv[1], O_RDWR)) == -1) {
            printf("Could not open block device, exiting\n");
            exit(1);
        }
#else // If using a file, create a new file.
        if (unlink (argv[1]) == -1) {
            printf("remove file failed, hopefully did not exist\n");
        }

#if WRITE        
        if ((device = open(argv[1], O_CREATE | O_EXCL | O_RDWR, O_SYNC | S_IRWXU | S_IRWXG | S_IRWXO)) == -1) {

            printf("Could not create new file for writing, exiting\n");
            exit(1);
        }
#else
        if ((device = open(argv[1], O_CREATE | O_RDWR, O_SYNC | S_IRWXU | S_IRWXG | S_IRWXO)) == -1) {
            printf("Could not open file for read test, exiting\n");
            exit(1);
        }
#endif // else of WRITE

#if READ // File needs to be created before the test. Use dd.
        sprintf(buf, "dd if=/dev/zero of=%s bs=4096 count=%d\0", argv[1], FILE_RANGE / 4096);
        if (system(buf) == -1) {
            printf("Could not populate the file for read test");
        }
#endif // READ
#endif // else of DEVICE

        if ((lseek(device, 0, SEEK_SET)) == -1) {
            printf("Seek failed, exiting\n");
            exit (1);
        }
        read(device, buf, 1); // Warm-up

        for (j = 0, local_nsec = 0, op_cnt = 0; j < (FILE_RANGE / i); j++) {
            clock_gettime(CLOCK_MONOTONIC, &start);
#if READ
            read(device, buf, i);
#endif
#if WRITE
            write(device, buf, i);
#endif
            clock_gettime(CLOCK_MONOTONIC, &end);
            local_nsec += (end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * SEC_TO_NSEC);
            op_cnt++;
#if SEQ
            if ((lseek(device, i, SEEK_CUR)) == -1) {
#endif
#if RANDOM
            if ((lseek(device, rand() % (FILE_RANGE / i), SEEK_SET)) == -1) {
#endif
                printf("Seek failed, exiting\n");
                exit (1);
            }
        }

        close(device);
        printf("Block = %5d\t,Ops = %5u\t,Time Taken(ns)= %15ld\n", i, op_cnt, local_nsec);
        fprintf(res_file, "Block = %5d\t,Ops = %5u\t,Time Taken(ns)= %15ld\n", i, op_cnt, local_nsec);
    }

    fclose(res_file);
    return;
}

