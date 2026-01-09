# gunzip for 2.11BSD

A minimal gzip decompressor implementation for the PDP-11 running 2.11BSD Unix.

## Overview

This is a bare-bones implementation of the gzip/DEFLATE decompression algorithm, specifically designed to run on vintage PDP-11 hardware with limited memory.

## Features

- **Complete DEFLATE Implementation**: Supports all three block types:
  - Uncompressed blocks (type 0)
  - Fixed Huffman coding (type 1)
  - Dynamic Huffman coding (type 2)
- **Full gzip Header Parsing**: Reads and displays all header information including modification time, flags, original filename, comments, and header CRC
- **32KB Sliding Window**: Full DEFLATE-compliant window size

## Requirements

- PDP-11 running 2.11BSD Unix
- Standard C compiler (`cc`)
- ~32KB available heap memory for decompression window

## Building

```bash
make
```

This will compile `gunzip.c` into the `gunzip` executable.


## Usage

Decompress a gzip file:

```bash
./gunzip filename.gz
```

The decompressor will:
1. Parse and display the gzip header information
2. Decompress the file
3. Write the output to `filename` (removing the `.gz` extension)

If the input file doesn't end in `.gz`, the output will be written to `filename.out`.

### Example

```bash
$ ./gunzip LICENSE.gz
GZIP Header Information:
  Magic:         0x1f 0x8b (valid)
  Method:        8 (deflate)
  Flags:         0x08
    - Original filename present
  Mod time:      1767946753
  Extra flags:   0x00
  OS:            3 (Unix)
  Filename:      LICENSE

Header parsed successfully!
Compressed data starts at byte offset: 18

Decompressing to: LICENSE
Block: final, type=2
Decompression successful!
```

## Limitations

- **Decompression only**: Does not compress files (memory constraints make compression impractical)
- **Single file processing**: Processes one file at a time
- **No CRC verification**: Does not verify the CRC32 checksum in the gzip trailer

## License

This software is released under the MIT license. See the [LICENSE](LICENSE) file for license information. **Use at your own risk**. 
