#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORYBUF_SIZE (1 << 12) /* 4 KB */
#define NULL_INDEX (HISTORYBUF_SIZE + 1)

/* bit1 : literal   bit0 : offset : length 
 * offset: 12 bits  pattern length: 4 bits
 */

#define cyclic_inc(var, limit) \
        ((var + 1) < limit) ? (var += 1) : (var = 0)
#define wrap(value, limit) \
        ( (value < limit) ? value : value - limit)


/*  +--------------+
    |              | --> linked_list head
    +--------------+
     0 1 2 ... 256 + 1   --> index pos. act as 8-bit values

    for pattern matching in history buffer, first byte of pattern to match
    act as key index in list head, it stores index of history buffer which
    contains data beginning with that byte-value. if pattern is not matched 
    from value at that, it then
    goes to next node (node which stores pos) in next_link list and repeats
    the same.

    +==============+
    |              | --> next_link
    +==============+
    0 1 2 ... history buffer size

    if the byte pattern doesn't match at linked_list head, then it goes at
    the value stored in list[byte_start_to_find] as next index in next_link
    list. next_link stores next index to history buffer, where
    history_buf[next_index] may contain data starting with byte_start_to_find, 
    and also if the data is not found, this next_index also acts as pointer
    to next index in next_link.
 */

/* heads of linked list */
unsigned int lists [256 + 1];

unsigned int next_link [HISTORYBUF_SIZE];

unsigned char history_buf [HISTORYBUF_SIZE];
unsigned char lookahead_buf [15];

FILE *file_read = NULL, *file_write = NULL;

/* bitio */
unsigned int bit_buffer = 0, bit_mask = 128; /* 128 = 10000000 */

void
put_bit1 (void)
{
    bit_buffer |= bit_mask;

    if ((bit_mask >>= 1) == 0)
    {
        if (fputc(bit_buffer, file_write) == EOF) exit(1);

        bit_buffer = 0;
        bit_mask = 128;
    }
}

void
put_bit0 (void)
{
    if ((bit_mask >>= 1) == 0)
    {
        if (fputc(bit_buffer, file_write) == EOF) exit(1);

        bit_buffer = 0; bit_mask = 128;
    }
}

void
output_literal (unsigned int c)
{
    /* printf("Encoding literal: 1 %#x\n", c); */
    int mask = 256; /* 1 00000000 */

    put_bit1();

    while (mask >>= 1)
    {
        /* testing every bit if set or unset */
        if (c & mask) put_bit1();
        else put_bit0();
    }
}

void
output_offslen (unsigned int offs, unsigned int len)
{
    /* printf("%s\n", history_buf);
    printf("Encoding offset: 0 %#x,%#x\n", offs, len); */
    int mask = 1 << 12;

    put_bit0();

    while (mask >>= 1)
    {
        if (offs & mask) put_bit1();
        else put_bit0();
    }

    mask = 1 << 4;

    while (mask >>= 1)
    {
        if (len & mask) put_bit1();
        else put_bit0();
    }
}

/* This function searches through the history buffer for the 
 * longest sequence matching the 15 (0x0f) len long string stored
 * in lookahead buffer.
 */
unsigned int
find_match (unsigned int *match_offset, unsigned int len, unsigned int index,
            unsigned int lookaheadbuf_head)
{
    unsigned int lists_index_value = lists[index];
    unsigned int match_len = 0, best_matchlen = 0;
    *match_offset = 0;

    while (lists_index_value != NULL_INDEX)
    {
            /* we matched one symbol */
            match_len = 1;

            while (history_buf[wrap(lists_index_value + match_len, 
                                    HISTORYBUF_SIZE)] == 
            lookahead_buf[wrap(lookaheadbuf_head + match_len, 15)])
            {
                if (match_len == len) break;
                
                match_len++;
            }

            if (match_len > best_matchlen)
            {
                best_matchlen = match_len;
                
                /* lists key --> value (lists_index_value) pointing to start 
                 * of current match in history buffer has the longest match 
                 */
                *match_offset = lists_index_value;
            }

            /* breaks the loop if pattern upto len length is matched
             * no need to try next index in list.
             */
            if (match_len == len)
                break;

        /* try next in list till we exhaust all in list */
        lists_index_value = next_link[lists_index_value];
    }

    return best_matchlen;
}

void
add_char (const unsigned int historybuf_head)
{
    /* this index shall be empty because it is being added 
       Think of it like this:

       You have a chain of people holding hands
       Before you join the chain, you make sure your free hand is clearly marked as "empty" (NULL_INDEX)
       Then the person at the end of the chain takes your other hand to add you to the chain
    */
    next_link[historybuf_head] = NULL_INDEX;

    if (lists[history_buf[historybuf_head]] == NULL_INDEX)
    {
        /* list head is empty */
        lists[history_buf[historybuf_head]] = historybuf_head;
        return;
    }

    /* add character at the end of list link */
    unsigned int i = lists[history_buf[historybuf_head]];

    while (next_link[i] != NULL_INDEX)
    {
        i = next_link[i];
    }

    /* next_link [i] points to historybuf_head where new character is added.
       also now since next_link[i] = historybuf_head, next node in linked list
       is historybuf_head, it is marked as empty by next_link[historybuf_head] = NULL_INDEX;
    */
    next_link[i] = historybuf_head;
}

void 
remove_char (const unsigned int historybuf_head)
{
    unsigned int next_index = next_link[historybuf_head];
    next_link[historybuf_head] = NULL_INDEX;

    if (lists[history_buf[historybuf_head]] == historybuf_head)
    {
        /* we are deleting list head */
        lists[history_buf[historybuf_head]] = next_index;
        return;
    }

    /* list head doesn't point to historybuf_head.
     * so we traverse through link_list till we find the one which points to
     * ours, replace that link with next_index pointed by it.
     */
    unsigned int i = lists[history_buf[historybuf_head]];

    if (i == NULL_INDEX)
        return;

    /* after whole list is filled with index to history buffer,
     * some next link associated with key must be pointing to 
     * historybuf_head (current character at current index which was
     * added to list links with add_char( ) function.
     */
    while (next_link[i] != historybuf_head)
    {
        i = next_link[i];
    }

    /* point it to next link after it */
    next_link[i] = next_index;  
}

/* this func replaces the char stored in history buffer with
 * the one specified by replacement.
 */
int
replace_char (const unsigned int historybuf_head, const unsigned int
                                                  replacement)
{
    remove_char(historybuf_head); 
    history_buf[historybuf_head] = replacement;
    add_char(historybuf_head);

    return 0;
}

int 
lz_compress (FILE *file_read, FILE *file_write)
{
    for (int i = 0; i < (HISTORYBUF_SIZE - 1); i++)
    {
        next_link[i] = i + 1;
    }

    next_link[HISTORYBUF_SIZE - 1] = NULL_INDEX;

    /* initialize the lists head */
    for (int i = 0; i < 256 + 1; i++)
    {
        lists[i] = NULL_INDEX;
    }

    /* head of history buffer and lookahead buffer */
    unsigned int historybuf_head = 0;
    unsigned int lookaheadbuf_head = 0;
    
    /* Initialize the history buffer to null byte, initialize
     * it to same value as specified by compressor.
     */
    memset(history_buf, 0, HISTORYBUF_SIZE * sizeof(unsigned char));
    
    lists[history_buf[0]] = 0;

    unsigned int len = 0; /* length of string */ 
    int c;

    for (len = 0; len < 15 && (c = getc(file_read)) != EOF; len++)
    {
        lookahead_buf[len] = c;
    }

    if (len < 3)
    {
        /* there is not enough symbol to compress */
        return 0;
    }

    unsigned int h = 0, match_len = 0, match_offset = 0;

    while (len > 0)
    {
        if (len < 3)
            /* there are not enough symbols to match */
            match_len = 0;
        else
        {
            h = lookahead_buf[lookaheadbuf_head];
            match_len = find_match(&match_offset, len, h, lookaheadbuf_head);
        }

        /* If match lengh <= 2 */
        if (match_len <= 2)
        {       
            /* write flag and literal */
            output_literal(lookahead_buf[lookaheadbuf_head]); 

            match_len = 1;
        }
        else 
        {
            /* match length > 2, encode as flag, offset and length */
            output_offslen(match_offset, match_len); 
        }


        /* replace match length worth of bytes we've matched in the
         * history buffer with new bytes from the input file
         */

        unsigned int j = 0;

        while ((j < match_len) && ((c = getc(file_read)) != EOF))
        {
            /* move old byte into historybuffer and new into lookahead
             * buffer
             */
            replace_char(historybuf_head, lookahead_buf[lookaheadbuf_head]); 
            lookahead_buf[lookaheadbuf_head] = c; 

            cyclic_inc(lookaheadbuf_head, 15);
            cyclic_inc(historybuf_head, HISTORYBUF_SIZE);
            j++;
        }

        /* handle the case where we hit EOF before filling lookahead */
        while (j < match_len)
        {
            /* move rest of matched data to history buffer that was halted
             * in above function due to EOF.
             */
            replace_char(historybuf_head, lookahead_buf[lookaheadbuf_head]);
 
            cyclic_inc(lookaheadbuf_head, 15);
            cyclic_inc(historybuf_head, HISTORYBUF_SIZE);
            len--;
            j++;
        }
    } 

    /* write out remaining bits less than 8 in the file */
    fputc(bit_buffer, file_write);

    return 0;
} 

int
main (int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("usage: lzss infile output\n");
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

    lz_compress(file_read, file_write);
    fclose(file_read); fclose(file_write);

    return 0;

}
