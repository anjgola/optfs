#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t fletcher32(uint32_t const crc32_sum, uint16_t const *data, size_t len);

int main ()
{
    char block1[4096], block2[4096], generator[16][512];
    unsigned int i,j,k;
    uint32_t checksum1, checksum2;
    unsigned long cnt = 0;

    srand(time(NULL));
    while (1) {
        for (i = 0; i < 16; i++) {
            for (k = 0; k < 512; k += sizeof(int)) {
                *((int *)(generator[i] + k)) = rand();
            }
        }

        for (i = 0; i < 8; i++) {
            memcpy((void *)(block1 + (512 * i)), (void *)generator[rand() % 16], 512);
            memcpy((void *)(block2 + (512 * i)), (void *)generator[rand() % 16], 512);
        }

        checksum1 = fletcher32(~0, (uint16_t *) block1, 4096);
        checksum2 = fletcher32(~0, (uint16_t *) block2, 4096);

        if (checksum1 == checksum2) {
            if (memcmp(block1, block2, 4096) != 0) {
                printf("False positive after %lu computations\n", cnt);
                exit (1);
            }
        }

        cnt++;
    }
}

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
