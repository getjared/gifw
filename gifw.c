/* gifw.c */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* GIF File Header Structures */
#pragma pack(push, 1)
typedef struct {
    char signature[3];
    char version[3];
} GIFHeader;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t packed;
    uint8_t bgColorIndex;
    uint8_t pixelAspectRatio;
} LogicalScreenDescriptor;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ColorTableEntry;

typedef struct {
    uint8_t separator;
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    uint8_t packed;
} ImageDescriptor;

typedef struct {
    uint8_t introducer;
    uint8_t label;
} ExtensionBlock;

typedef struct {
    uint8_t blockSize;
    uint8_t packed;
    uint16_t delayTime;
    uint8_t transparentColorIndex;
    uint8_t terminator;
} GraphicsControlExtension;
#pragma pack(pop)

/* LZW Decompression Structures */
#define MAX_LZW_BITS 12
#define MAX_LZW_CODES (1 << MAX_LZW_BITS)
#define LZW_TABLE_SIZE (MAX_LZW_CODES * 2)

typedef struct {
    int16_t prefix;
    uint8_t suffix;
} LZWEntry;

/* Number of threads to use for image processing */
#define NUM_THREADS 4

/* Enumeration for Display Modes */
typedef enum {
    STRETCH,
    CENTER,
    TILE
} DisplayMode;

/* Structure to pass data to threads */
typedef struct {
    int thread_id;
    uint32_t *dst;
    uint8_t *frameBuffer;
    int destWidth;
    int destHeight;
    int gifWidth;
    int gifHeight;
    int startRow;
    int endRow;
} ThreadData;

/* Additional fields for transparency handling */
typedef struct {
    uint8_t disposalMethod;
    uint8_t transparencyFlag;
    uint8_t transparentColorIndex;
} GraphicControlExtensionData;

/* Helper Functions */
uint16_t read_le_uint16(FILE *fp) {
    uint8_t bytes[2];
    fread(bytes, 1, 2, fp);
    return bytes[0] | (bytes[1] << 8);
}

void skip_sub_blocks(FILE *fp) {
    uint8_t blockSize;
    do {
        fread(&blockSize, 1, 1, fp);
        fseek(fp, blockSize, SEEK_CUR);
    } while (blockSize != 0);
}

/* Function to Read Next Code from LZW Stream */
int read_code(uint8_t **data, int *bitPos, int codeSize) {
    int code = 0;
    for (int i = 0; i < codeSize; i++) {
        code |= ((**data >> *bitPos) & 1) << i;
        (*bitPos)++;
        if (*bitPos == 8) {
            (*data)++;
            *bitPos = 0;
        }
    }
    return code;
}

/* Function to Decode LZW Compressed Data */
int lzw_decode(uint8_t *compressedData, int compressedSize, uint8_t *outData, int width, int height, int lzwMinCodeSize) {
    int clearCode = 1 << lzwMinCodeSize;
    int endCode = clearCode + 1;
    int codeSize = lzwMinCodeSize + 1;
    int maxCode = (1 << codeSize) - 1;

    LZWEntry *table = malloc(sizeof(LZWEntry) * LZW_TABLE_SIZE);
    if (!table) {
        fprintf(stderr, "Failed to allocate LZW table\n");
        return -1;
    }

    for (int i = 0; i < clearCode; i++) {
        table[i].prefix = -1;
        table[i].suffix = i;
    }

    int tableSize = endCode + 1;
    uint8_t *dataPtr = compressedData;
    int bitPos = 0;
    int oldCode = -1;
    int outPos = 0;

    while (1) {
        int code = read_code(&dataPtr, &bitPos, codeSize);

        if (code == clearCode) {
            codeSize = lzwMinCodeSize + 1;
            maxCode = (1 << codeSize) - 1;
            tableSize = endCode + 1;
            oldCode = -1;
            continue;
        } else if (code == endCode) {
            break;
        } else if (code < tableSize) {
            int stackSize = 0;
            uint8_t stack[MAX_LZW_CODES];
            int currentCode = code;
            while (currentCode >= clearCode) {
                stack[stackSize++] = table[currentCode].suffix;
                currentCode = table[currentCode].prefix;
            }
            stack[stackSize++] = currentCode;

            for (int i = stackSize - 1; i >= 0; i--) {
                outData[outPos++] = stack[i];
                if (outPos >= width * height) break;
            }

            if (oldCode != -1) {
                table[tableSize].prefix = oldCode;
                table[tableSize].suffix = currentCode;
                tableSize++;
                if (tableSize > maxCode && codeSize < MAX_LZW_BITS) {
                    codeSize++;
                    maxCode = (1 << codeSize) - 1;
                }
            }
            oldCode = code;
        } else {
            /* Special case */
            int stackSize = 0;
            uint8_t stack[MAX_LZW_CODES];
            int currentCode = oldCode;
            while (currentCode >= clearCode) {
                stack[stackSize++] = table[currentCode].suffix;
                currentCode = table[currentCode].prefix;
            }
            stack[stackSize++] = currentCode;

            uint8_t firstChar = currentCode;

            stack[stackSize++] = firstChar;

            for (int i = stackSize - 1; i >= 0; i--) {
                outData[outPos++] = stack[i];
                if (outPos >= width * height) break;
            }

            table[tableSize].prefix = oldCode;
            table[tableSize].suffix = firstChar;
            tableSize++;
            if (tableSize > maxCode && codeSize < MAX_LZW_BITS) {
                codeSize++;
                maxCode = (1 << codeSize) - 1;
            }
            oldCode = code;
        }

        if (outPos >= width * height) break;
    }

    free(table);
    return 0;
}

/* Function to Read Compressed Data Blocks */
int read_data_blocks(FILE *fp, uint8_t **data, int *dataSize) {
    uint8_t blockSize;
    int totalSize = 0;
    uint8_t *buffer = NULL;

    while (1) {
        fread(&blockSize, 1, 1, fp);
        if (blockSize == 0) break;
        buffer = realloc(buffer, totalSize + blockSize);
        fread(buffer + totalSize, 1, blockSize, fp);
        totalSize += blockSize;
    }

    *data = buffer;
    *dataSize = totalSize;
    return 0;
}

/* Function to decode interlaced images */
void decode_interlaced_image(uint8_t *pixelIndices, int width, int height, uint8_t *decodedPixels) {
    int pass = 0;
    int i = 0;
    int y;
    int offsets[] = {0, 4, 2, 1};
    int steps[] = {8, 8, 4, 2};

    while (pass < 4) {
        for (y = offsets[pass]; y < height; y += steps[pass]) {
            memcpy(decodedPixels + y * width, pixelIndices + i * width, width);
            i++;
        }
        pass++;
    }
}

/* Thread function for bilinear interpolation */
void *bilinear_thread_func(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    uint32_t *dst = data->dst;
    uint8_t *frameBuffer = data->frameBuffer;
    int destWidth = data->destWidth;
    int destHeight = data->destHeight;
    int gifWidth = data->gifWidth;
    int gifHeight = data->gifHeight;
    int startRow = data->startRow;
    int endRow = data->endRow;

    for (int y = startRow; y < endRow; y++) {
        float srcY = y * ((float)gifHeight / destHeight);
        int y0 = (int)srcY;
        int y1 = y0 + 1;
        float y_weight = srcY - y0;
        if (y1 >= gifHeight) y1 = gifHeight - 1;

        for (int x = 0; x < destWidth; x++) {
            float srcX = x * ((float)gifWidth / destWidth);
            int x0 = (int)srcX;
            int x1 = x0 + 1;
            float x_weight = srcX - x0;
            if (x1 >= gifWidth) x1 = gifWidth - 1;

            /* Get the four neighboring pixels */
            int idx00 = (y0 * gifWidth + x0) * 3;
            int idx01 = (y0 * gifWidth + x1) * 3;
            int idx10 = (y1 * gifWidth + x0) * 3;
            int idx11 = (y1 * gifWidth + x1) * 3;

            /* Interpolate red channel */
            float r = (1 - x_weight) * (1 - y_weight) * frameBuffer[idx00] +
                      x_weight * (1 - y_weight) * frameBuffer[idx01] +
                      (1 - x_weight) * y_weight * frameBuffer[idx10] +
                      x_weight * y_weight * frameBuffer[idx11];

            /* Interpolate green channel */
            float g = (1 - x_weight) * (1 - y_weight) * frameBuffer[idx00 + 1] +
                      x_weight * (1 - y_weight) * frameBuffer[idx01 + 1] +
                      (1 - x_weight) * y_weight * frameBuffer[idx10 + 1] +
                      x_weight * y_weight * frameBuffer[idx11 + 1];

            /* Interpolate blue channel */
            float b = (1 - x_weight) * (1 - y_weight) * frameBuffer[idx00 + 2] +
                      x_weight * (1 - y_weight) * frameBuffer[idx01 + 2] +
                      (1 - x_weight) * y_weight * frameBuffer[idx10 + 2] +
                      x_weight * y_weight * frameBuffer[idx11 + 2];

            int dstIdx = y * destWidth + x;
            dst[dstIdx] = (((uint8_t)r) << 16) | (((uint8_t)g) << 8) | ((uint8_t)b);
        }
    }

    pthread_exit(NULL);
}

/* Helper function to get current time in milliseconds */
uint64_t get_current_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec) * 1000 + (ts.tv_nsec) / 1000000;
}

/* Main Program */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <animated-gif-file> [stretch|center|tile]\n", argv[0]);
        exit(1);
    }

    const char *filename = argv[1];
    DisplayMode mode = STRETCH; // Default display mode

    if (argc == 3) {
        if (strcmp(argv[2], "stretch") == 0) {
            mode = STRETCH;
        } else if (strcmp(argv[2], "center") == 0) {
            mode = CENTER;
        } else if (strcmp(argv[2], "tile") == 0) {
            mode = TILE;
        } else {
            fprintf(stderr, "Invalid display mode. Choose from stretch, center, or tile.\n");
            exit(1);
        }
    }

    FILE *gifFile = fopen(filename, "rb");
    if (!gifFile) {
        fprintf(stderr, "Could not open GIF file %s\n", filename);
        exit(1);
    }

    /* Read GIF Header */
    GIFHeader header;
    fread(&header, sizeof(GIFHeader), 1, gifFile);
    if (strncmp(header.signature, "GIF", 3) != 0) {
        fprintf(stderr, "Invalid GIF file\n");
        fclose(gifFile);
        exit(1);
    }

    /* Read Logical Screen Descriptor */
    LogicalScreenDescriptor lsd;
    fread(&lsd, sizeof(LogicalScreenDescriptor), 1, gifFile);
    int gifWidth = lsd.width;
    int gifHeight = lsd.height;

    /* Check for Global Color Table */
    int globalColorTableFlag = (lsd.packed & 0x80) >> 7;
    int globalColorTableSize = 1 << ((lsd.packed & 0x07) + 1);

    /* Read Global Color Table */
    ColorTableEntry *globalColorTable = NULL;
    if (globalColorTableFlag) {
        globalColorTable = malloc(sizeof(ColorTableEntry) * globalColorTableSize);
        fread(globalColorTable, sizeof(ColorTableEntry), globalColorTableSize, gifFile);
    }

    /* Initialize X11 */
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Could not open X display\n");
        fclose(gifFile);
        free(globalColorTable);
        exit(1);
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    GC gc = DefaultGC(display, screen);

    /* Get Screen Dimensions */
    int screenWidth = DisplayWidth(display, screen);
    int screenHeight = DisplayHeight(display, screen);

    XVisualInfo vinfo;
    if (!XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo)) {
        fprintf(stderr, "No matching visual\n");
        XCloseDisplay(display);
        fclose(gifFile);
        free(globalColorTable);
        exit(1);
    }

    Visual *visual = vinfo.visual;

    /* Adjust image size based on display mode */
    int destWidth = screenWidth;
    int destHeight = screenHeight;
    int offsetX = 0;
    int offsetY = 0;

    if (mode == STRETCH) {
        destWidth = screenWidth;
        destHeight = screenHeight;
    } else if (mode == CENTER) {
        destWidth = gifWidth;
        destHeight = gifHeight;
        offsetX = (screenWidth - gifWidth) / 2;
        offsetY = (screenHeight - gifHeight) / 2;
    } else if (mode == TILE) {
        destWidth = screenWidth;
        destHeight = screenHeight;
    }

    XImage *ximage = XCreateImage(display, visual, vinfo.depth, ZPixmap, 0,
                                  NULL, destWidth, destHeight, 32, 0);
    if (!ximage) {
        fprintf(stderr, "Could not create XImage\n");
        XCloseDisplay(display);
        fclose(gifFile);
        free(globalColorTable);
        exit(1);
    }

    ximage->data = malloc(ximage->height * ximage->bytes_per_line);
    if (!ximage->data) {
        fprintf(stderr, "Could not allocate memory for XImage\n");
        XDestroyImage(ximage);
        XCloseDisplay(display);
        fclose(gifFile);
        free(globalColorTable);
        exit(1);
    }

    uint8_t *frameBuffer = malloc(gifWidth * gifHeight * 3);
    if (!frameBuffer) {
        fprintf(stderr, "Could not allocate memory for frame buffer\n");
        free(ximage->data);
        XDestroyImage(ximage);
        XCloseDisplay(display);
        fclose(gifFile);
        free(globalColorTable);
        exit(1);
    }

    /* Variables for transparency and interlacing */
    GraphicControlExtensionData gceData = {0, 0, 0};
    int hasGCE = 0;

    /* Main Loop to Read Frames */
    int running = 1;
    int frameDelay = 100; // default delay in milliseconds
    uint8_t *prevFrame = calloc(gifWidth * gifHeight, sizeof(uint8_t));
    memset(prevFrame, 0, gifWidth * gifHeight);

    while (running) {
        uint64_t frameStartTime = get_current_time_ms();

        int c = fgetc(gifFile);
        if (c == EOF) {
            /* Rewind to start */
            fseek(gifFile, sizeof(GIFHeader) + sizeof(LogicalScreenDescriptor), SEEK_SET);
            if (globalColorTableFlag) {
                fseek(gifFile, sizeof(ColorTableEntry) * globalColorTableSize, SEEK_CUR);
            }
            continue;
        }

        if (c == 0x2C) {
            /* Image Descriptor */
            ImageDescriptor id;
            id.separator = c;
            fread(&id.left, 2, 1, gifFile);
            fread(&id.top, 2, 1, gifFile);
            fread(&id.width, 2, 1, gifFile);
            fread(&id.height, 2, 1, gifFile);
            fread(&id.packed, 1, 1, gifFile);

            /* Interlace Flag */
            int interlaceFlag = (id.packed & 0x40) >> 6;

            /* Local Color Table */
            int localColorTableFlag = (id.packed & 0x80) >> 7;
            int localColorTableSize = 1 << ((id.packed & 0x07) + 1);

            ColorTableEntry *colorTable = globalColorTable;
            if (localColorTableFlag) {
                colorTable = malloc(sizeof(ColorTableEntry) * localColorTableSize);
                fread(colorTable, sizeof(ColorTableEntry), localColorTableSize, gifFile);
            }

            /* LZW Minimum Code Size */
            uint8_t lzwMinCodeSize;
            fread(&lzwMinCodeSize, 1, 1, gifFile);

            /* Read Image Data */
            uint8_t *compressedData = NULL;
            int compressedSize = 0;
            read_data_blocks(gifFile, &compressedData, &compressedSize);

            /* Decode Image Data */
            uint8_t *pixelIndices = malloc(id.width * id.height);
            if (!pixelIndices) {
                fprintf(stderr, "Failed to allocate pixel indices\n");
                free(compressedData);
                continue;
            }
            lzw_decode(compressedData, compressedSize, pixelIndices, id.width, id.height, lzwMinCodeSize);

            /* Handle Interlacing */
            uint8_t *decodedPixels = pixelIndices;
            if (interlaceFlag) {
                decodedPixels = malloc(id.width * id.height);
                decode_interlaced_image(pixelIndices, id.width, id.height, decodedPixels);
                free(pixelIndices);
            }

            /* Build Frame Buffer */
            for (int y = 0; y < gifHeight; y++) {
                for (int x = 0; x < gifWidth; x++) {
                    int idx = y * gifWidth + x;
                    uint8_t colorIndex;

                    if (x >= id.left && x < id.left + id.width && y >= id.top && y < id.top + id.height) {
                        int imgX = x - id.left;
                        int imgY = y - id.top;
                        colorIndex = decodedPixels[imgY * id.width + imgX];

                        /* Handle Transparency */
                        if (hasGCE && gceData.transparencyFlag && colorIndex == gceData.transparentColorIndex) {
                            colorIndex = prevFrame[idx]; // Use previous frame pixel
                        } else {
                            prevFrame[idx] = colorIndex;
                        }
                    } else {
                        colorIndex = prevFrame[idx];
                    }

                    ColorTableEntry color = colorTable[colorIndex];
                    int fbIdx = idx * 3;
                    frameBuffer[fbIdx] = color.red;
                    frameBuffer[fbIdx + 1] = color.green;
                    frameBuffer[fbIdx + 2] = color.blue;
                }
            }

            /* Reset GCE data */
            hasGCE = 0;

            /* Resize and Position Frame Buffer Based on Mode */
            uint32_t *dst = (uint32_t *)ximage->data;
            memset(dst, 0, ximage->height * ximage->bytes_per_line); // Clear the image

            if (mode == STRETCH) {
                /* Multithreaded bilinear interpolation */
                pthread_t threads[NUM_THREADS];
                ThreadData threadData[NUM_THREADS];
                int rowsPerThread = destHeight / NUM_THREADS;

                for (int i = 0; i < NUM_THREADS; i++) {
                    threadData[i].thread_id = i;
                    threadData[i].dst = dst;
                    threadData[i].frameBuffer = frameBuffer;
                    threadData[i].destWidth = destWidth;
                    threadData[i].destHeight = destHeight;
                    threadData[i].gifWidth = gifWidth;
                    threadData[i].gifHeight = gifHeight;
                    threadData[i].startRow = i * rowsPerThread;
                    threadData[i].endRow = (i == NUM_THREADS - 1) ? destHeight : threadData[i].startRow + rowsPerThread;
                    pthread_create(&threads[i], NULL, bilinear_thread_func, (void *)&threadData[i]);
                }

                /* Wait for all threads to complete */
                for (int i = 0; i < NUM_THREADS; i++) {
                    pthread_join(threads[i], NULL);
                }

            } else if (mode == CENTER) {
                /* Center the image */
                for (int y = 0; y < gifHeight; y++) {
                    for (int x = 0; x < gifWidth; x++) {
                        int srcIdx = (y * gifWidth + x) * 3;
                        uint8_t r = frameBuffer[srcIdx];
                        uint8_t g = frameBuffer[srcIdx + 1];
                        uint8_t b = frameBuffer[srcIdx + 2];
                        int dstX = x + offsetX;
                        int dstY = y + offsetY;
                        if (dstX >= 0 && dstX < destWidth && dstY >= 0 && dstY < destHeight) {
                            int dstIdx = dstY * destWidth + dstX;
                            dst[dstIdx] = (r << 16) | (g << 8) | b;
                        }
                    }
                }
            } else if (mode == TILE) {
                /* Tile the image across the screen */
                for (int y = 0; y < destHeight; y++) {
                    for (int x = 0; x < destWidth; x++) {
                        int srcX = x % gifWidth;
                        int srcY = y % gifHeight;
                        int srcIdx = (srcY * gifWidth + srcX) * 3;
                        uint8_t r = frameBuffer[srcIdx];
                        uint8_t g = frameBuffer[srcIdx + 1];
                        uint8_t b = frameBuffer[srcIdx + 2];
                        int dstIdx = y * destWidth + x;
                        dst[dstIdx] = (r << 16) | (g << 8) | b;
                    }
                }
            }

            /* Create pixmap from ximage */
            Pixmap pixmap = XCreatePixmap(display, root, destWidth, destHeight, vinfo.depth);
            XPutImage(display, pixmap, gc, ximage, 0, 0, 0, 0, destWidth, destHeight);

            /* Set root window background pixmap */
            XSetWindowBackgroundPixmap(display, root, pixmap);

            /* Clear root window */
            XClearWindow(display, root);

            /* Remove old pixmap */
            XFreePixmap(display, pixmap);

            /* Flush changes */
            XFlush(display);

            /* Calculate processing time */
            uint64_t frameEndTime = get_current_time_ms();
            uint64_t processingTime = frameEndTime - frameStartTime;

            /* Adjust frame delay */
            int adjustedDelay = frameDelay - (int)processingTime;
            if (adjustedDelay < 0) {
                adjustedDelay = 0; // Prevent negative delay
            }

            /* Sleep for the adjusted frame delay */
            usleep(adjustedDelay * 1000);

            /* Clean up */
            if (localColorTableFlag) {
                free(colorTable);
            }
            free(compressedData);
            if (interlaceFlag) {
                free(decodedPixels);
            } else {
                free(pixelIndices);
            }

        } else if (c == 0x21) {
            /* Extension Block */
            ExtensionBlock ext;
            ext.introducer = c;
            fread(&ext.label, 1, 1, gifFile);

            if (ext.label == 0xF9) {
                /* Graphics Control Extension */
                uint8_t blockSize;
                fread(&blockSize, 1, 1, gifFile);
                uint8_t packed;
                uint16_t delayTime;
                uint8_t transparentColorIndex;
                fread(&packed, 1, 1, gifFile);
                fread(&delayTime, 2, 1, gifFile);
                fread(&transparentColorIndex, 1, 1, gifFile);
                uint8_t terminator;
                fread(&terminator, 1, 1, gifFile);

                /* Use delay time from GCE */
                frameDelay = delayTime * 10; // delay in milliseconds

                /* Handle zero or very small delays */
                if (frameDelay < 20) {
                    frameDelay = 20; // Set minimum delay to 20ms (50 FPS)
                }

                /* Save GCE data for transparency */
                gceData.disposalMethod = (packed >> 2) & 0x07;
                gceData.transparencyFlag = packed & 0x01;
                gceData.transparentColorIndex = transparentColorIndex;
                hasGCE = 1;

            } else {
                /* Skip other extensions */
                skip_sub_blocks(gifFile);
            }
        } else if (c == 0x3B) {
            /* GIF Trailer */
            /* Loop back to start */
            fseek(gifFile, sizeof(GIFHeader) + sizeof(LogicalScreenDescriptor), SEEK_SET);
            if (globalColorTableFlag) {
                fseek(gifFile, sizeof(ColorTableEntry) * globalColorTableSize, SEEK_CUR);
            }
        } else {
            /* Unknown block */
            fprintf(stderr, "Unknown block: 0x%X\n", c);
            break;
        }
    }

    /* Cleanup */
    free(prevFrame);
    free(frameBuffer);
    free(ximage->data);
    XDestroyImage(ximage);
    XCloseDisplay(display);
    fclose(gifFile);
    if (globalColorTable) {
        free(globalColorTable);
    }

    return 0;
}
