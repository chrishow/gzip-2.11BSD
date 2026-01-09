/*
 * A *very* minimal gunzip for 2.11BSD
 * DEFLATE decompressor implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GZIP header structure */
#define GZIP_MAGIC1     0x1f
#define GZIP_MAGIC2     0x8b
#define GZIP_DEFLATE    8

/* GZIP flags */
#define FTEXT           0x01    /* File is probably text */
#define FHCRC           0x02    /* Header CRC present */
#define FEXTRA          0x04    /* Extra field present */
#define FNAME           0x08    /* Original filename present */
#define FCOMMENT        0x10    /* File comment present */

/* OS types */
static char *os_names[] = {
    "FAT", "Amiga", "VMS", "Unix", "VM/CMS", "Atari TOS",
    "HPFS", "Macintosh", "Z-System", "CP/M", "TOPS-20",
    "NTFS", "QDOS", "Acorn RISCOS", "unknown"
};

/* DEFLATE constants */
#define MAX_BITS        15
#define MAX_CODES       288
#define WSIZE           32768U  /* Window size - must be unsigned on 16-bit systems */

/* Bit buffer for reading bit-by-bit */
static unsigned long bitbuf = 0;
static int bitcount = 0;
static FILE *infile;

/* Output window for LZ77 decompression */
static unsigned char *window = NULL;
static unsigned int wpos = 0;

/* Progress tracking */
static long compressed_size = 0;
static long bytes_output = 0;
static unsigned int progress_counter = 0;

/* Huffman code structure */
struct huffman {
    short *count;   /* Number of codes of each length */
    short *symbol;  /* Symbols in canonical order */
};

/*
 * Get bits from input stream
 */
static int getbits(int n)
{
    int val;
    
    /* Load bytes into bit buffer until we have enough */
    while (bitcount < n) {
        int c = getc(infile);
        if (c == EOF)
            return -1;
        bitbuf |= ((unsigned long)c << bitcount);
        bitcount += 8;
    }
    
    /* Extract n bits */
    val = bitbuf & ((1 << n) - 1);
    bitbuf >>= n;
    bitcount -= n;
    
    return val;
}

/*
 * Build Huffman decoding tables
 */
static int build_huffman(struct huffman *h, int *length, int n)
{
    int len, code, first, count, index;
    short offs[MAX_BITS + 1];
    int i;
    
    /* Count number of codes for each length */
    for (len = 0; len <= MAX_BITS; len++)
        h->count[len] = 0;
    
    for (i = 0; i < n; i++)
        h->count[length[i]]++;
    
    /* Check for over-subscribed or incomplete set */
    if (h->count[0] == n)
        return 0;   /* Complete, but empty code */
    
    /* Generate offsets into symbol table for each length */
    offs[1] = 0;
    for (len = 1; len < MAX_BITS; len++)
        offs[len + 1] = offs[len] + h->count[len];
    
    /* Put symbols in table sorted by length */
    for (i = 0; i < n; i++)
        if (length[i] != 0)
            h->symbol[offs[length[i]]++] = i;
    
    return 1;
}

/*
 * Decode a symbol from the input using Huffman table
 */
static int decode_symbol(struct huffman *h)
{
    int len, code, first, count, index;
    
    code = first = index = 0;
    
    for (len = 1; len <= MAX_BITS; len++) {
        int bit = getbits(1);
        if (bit < 0)
            return -1;
        
        code |= bit;
        count = h->count[len];
        
        if (code < first + count)
            return h->symbol[index + (code - first)];
        
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    
    return -1;  /* Ran out of codes */
}

/*
 * Output a byte to the window and output file
 */
static void output_byte(unsigned char c, FILE *outfile)
{
    window[wpos++] = c;
    if (wpos >= WSIZE)
        wpos = 0;
    putc(c, outfile);
    bytes_output++;
    
    /* Show progress every 1000 bytes */
    if (++progress_counter % 1000 == 0 && compressed_size > 0) {
        long current_pos = ftell(infile);
        if (current_pos > 0) {
            int percent = (int)((current_pos * 100L) / compressed_size);
            fprintf(stderr, "\rDecompressing: %d%% (%ld/%ld bytes)", 
                    percent, current_pos, compressed_size);
        }
    }
}

/*
 * Decode literal/length and distance codes
 */
static int decode_codes(struct huffman *lencode, struct huffman *distcode, FILE *outfile)
{
    int symbol, len, dist;
    
    /* Length base values */
    static short lens[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static short lext[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    
    /* Distance base values */
    static short dists[] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577
    };
    static short dext[] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    
    /* Decode literals and length/distance pairs */
    while (1) {
        symbol = decode_symbol(lencode);
        if (symbol < 0) return -1;
        
        if (symbol < 256) {
            /* Literal byte */
            output_byte((unsigned char)symbol, outfile);
        }
        else if (symbol == 256) {
            /* End of block */
            break;
        }
        else {
            /* Length/distance pair */
            symbol -= 257;
            if (symbol >= 29) return -1;
            
            len = lens[symbol] + getbits(lext[symbol]);
            
            symbol = decode_symbol(distcode);
            if (symbol < 0) return -1;
            
            dist = dists[symbol] + getbits(dext[symbol]);
            
            /* Copy from window */
            while (len--) {
                unsigned int pos = (wpos >= (unsigned int)dist) ? (wpos - dist) : (WSIZE - dist + wpos);
                output_byte(window[pos], outfile);
            }
        }
    }
    
    return 0;
}

/*
 * Decompress a block with fixed Huffman codes
 */
static int inflate_fixed(FILE *outfile)
{
    struct huffman lencode, distcode;
    short lencnt[MAX_BITS + 1], lensym[288];
    short distcnt[MAX_BITS + 1], distsym[32];
    int lengths[288];
    int i, symbol, len, dist;
    
    /* Build fixed literal/length code */
    lencode.count = lencnt;
    lencode.symbol = lensym;
    
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (i = 144; i < 256; i++) lengths[i] = 9;
    for (i = 256; i < 280; i++) lengths[i] = 7;
    for (i = 280; i < 288; i++) lengths[i] = 8;
    
    build_huffman(&lencode, lengths, 288);
    
    /* Build fixed distance code */
    distcode.count = distcnt;
    distcode.symbol = distsym;
    
    for (i = 0; i < 32; i++) lengths[i] = 5;
    build_huffman(&distcode, lengths, 32);
    
    /* Decode using the fixed codes */
    return decode_codes(&lencode, &distcode, outfile);
}

/*
 * Decompress a block with dynamic Huffman codes
 */
static int inflate_dynamic(FILE *outfile)
{
    struct huffman lencode, distcode, codecode;
    short lencnt[MAX_BITS + 1], lensym[320];
    short distcnt[MAX_BITS + 1], distsym[32];
    short codecnt[MAX_BITS + 1], codesym[19];
    int lengths[320];
    int nlen, ndist, ncode;
    int i, symbol;
    
    /* Order of code length code lengths */
    static short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    
    /* Get number of length and distance codes */
    nlen = getbits(5) + 257;
    ndist = getbits(5) + 1;
    ncode = getbits(4) + 4;
    
    if (nlen > 286 || ndist > 32) {
        fprintf(stderr, "Error: Invalid dynamic block header\n");
        return -1;
    }
    
    /* Read code length code lengths */
    for (i = 0; i < ncode; i++)
        lengths[order[i]] = getbits(3);
    for (; i < 19; i++)
        lengths[order[i]] = 0;
    
    /* Build code length code table */
    codecode.count = codecnt;
    codecode.symbol = codesym;
    if (build_huffman(&codecode, lengths, 19) == 0) {
        fprintf(stderr, "Error: Invalid code length code\n");
        return -1;
    }
    
    /* Read length and distance code lengths */
    i = 0;
    while (i < nlen + ndist) {
        symbol = decode_symbol(&codecode);
        if (symbol < 0) return -1;
        
        if (symbol < 16) {
            /* Length */
            lengths[i++] = symbol;
        }
        else {
            /* Copy or repeat */
            int len = 0;
            int val = 0;
            
            if (symbol == 16) {
                if (i == 0) {
                    fprintf(stderr, "Error: Invalid repeat code\n");
                    return -1;
                }
                val = lengths[i - 1];
                len = 3 + getbits(2);
            }
            else if (symbol == 17) {
                len = 3 + getbits(3);
            }
            else {
                len = 11 + getbits(7);
            }
            
            if (i + len > nlen + ndist) {
                if (inflate_dynamic(outfile) != 0)
                    return -1;
                break;
            }
            
            while (len--)
                lengths[i++] = val;
        }
    }
    
    /* Build literal/length code table */
    lencode.count = lencnt;
    lencode.symbol = lensym;
    if (build_huffman(&lencode, lengths, nlen) == 0) {
        fprintf(stderr, "Error: Invalid literal/length code\n");
        return -1;
    }
    
    /* Build distance code table */
    distcode.count = distcnt;
    distcode.symbol = distsym;
    if (build_huffman(&distcode, lengths + nlen, ndist) == 0) {
        fprintf(stderr, "Error: Invalid distance code\n");
        return -1;
    }
    
    /* Decode using the dynamic codes */
    return decode_codes(&lencode, &distcode, outfile);
}

/*
 * Decompress uncompressed block
 */
static int inflate_uncompressed(FILE *outfile)
{
    unsigned int len, nlen;
    int i;
    
    /* Discard bits to byte boundary */
    bitbuf = 0;
    bitcount = 0;
    
    /* Get length */
    len = getc(infile);
    len |= getc(infile) << 8;
    
    /* Get one's complement of length */
    nlen = getc(infile);
    nlen |= getc(infile) << 8;
    
    /* Check validity */
    if (len != (~nlen & 0xffff)) {
        fprintf(stderr, "Error: Invalid uncompressed block length\n");
        return -1;
    }
    
    /* Copy bytes */
    for (i = 0; i < len; i++) {
        int c = getc(infile);
        if (c == EOF) {
            fprintf(stderr, "Error: Premature EOF in uncompressed block\n");
            return -1;
        }
        output_byte((unsigned char)c, outfile);
    }
    
    return 0;
}

/*
 * Decompress DEFLATE stream
 */
static int inflate(FILE *outfile)
{
    int bfinal, btype;
    
    do {
        /* Read block header */
        bfinal = getbits(1);
        btype = getbits(2);
        
        if (bfinal < 0 || btype < 0) {
            fprintf(stderr, "Error: Cannot read block header\n");
            return -1;
        }
        
        printf("Block: %s, type=%d\n", bfinal ? "final" : "non-final", btype);
        
        switch (btype) {
            case 0:
                /* Uncompressed */
                if (inflate_uncompressed(outfile) != 0)
                    return -1;
                break;
            
            case 1:
                /* Fixed Huffman codes */
                if (inflate_fixed(outfile) != 0)
                    return -1;
                break;
            
            case 2:
                /* Dynamic Huffman codes */
                if (inflate_dynamic(outfile) != 0)
                    return -1;
                break;
            
            case 3:
                fprintf(stderr, "Error: Invalid block type\n");
                return -1;
        }
    } while (!bfinal);
    
    return 0;
}

/*
 * Read and validate gzip header
 */
int read_header(FILE *fp)
{
    unsigned char buf[10];
    unsigned char flags;
    unsigned long mtime;
    int xlen, i, c;
    
    /* Read the 10-byte header */
    if (fread(buf, 1, 10, fp) != 10) {
        fprintf(stderr, "Error: Cannot read header\n");
        return -1;
    }
    
    /* Check magic number */
    if (buf[0] != GZIP_MAGIC1 || buf[1] != GZIP_MAGIC2) {
        fprintf(stderr, "Error: Not a gzip file (magic %02x %02x)\n",
                buf[0], buf[1]);
        return -1;
    }
    
    /* Check compression method */
    if (buf[2] != GZIP_DEFLATE) {
        fprintf(stderr, "Error: Unknown compression method %d\n", buf[2]);
        return -1;
    }
    
    flags = buf[3];
    
    /* Extract modification time (little-endian) */
    mtime = (unsigned long)buf[4] |
            ((unsigned long)buf[5] << 8) |
            ((unsigned long)buf[6] << 16) |
            ((unsigned long)buf[7] << 24);
    
    printf("GZIP Header Information:\n");
    printf("  Magic:         0x%02x 0x%02x (valid)\n", buf[0], buf[1]);
    printf("  Method:        %d (deflate)\n", buf[2]);
    printf("  Flags:         0x%02x\n", flags);
    if (flags & FTEXT)    printf("    - Text file\n");
    if (flags & FHCRC)    printf("    - Header CRC present\n");
    if (flags & FEXTRA)   printf("    - Extra field present\n");
    if (flags & FNAME)    printf("    - Original filename present\n");
    if (flags & FCOMMENT) printf("    - Comment present\n");
    
    printf("  Mod time:      %lu\n", mtime);
    printf("  Extra flags:   0x%02x\n", buf[8]);
    printf("  OS:            %d (%s)\n", buf[9], 
           buf[9] < 14 ? os_names[buf[9]] : "unknown");
    
    /* Handle optional fields */
    
    /* Extra field */
    if (flags & FEXTRA) {
        if (fread(buf, 1, 2, fp) != 2) {
            fprintf(stderr, "Error: Cannot read extra field length\n");
            return -1;
        }
        xlen = buf[0] | (buf[1] << 8);
        printf("  Extra field:   %d bytes\n", xlen);
        /* Skip extra field */
        for (i = 0; i < xlen; i++) {
            if (getc(fp) == EOF) {
                fprintf(stderr, "Error: Premature EOF in extra field\n");
                return -1;
            }
        }
    }
    
    /* Original filename */
    if (flags & FNAME) {
        printf("  Filename:      ");
        while ((c = getc(fp)) != 0 && c != EOF) {
            putchar(c);
        }
        printf("\n");
        if (c == EOF) {
            fprintf(stderr, "Error: Premature EOF in filename\n");
            return -1;
        }
    }
    
    /* Comment */
    if (flags & FCOMMENT) {
        printf("  Comment:       ");
        while ((c = getc(fp)) != 0 && c != EOF) {
            putchar(c);
        }
        printf("\n");
        if (c == EOF) {
            fprintf(stderr, "Error: Premature EOF in comment\n");
            return -1;
        }
    }
    
    /* Header CRC */
    if (flags & FHCRC) {
        if (fread(buf, 1, 2, fp) != 2) {
            fprintf(stderr, "Error: Cannot read header CRC\n");
            return -1;
        }
        printf("  Header CRC:    0x%02x%02x\n", buf[1], buf[0]);
    }
    
    printf("\nHeader parsed successfully!\n");
    printf("Compressed data starts at byte offset: %ld\n", ftell(fp));
    
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *outfile;
    char *outname;
    int len;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <gzip-file>\n", argv[0]);
        return 1;
    }
    
    infile = fopen(argv[1], "r");
    if (infile == NULL) {
        perror(argv[1]);
        return 1;
    }
    
    /* Get compressed file size for progress tracking */
    fseek(infile, 0L, 2);  /* SEEK_END */
    compressed_size = ftell(infile);
    fseek(infile, 0L, 0);  /* SEEK_SET */
    bytes_output = 0;
    progress_counter = 0;
    
    if (read_header(infile) != 0) {
        fclose(infile);
        return 1;
    }
    
    /* Create output filename (remove .gz extension) */
    len = strlen(argv[1]);
    if (len > 3 && strcmp(argv[1] + len - 3, ".gz") == 0) {
        outname = malloc(len - 2);
        strncpy(outname, argv[1], len - 3);
        outname[len - 3] = '\0';
    } else {
        outname = malloc(len + 5);
        sprintf(outname, "%s.out", argv[1]);
    }
    
    printf("\nDecompressing to: %s\n", outname);
    
    outfile = fopen(outname, "w");
    if (outfile == NULL) {
        perror(outname);
        free(outname);
        fclose(infile);
        return 1;
    }
    
    /* Allocate decompression window */
    window = (unsigned char *)malloc((unsigned)WSIZE);
    if (window == NULL) {
        fprintf(stderr, "Error: Cannot allocate 32KB window (out of memory)\n");
        fclose(outfile);
        fclose(infile);
        free(outname);
        return 1;
    }
    
    /* Initialize decompression state */
    bitbuf = 0;
    bitcount = 0;
    wpos = 0;
    memset(window, 0, (unsigned)WSIZE);
    
    /* Decompress */
    if (inflate(outfile) != 0) {
        fprintf(stderr, "\nDecompression failed\n");
        fclose(outfile);
    free(window);
        fclose(infile);
        free(outname);
        free(window);
        return 1;
    }
    
    /* Clear progress line and show completion */
    fprintf(stderr, "\rDecompressing: 100%% (%ld/%ld bytes)\n", 
            compressed_size, compressed_size);
    printf("Decompression successful! Output: %ld bytes\n", bytes_output);
    
    fclose(outfile);
    fclose(infile);
    free(outname);
    return 0;
}
