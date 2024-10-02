# gif wallpaper: animated wallpaper for X11 systems

## overview

this program reads a gif file and sets it as an animated wallpaper on X11-based systems. it supports various display modes and uses multi-threading for improved performance.

## dependencies

relies on the following libraries:

- `stdio.h`: standard input/output operations
- `stdlib.h`: memory allocation, process control
- `stdint.h`: fixed-width integer types
- `unistd.h`: POSIX operating system API
- `X11/Xlib.h`: X11 library for window system operations
- `X11/Xutil.h`: utility functions for X11
- `string.h`: string manipulation functions
- `pthread.h`: POSIX thread library
- `time.h`: time and date functions

## key structures

uses several structures to handle gif data:

- `GIFHeader`: stores gif file header information
- `LogicalScreenDescriptor`: contains logical screen descriptor data
- `ColorTableEntry`: represents an entry in the color table
- `ImageDescriptor`: holds image descriptor information
- `ExtensionBlock`: represents an extension block in the gif
- `GraphicsControlExtension`: stores graphics control extension data
- `LZWEntry`: used for LZW decompression
- `ThreadData`: contains data passed to threads
- `GraphicControlExtensionData`: stores graphic control extension data

## main functions

### `int main(int argc, char *argv[])`

the main function. it:
1. parses command-line arguments
2. opens and reads the gif file
3. initializes the X11 display
4. processes gif frames
5. handles different display modes
6. continuously updates the wallpaper until terminated

## helper functions

includes several helper functions:

### `uint16_t read_le_uint16(FILE *fp)`
reads a 16-bit unsigned integer in little-endian format.

### `void skip_sub_blocks(FILE *fp)`
skips over sub-blocks in the gif file.

### `int read_code(uint8_t **data, int *bitPos, int codeSize)`
reads LZW codes from the compressed data.

### `int lzw_decode(...)`
decodes LZW compressed data.

### `int read_data_blocks(FILE *fp, uint8_t **data, int *dataSize)`
reads data blocks from the gif file.

### `void decode_interlaced_image(...)`
decodes interlaced gif images.

### `uint64_t get_current_time_ms()`
retrieves the current time in milliseconds.

## threading

uses POSIX threads for parallel processing:

### `void *bilinear_thread_func(void *arg)`
performs bilinear interpolation in parallel for image scaling.

## display modes

supports three display modes:

1. `STRETCH`: scales the gif to fill the entire screen
2. `CENTER`: centers the gif on the screen without scaling
3. `TILE`: repeats the gif to fill the screen

## error handling

implements error checking throughout, including:
- file opening and reading
- memory allocation
- X11 display initialization
- thread creation and management

## optimization techniques

several optimization techniques are employed:
- multi-threading for bilinear interpolation to improve scaling performance
- reuse of frame buffers and structures to minimize memory allocation
- frame timing adjustment to account for processing time and maintain correct animation speed
