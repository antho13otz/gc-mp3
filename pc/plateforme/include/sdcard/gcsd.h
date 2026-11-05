#ifndef PCSIM_GCSD_H
#define PCSIM_GCSD_H
#include <gccore.h>
typedef u32 sec_t;
typedef struct DISC_INTERFACE_STRUCT {
    unsigned long ioType, features;
    bool (*startup)(void); bool (*isInserted)(void);
    bool (*readSectors)(sec_t sector, sec_t n, void *buf); bool (*writeSectors)(sec_t sector, sec_t n, const void *buf);
    bool (*clearStatus)(void); bool (*shutdown)(void);
} DISC_INTERFACE;
extern const DISC_INTERFACE __io_gcsda, __io_gcsdb, __io_gcsd2;
#define DEVICE_TYPE_GAMECUBE_SD(x) (('G' << 24) | ('C' << 16) | ('S' << 8) | ('0' + (x)))
#endif
