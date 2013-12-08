#include <fletcher.h>

uint32_t fletcher32( uint32_t const crc32_sum, uint16_t const *data, size_t len )
{
//    uint32_t sum1 = 0xffff, sum2 = 0xffff;
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
    /* Second reduction step to reduce sums to 16 bits */
    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    return sum2 << 16 | sum1;
}
