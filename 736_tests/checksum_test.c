#define FLETCHER    1
#define CRC         0

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if CRC
#include </optfs/include/linux/crc32.h>
#endif

#include <time.h>
#define REPEATS 100
#define u8 uint8_t
#define u32 uint32_t

extern u32  crc32_be(u32 crc, unsigned char const *p, size_t len);

uint32_t fletcher32(uint32_t const crc32_sum, uint16_t const *data, size_t len);

int main ()
{
    char block1[4096], block2[4096], generator[16][512];
    unsigned int i, j, k, repeats = 0;
    uint32_t checksum1, checksum2;
    struct timespec start, end;
    unsigned long fletcher32_time = 0, cnt = 0, false_cnt = 0;

    srand(time(NULL));
    while (1) {
        cnt++;

        for (i = 0; i < 16; i++) {
            for (k = 0; k < 512; k += sizeof(int)) {
                *((int *)(generator[i] + k)) = rand();
            }
        }

        for (i = 0; i < 8; i++) {
            memcpy((void *)(block1 + (512 * i)), (void *)generator[rand() % 16], 512);
            memcpy((void *)(block2 + (512 * i)), (void *)generator[rand() % 16], 512);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
#if FLETCHER
        checksum1 = fletcher32(~0, (uint16_t *) block1, 4096);
#endif
#if CRC
        checksum1 = crc32_be(~0, (uint16_t *) block1, 4096);
#endif
        clock_gettime(CLOCK_MONOTONIC, &end);
        fletcher32_time += ((end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * 1000000000));

        clock_gettime(CLOCK_MONOTONIC, &start);
#if FLETCHER
        checksum2 = fletcher32(~0, (uint16_t *) block2, 4096);
#endif
#if CRC
        checksum2 = crc32_be(~0, (uint16_t *) block2, 4096);
#endif
        clock_gettime(CLOCK_MONOTONIC, &end);
        fletcher32_time += ((end.tv_nsec - start.tv_nsec) + ((end.tv_sec - start.tv_sec) * 1000000000));

        if (checksum1 == checksum2) {
            if (memcmp(block1, block2, 4096) != 0) {
                repeats++;
            }
        }

        if (repeats == REPEATS) {
            printf("False positive after %lu computations, average time     = %lu\n", cnt / REPEATS, fletcher32_time / (2 * cnt * REPEATS));
            exit (0);
        }
    }
} //main ends here

//checksum routines
uint32_t fletcher32(uint32_t const crc32_sum, uint16_t const *data, size_t len)
{
    uint32_t sum1 = crc32_sum & 0xffff, sum2 = (crc32_sum >> 16) & 0xffff ;
    size_t words = len/4;
    while (words) {

        unsigned tlen = words > 359 ? 359 : words;
        words -= tlen;
        do {
            sum2 += sum1 += *data++;
        } while (--tlen);

        sum1 = (sum1 & 0xffff) + (sum1 >> 16);
        sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    }

    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    return sum2 << 16 | sum1;
}

#if 0
uint32_t __pure crc32_be(uint32_t crc, unsigned char const *p, size_t len)
{
    const uint32_t      (*tab)[] = crc32table_be;

    crc = __cpu_to_be32(crc);
    crc = crc32_body(crc, p, len, tab);
    return __be32_to_cpu(crc);
}
#endif
