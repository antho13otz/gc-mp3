#ifndef PCSIM_FAT_H
#define PCSIM_FAT_H
#include <gccore.h>
#include <sdcard/gcsd.h>
bool fatInitDefault(void);
bool fatMountSimple(const char *name, const DISC_INTERFACE *iface);
bool fatMount(const char *name, const DISC_INTERFACE *iface, sec_t start, u32 cache, u32 sectors);
void fatUnmount(const char *name);
void fatGetVolumeLabel(const char *name, char *label);
int FAT_getAttr(const char *file); int FAT_setAttr(const char *file, uint8_t attr);
#endif
