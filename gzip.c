/*
 * A *very* minimal gzip compressor for 2.11BSD
 * Fixed Huffman DEFLATE compression with 8KB window
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* GZIP header constants */
#define GZIP_MAGIC1     0x1f
#define GZIP_MAGIC2     0x8b
#define GZIP_DEFLATE    8

/* Compression parameters - tuned for PDP-11 */
#define WSIZE           4096U   /* Window size - 4KB */
#define HASH_BITS       11      /* Hash table size = 2048 entries */
#define HASH_SIZE       (1U << HASH_BITS)
#define HASH_MASK       (HASH_SIZE - 1)
#define MIN_MATCH       3       /* Minimum match length */
#define MAX_MATCH       258     /* Maximum match length */
#define MIN_LOOKAHEAD   (MAX_MATCH + MIN_MATCH + 1)

/* Compression state */
static unsigned char *window = NULL;    /* Sliding window buffer */
static unsigned short *hash_head = NULL; /* Hash table head pointers */
static unsigned short *prev = NULL;     /* Link to older string with same hash */
static unsigned int wpos = 0;           /* Current position in window */
static unsigned int lookahead = 0;      /* Bytes available at wpos */
static unsigned int match_start = 0;    /* Start of current match */
static unsigned int match_length = 0;   /* Length of current match */

/* Input/output */
static FILE *infile = NULL;
static FILE *outfile = NULL;

/* Bit output buffer */
static unsigned long outbuf = 0;
static int outbits = 0;

/* CRC32 table and value */
static unsigned long crc_table[256];
static unsigned long crc = 0xffffffffL;
static unsigned long input_len = 0;

/*
 * Initialize CRC32 table
 */
static void make_crc_table(void)
{
    unsigned long c;
    int n, k;
    
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
}

/*
 * Update CRC32 with new byte
 */
static void update_crc(unsigned char c)
{
    crc = crc_table[(crc ^ c) & 0xff] ^ (crc >> 8);
}

/*
 * Write bits to output
 */
static void put_bits(int bits, int length)
{
    outbuf |= ((unsigned long)bits << outbits);
    outbits += length;
    
    while (outbits >= 8) {
        putc((int)(outbuf & 0xff), outfile);
        outbuf >>= 8;
        outbits -= 8;
    }
}

/*
 * Reverse bits in a value
 */
static unsigned int reverse_bits(unsigned int value, int bits)
{
    unsigned int result = 0;
    int i;
    
    for (i = 0; i < bits; i++) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

/*
 * Flush remaining bits
 */
static void flush_bits(void)
{
    if (outbits > 0) {
        putc((int)(outbuf & 0xff), outfile);
    }
    outbuf = 0;
    outbits = 0;
}

/*
 * Write gzip header
 */
static void write_header(char *filename)
{
    time_t mtime = time(NULL);
    
    /* Magic number */
    putc(GZIP_MAGIC1, outfile);
    putc(GZIP_MAGIC2, outfile);
    
    /* Compression method */
    putc(GZIP_DEFLATE, outfile);
    
    /* Flags - include original filename */
    putc(0x08, outfile);
    
    /* Modification time */
    putc((int)(mtime & 0xff), outfile);
    putc((int)((mtime >> 8) & 0xff), outfile);
    putc((int)((mtime >> 16) & 0xff), outfile);
    putc((int)((mtime >> 24) & 0xff), outfile);
    
    /* Extra flags (2 = max compression, 4 = fastest) */
    putc(4, outfile);
    
    /* OS (3 = Unix) */
    putc(3, outfile);
    
    /* Original filename */
    while (*filename) {
        putc(*filename++, outfile);
    }
    putc(0, outfile);
}

/*
 * Write gzip trailer
 */
static void write_trailer(void)
{
    unsigned long c = crc ^ 0xffffffffL;
    int i;
    
    /* CRC32 - write byte by byte using put_bits */
    for (i = 0; i < 4; i++) {
        put_bits((int)((c >> (i * 8)) & 0xff), 8);
    }
    
    /* Uncompressed size - write byte by byte using put_bits */
    for (i = 0; i < 4; i++) {
        put_bits((int)((input_len >> (i * 8)) & 0xff), 8);
    }
}

/*
 * Compute hash value for 3-byte sequence
 */
static unsigned int hash_func(unsigned char *p)
{
    return ((((unsigned int)p[0] << 10) ^ 
             ((unsigned int)p[1] << 5) ^ 
             (unsigned int)p[2]) & HASH_MASK);
}

/*
 * Insert string at current position into hash table
 */
static void insert_string(void)
{
    unsigned int hash;
    
    if (lookahead < MIN_MATCH)
        return;
    
    hash = hash_func(&window[wpos]);
    prev[wpos & (WSIZE - 1)] = hash_head[hash];
    hash_head[hash] = wpos;
}

/*
 * Find longest match starting at current position
 */
static int find_match(void)
{
    unsigned int chain_length = 128;  /* Max hash chain to search */
    unsigned int cur_match;
    unsigned int hash;
    int len;
    int best_len = 0;
    unsigned char *scan, *match;
    int limit = (wpos > WSIZE) ? (wpos - WSIZE) : 0;
    
    if (lookahead < MIN_MATCH)
        return 0;
    
    hash = hash_func(&window[wpos]);
    cur_match = hash_head[hash];
    
    /* Search hash chain */
    while (cur_match >= (unsigned int)limit && chain_length-- > 0) {
        /* Skip if matching current position or if match is too recent */
        if (cur_match >= wpos) {
            cur_match = prev[cur_match & (WSIZE - 1)];
            continue;
        }
        
        /* Quick check on first and last bytes */
        if (window[cur_match + best_len] == window[wpos + best_len] &&
            window[cur_match] == window[wpos]) {
            
            /* Check full match */
            scan = &window[wpos];
            match = &window[cur_match];
            len = 0;
            
            while (len < MAX_MATCH && len < lookahead && 
                   scan[len] == match[len]) {
                len++;
            }
            
            if (len > best_len) {
                best_len = len;
                match_start = cur_match;
                
                if (len >= MAX_MATCH)
                    break;
            }
        }
        
        cur_match = prev[cur_match & (WSIZE - 1)];
    }
    
    match_length = best_len;
    return best_len >= MIN_MATCH;
}

/*
 * Fill the lookahead buffer
 */
static void fill_window(void)
{
    int n, more;
    
    do {
        more = (WSIZE * 2) - lookahead - wpos;
        
        if (more == 0 && wpos == 0 && lookahead == 0) {
            more = WSIZE * 2;
        }
        else if (more <= 0 && wpos >= WSIZE) {
            /* Slide window - move second half to first half */
            int i;
            memcpy(window, window + WSIZE, WSIZE);
            
            /* Adjust positions */
            if (match_start >= WSIZE)
                match_start -= WSIZE;
            else
                match_start = 0;
            
            wpos -= WSIZE;
            
            /* Clear hash table since all positions have shifted */
            for (i = 0; i < HASH_SIZE; i++)
                hash_head[i] = 0;
            
            /* Recalculate available space */
            more = (WSIZE * 2) - lookahead - wpos;
        }
        else if (more <= 0) {
            /* Can't read more - window is full */
            break;
        }
        
        if (more > 0) {
            n = fread(window + wpos + lookahead, 1, more, infile);
            if (n > 0) {
                lookahead += n;
            }
            else {
                break;
            }
        }
    } while (lookahead < MIN_LOOKAHEAD && !feof(infile));
}

/*
 * Fixed Huffman code tables (RFC 1951)
 * Literals 0-143:   00110000 - 10111111  (8 bits, values 0x30-0xBF)
 * Literals 144-255: 110010000 - 111111111 (9 bits, values 0x190-0x1FF)
 * EOB 256:          0000000                (7 bits, value 0x0)
 * Lengths 257-279:  0000001 - 0010111     (7 bits, values 0x1-0x17)
 * Lengths 280-287:  11000000 - 11000111   (8 bits, values 0xC0-0xC7)
 */

static void send_literal(int c)
{
    if (c <= 143) {
        /* Codes 0-143: 00110000 through 10111111 (8 bits) */
        /* These need to be bit-reversed: 00001100 through 11111101 */
        put_bits(reverse_bits(0x30 + c, 8), 8);
    } else {
        /* Codes 144-255: 110010000 through 111111111 (9 bits) */
        /* These need to be bit-reversed */
        put_bits(reverse_bits(0x190 + (c - 144), 9), 9);
    }
}

static void send_length(int length)
{
    int code, extra, base_length, i;
    
    /* Simplified length encoding for fixed Huffman */
    /* Lengths 3-10: codes 257-264 (0 extra) */
    /* Lengths 11-18: codes 265-268 (1 extra) */
    /* Lengths 19-34: codes 269-272 (2 extra) */
    /* etc. */
    
    static short base_lengths[] = {
        3, 4, 5, 6, 7, 8, 9, 10,    /* codes 257-264 */
        11, 13, 15, 17,              /* codes 265-268, 1 extra bit */
        19, 23, 27, 31,              /* codes 269-272, 2 extra bits */
        35, 43, 51, 59,              /* codes 273-276, 3 extra bits */
        67, 83, 99, 115,             /* codes 277-280, 4 extra bits */
        131, 163, 195, 227, 258      /* codes 281-285, 5 extra bits (except 258) */
    };
    static short extra_bits[] = {
        0, 0, 0, 0, 0, 0, 0, 0,      /* codes 257-264 */
        1, 1, 1, 1,                  /* codes 265-268 */
        2, 2, 2, 2,                  /* codes 269-272 */
        3, 3, 3, 3,                  /* codes 273-276 */
        4, 4, 4, 4,                  /* codes 277-280 */
        5, 5, 5, 5, 0                /* codes 281-285 */
    };
    
    /* Find the code for this length */
    code = 0;
    for (i = 0; i < 29; i++) {
        if (length < base_lengths[i + 1]) {
            code = i;
            break;
        }
    }
    
    extra = extra_bits[code];
    base_length = base_lengths[code];
    
    /* Send length code (257 + code) */
    if (code <= 22) {
        /* Codes 257-279 are 7 bits: 0000001 through 0010111 */
        put_bits(reverse_bits(code + 1, 7), 7);
    } else {
        /* Codes 280-287 are 8 bits: 11000000 through 11000111 */
        put_bits(reverse_bits(0xC0 + (code - 23), 8), 8);
    }
    
    /* Send extra bits - NOT reversed, just raw value */
    if (extra > 0) {
        put_bits(length - base_length, extra);
    }
}

static void send_distance(int dist)
{
    int code, extra, base_dist;
    
    /* Base distances and extra bits for each distance code */
    static unsigned int base_distances[] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577
    };
    static unsigned char extra_bits[] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    
    /* Find the distance code by comparing to base distances */
    code = 0;
    while (code < 29 && dist >= base_distances[code + 1]) {
        code++;
    }
    
    extra = extra_bits[code];
    base_dist = base_distances[code];
    
    /* Distance codes are 5 bits, bit-reversed */
    put_bits(reverse_bits(code, 5), 5);
    
    /* Send extra bits - NOT reversed, just raw value */
    if (extra > 0) {
        put_bits(dist - base_dist, extra);
    }
}

/*
 * Compress the data
 */
static int compress_data(void)
{
    unsigned int i;
    long file_size, current_pos;
    
    /* Get file size for progress reporting */
    fseek(infile, 0L, 2);  /* SEEK_END */
    file_size = ftell(infile);
    fseek(infile, 0L, 0);  /* SEEK_SET */
    
    /* Initialize hash table */
    for (i = 0; i < HASH_SIZE; i++)
        hash_head[i] = 0;
    
    /* Start with empty window */
    wpos = 0;
    lookahead = 0;
    
    /* Fill initial window */
    fill_window();
    
    if (lookahead == 0)
        return 0;  /* Empty file */
    
    /* Send block header: final block, fixed Huffman */
    put_bits(1, 1);  /* BFINAL = 1 */
    put_bits(1, 2);  /* BTYPE = 01 (fixed Huffman) */
    
    /* Compress the data */
    while (lookahead > 0) {
        static unsigned int count = 0;
        if (++count % 100 == 0) {
            current_pos = ftell(infile);
            if (file_size > 0) {
                int percent = (int)((current_pos * 100L) / file_size);
                fprintf(stderr, "\rCompressing: %d%% (%ld/%ld bytes)", 
                        percent, current_pos, file_size);
            }
        }
        
        /* Try to find a match */
        if (find_match() && match_length >= MIN_MATCH) {
            /* Send length/distance pair */
            int distance = wpos - match_start;
            
            send_length(match_length);
            send_distance(distance);
            
            /* Insert all strings in the match and update CRC */
            for (i = 0; i < match_length; i++) {
                update_crc(window[wpos]);
                input_len++;
                if (lookahead >= MIN_MATCH)
                    insert_string();
                wpos++;
                lookahead--;
                if (lookahead > 0 && i < match_length - 1)
                    fill_window();
            }
        }
        else {
            /* Send literal */
            send_literal(window[wpos]);
            update_crc(window[wpos]);
            input_len++;
            insert_string();
            wpos++;
            lookahead--;
            fill_window();
        }
    }
    
    /* Send end of block (code 256) */
    put_bits(reverse_bits(0, 7), 7);  /* Code 256 is 0000000 (7 bits, reversed) */
    
    /* Pad to byte boundary */
    if (outbits > 0) {
        put_bits(0, 8 - outbits);
    }
    
    /* Clear progress line */
    fprintf(stderr, "\rCompressing: 100%% (%ld/%ld bytes)\n", file_size, file_size);
    
    return 0;
}

int main(int argc, char *argv[])
{
    char *inname, *outname;
    char *basename;
    int len;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }
    
    inname = argv[1];
    
    /* Open input file */
    infile = fopen(inname, "rb");
    if (infile == NULL) {
        perror(inname);
        return 1;
    }
    
    /* Create output filename */
    len = strlen(inname);
    outname = malloc(len + 4);
    if (outname == NULL) {
        fprintf(stderr, "Out of memory\n");
        fclose(infile);
        return 1;
    }
    sprintf(outname, "%s.gz", inname);
    
    /* Get basename for gzip header */
    basename = strrchr(inname, '/');
    basename = basename ? basename + 1 : inname;
    
    /* Open output file */
    outfile = fopen(outname, "wb");
    if (outfile == NULL) {
        perror(outname);
        free(outname);
        fclose(infile);
        return 1;
    }
    
    printf("Compressing %s to %s...\n", inname, outname);
    
    /* Allocate buffers */
    window = (unsigned char *)malloc((unsigned)(WSIZE * 2));
    hash_head = (unsigned short *)malloc((unsigned)(HASH_SIZE * sizeof(unsigned short)));
    prev = (unsigned short *)malloc((unsigned)(WSIZE * sizeof(unsigned short)));
    
    if (window == NULL || hash_head == NULL || prev == NULL) {
        fprintf(stderr, "Error: Cannot allocate compression buffers\n");
        if (window) free(window);
        if (hash_head) free(hash_head);
        if (prev) free(prev);
        fclose(outfile);
        fclose(infile);
        free(outname);
        return 1;
    }
    
    /* Initialize CRC */
    make_crc_table();
    crc = 0xffffffffL;
    input_len = 0;
    
    /* Write gzip header */
    write_header(basename);
    
    /* Compress the data */
    if (compress_data() != 0) {
        fprintf(stderr, "Compression failed\n");
        free(window);
        free(hash_head);
        free(prev);
        fclose(outfile);
        fclose(infile);
        free(outname);
        return 1;
    }
    
    /* Flush output bits */
    flush_bits();
    
    /* Flush stdio buffer to ensure bit output is complete */
    fflush(outfile);
    
    /* Write gzip trailer */
    write_trailer();
    
    printf("Compressed %lu bytes to %ld bytes\n", 
           input_len, ftell(outfile));
    
    /* Cleanup */
    free(window);
    free(hash_head);
    free(prev);
    fclose(outfile);
    fclose(infile);
    free(outname);
    
    return 0;
}
