# gzip/gunzip for 2.11BSD

A minimal gzip compressor and decompressor implementation for the PDP-11 running 2.11BSD Unix.

## Overview

This is a bare-bones implementation of the gzip/DEFLATE compression and decompression algorithms, specifically designed to run on vintage PDP-11 hardware with limited memory. It provides both compression (gzip) and decompression (gunzip) utilities that are compatible with standard gzip files.

## Features

### gunzip (Decompressor)
- **Complete DEFLATE Implementation**: Supports all three block types:
  - Uncompressed blocks (type 0)
  - Fixed Huffman coding (type 1)
  - Dynamic Huffman coding (type 2)
- **Full gzip Header Parsing**: Reads and displays all header information including modification time, flags, original filename, comments, and header CRC
- **CRC32 Verification**: Validates data integrity by verifying CRC32 checksums
- **32KB Sliding Window**: Full DEFLATE-compliant window size
- **Progress Indication**: Shows decompression progress with percentage and bytes processed
- **Standard Compatibility**: Decompresses files created by any standard gzip implementation

### gzip (Compressor)
- **LZ77 String Matching**: Hash-based matching with 4KB sliding window
- **Memory Efficient**: ~12KB total memory usage (4KB window + hash tables)
- **Standard gzip Format**: Creates files compatible with all gunzip implementations
- **CRC32 Checksums**: Generates proper CRC32 checksums for data integrity
- **Progress Indication**: Shows compression progress with percentage and bytes processed


## Building

```bash
make
```

This will compile both utilities:
- `gunzip.c` → `gunzip` executable (decompressor)
- `gzip.c` → `gzip` executable (compressor)

## Usage

### Compressing Files

```bash
./gzip filename
```

This creates `filename.gz` containing the compressed data. The compressor will:
1. Create a standard gzip header with the original filename
2. Compress using LZ77 matching and fixed Huffman encoding
3. Generate CRC32 checksum and write gzip trailer

**Example:**
```bash
$ ./gzip LICENSE
Compressing LICENSE to LICENSE.gz...
Compressed 1083 bytes to 616 bytes
```

### Decompressing Files

```bash
./gunzip filename.gz
```

The decompressor will:
1. Parse and display the gzip header information
2. Decompress the file
3. Write the output to `filename` (removing the `.gz` extension)

If the input file doesn't end in `.gz`, the output will be written to `filename.out`.

**Example:**
```bash
$ ./gunzip LICENSE.gz
GZIP Header Information:
  Magic:         0x1f 0x8b (valid)
  Method:        8 (deflate)
  Flags:         0x08
    - Original filename present
  Mod time:      1767952414
  Extra flags:   0x04
  OS:            3 (Unix)
  Filename:      LICENSE

Header parsed successfully!
Compressed data starts at byte offset: 21

Decompressing to: LICENSE
Block: final, type=1
Decompression successful! Output: 1083 bytes (CRC OK)
```

## Implementation Details

### Compression Strategy
- Uses **fixed Huffman coding** (DEFLATE type 1 blocks) for simplicity and compatibility
- **4KB sliding window** with hash-based string matching (reduced from 32KB to fit memory constraints)
- Hash chain depth limited to 128 entries for performance
- Minimum match length: 3 bytes
- Maximum match length: 258 bytes

### Memory Usage
- **gzip**: ~12KB total
  - 8KB sliding window (4KB × 2 for circular buffer)
  - 2KB hash table (2048 entries × 2 bytes)
  - 2KB previous links
- **gunzip**: ~32KB
  - 32KB sliding window (full DEFLATE specification)

## Limitations

- **Compression is very slow!** It takes my (emulated) PDP-11 22 minutes to compress the King James Version of the Bible (4.6MB to 2.3MB) compared to the 6 minutes it takes to decompress the file. 
- **Single file processing**: Processes one file at a time
- **Fixed Huffman only for compression**: Does not generate dynamic Huffman trees (but achieves reasonable compression ratios)
- **Reduced window size for compression**: 4KB window vs. standard 32KB (may reduce compression ratio on large files with distant matches)

## License

This software is released under the MIT license. See the [LICENSE](LICENSE) file for license information. **Use at your own risk**. 
