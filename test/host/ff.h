// Minimal FatFs API shim over stdio so convert.c can run on a PC.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE } FRESULT;
typedef struct { FILE *fp; } FIL;
typedef struct { FSIZE_t fsize; } FILINFO;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

FRESULT f_open(FIL *f, const char *path, int mode);
FRESULT f_close(FIL *f);
FRESULT f_read(FIL *f, void *buf, UINT n, UINT *br);
FRESULT f_write(FIL *f, const void *buf, UINT n, UINT *bw);
FRESULT f_lseek(FIL *f, FSIZE_t ofs);
FSIZE_t f_tell(FIL *f);
FRESULT f_unlink(const char *path);
FRESULT f_stat(const char *path, FILINFO *fi);
