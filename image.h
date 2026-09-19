#ifndef IMAGE_H
#define IMAGE_H

typedef struct {
    short x, y;
    unsigned char format;
    void *data; /* int* или unsigned short* */
} Image;

#endif
