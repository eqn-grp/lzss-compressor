#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORYBUF_SIZE (1 << 12) /* 4 KB */

FILE *file_read = NULL, *file_write = NULL;

unsigned char history_buf [HISTORYBUF_SIZE];
unsigned int historybuf_head = 0;

#define cyclic_inc(var, limit) \
        ((var + 1) < limit) ? (var += 1) : (var = 0)

#define wrap(value, limit) \
        (value < limit) ? value : (value - limit)

/* get n bits from input file, but n <= sizeof(int) */
int mask = 0, in_byte = 0;

int
get_bit (unsigned int n)
{
    int bit_buffer = 0; // Local buffer to accumulate bits
    int bits_read = 0;  // Count of bits actually read

    while (bits_read < n) {
        // Check if mask is exhausted
        if (mask == 0) {
            in_byte = fgetc(file_read); // Read next byte
            if (in_byte == EOF) {
                if (bits_read == 0) return EOF; // Return EOF if no bits read
                break; // Break if we have already read some bits
            }
            mask = 0x80; // Reset mask to the highest bit
        }

        // Read one bit
        bit_buffer = (bit_buffer << 1) | ((in_byte & mask) ? 1 : 0);
        mask >>= 1; // Move mask to the next bit
        bits_read++; // Increment count of bits read
    }

    return bit_buffer;
}

int
lz_decompress (FILE *file_read, FILE *file_write)
{
    memset(history_buf, 0, sizeof(unsigned char) * HISTORYBUF_SIZE);

    unsigned int c = 0;
    int offset = 0, len = 0; /* offset = 12 bit, length = 4 bit */
    unsigned char temp_history_buf [15];

    while ((c = get_bit(1)) != EOF)
    {
        /* printf("%s\n bit_buffer: %x\n", history_buf, c); */

        if (c)
        {
            if ((c = get_bit(8)) == EOF) break;
            /* printf("decoding literal: %c\n", c); */
            fputc(c, file_write);
            history_buf[historybuf_head] = c;
            cyclic_inc(historybuf_head, HISTORYBUF_SIZE);
        }
        else
        {
            if ((offset = get_bit(12)) == EOF) break;
            if ((len = get_bit(4)) == EOF) break;

            /* printf("offset: %#x, len: %#x\n", offset, len); */

            for (unsigned int i = 0; i < len; i++)
            {
                c = history_buf[wrap((offset + i), HISTORYBUF_SIZE)];
                /* printf("decoding literal offset: %u, %c\n", offset, c); */
                fputc(c, file_write);

                /* writing to temporary buffer, because offset:len might be
                 * at historybuf_head - 1 and reading next few bytes which 
                 * is memset to 0, but it will overwrite that with c.
                 */
                temp_history_buf[i] = c;
            }

            for (unsigned int i = 0; i < len; i++)
            {
                history_buf[historybuf_head] = temp_history_buf[i];
                cyclic_inc(historybuf_head, HISTORYBUF_SIZE);
            }
        }
    }
}

int
main (int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("usage: supply infile outfile to decode");
        return 1;
    }

    file_read = fopen(argv[1], "rb");
    file_write = fopen(argv[2], "wb");

    if (file_read == NULL)
    {
        printf("? %s\n", argv[1]); return 1;
    }

    if (file_write == NULL)
    {
        printf("? %s\n", argv[2]); return 1;
    }

    lz_decompress(file_read, file_write);
    fclose(file_read); fclose(file_write);

    return 0;

}
