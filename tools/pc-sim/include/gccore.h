/* GC Station sur PC : remplacant de libogc (en-tetes). Les fonctions sont dans ../pcsim.c.
   Seul ce que GC Station utilise est la. Les valeurs reprennent celles de libogc. */
#ifndef PCSIM_GCCORE_H
#define PCSIM_GCCORE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef volatile u8 vu8; typedef volatile u16 vu16; typedef volatile u32 vu32; typedef volatile u64 vu64;
typedef volatile s8 vs8; typedef volatile s16 vs16; typedef volatile s32 vs32; typedef volatile s64 vs64;
typedef float f32; typedef double f64; typedef volatile float vf32; typedef volatile double vf64;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#define ATTRIBUTE_PACKED __attribute__((packed))
#define MEM_K0_TO_K1(x) ((void *)(x))
#define MEM_K1_TO_K0(x) ((void *)(x))
#define MEM_VIRTUAL_TO_PHYSICAL(x) ((u32)(uintptr_t)(x))
#define MEM_PHYSICAL_TO_K0(x) ((void *)(uintptr_t)(x))
#define MEM_PHYSICAL_TO_K1(x) ((void *)(uintptr_t)(x))
/* ---------- temps ---------- (sur PC : millisecondes) */
u64 gettime(void);
u32 gettick(void);
#define ticks_to_millisecs(t) ((u64)(t))
#define ticks_to_microsecs(t) ((u64)(t) * 1000ull)
#define ticks_to_nanosecs(t)  ((u64)(t) * 1000000ull)
#define ticks_to_secs(t)      ((u64)(t) / 1000ull)
#define millisecs_to_ticks(t) ((u64)(t))
#define microsecs_to_ticks(t) ((u64)(t) / 1000ull)
#define secs_to_ticks(t)      ((u64)(t) * 1000ull)
#define diff_ticks(a, b)      ((u64)(b) - (u64)(a))
#define diff_msec(a, b)       ((u64)(b) - (u64)(a))
#define diff_usec(a, b)       (((u64)(b) - (u64)(a)) * 1000ull)
#define TB_TIMER_CLOCK 1000
void settime(u64 t);
/* ---------- cache, interruptions, memoire ---------- */
void DCFlushRange(void *p, u32 n); void DCInvalidateRange(void *p, u32 n); void DCStoreRange(void *p, u32 n);
void ICInvalidateRange(void *p, u32 n); void DCFlushRangeNoSync(void *p, u32 n); void DCStoreRangeNoSync(void *p, u32 n);
u32 IRQ_Disable(void); void IRQ_Restore(u32 l);
#define _CPU_ISR_Disable(l) ((l) = IRQ_Disable())
#define _CPU_ISR_Restore(l) IRQ_Restore(l)
/* ---------- systeme ---------- */
#define SYS_RESTART 0
#define SYS_HOTRESET 1
#define SYS_SHUTDOWN 2
#define SYS_RETURNTOMENU 3
#define SYS_POWEROFF 4
#define SYS_LANG_ENGLISH 0
#define SYS_LANG_GERMAN 1
#define SYS_LANG_FRENCH 2
#define SYS_LANG_SPANISH 3
#define SYS_LANG_ITALIAN 4
#define SYS_LANG_DUTCH 5
#define SYS_SOUND_MONO 0
#define SYS_SOUND_STEREO 1
#define SYS_FONTENC_ANSI 0
#define SYS_FONTENC_SJIS 1
#define SYS_VIDEO_NTSC 0
#define SYS_VIDEO_PAL 1
#define SYS_VIDEO_MPAL 2
typedef void (*resetcallback)(u32 irq, void *ctx);
typedef void (*powercallback)(void);
void SYS_ResetSystem(s32 reset, u32 code, s32 force);
resetcallback SYS_SetResetCallback(resetcallback cb);
powercallback SYS_SetPowerCallback(powercallback cb);
u32 SYS_GetLanguage(void); void SYS_SetLanguage(u32 l);
u32 SYS_GetSoundMode(void); void SYS_SetSoundMode(u32 m);
u32 SYS_GetFontEncoding(void);
s8 SYS_GetDisplayOffsetH(void); void SYS_SetDisplayOffsetH(s8 o);
u32 SYS_GetVideoMode(void); void SYS_SetVideoMode(u32 m);
u32 SYS_GetProgressiveScan(void); void SYS_SetProgressiveScan(u32 o);
u32 SYS_GetEuRGB60(void); void SYS_SetEuRGB60(u32 o);
u32 SYS_GetRumbleMode(void); void SYS_SetRumbleMode(u32 m);
void *SYS_AllocateFramebuffer(void *mode);
void *SYS_GetArena1Lo(void); void *SYS_GetArena1Hi(void); void SYS_SetArena1Lo(void *p); void SYS_SetArena1Hi(void *p);
u32 SYS_GetArena1Size(void);
s32 SYS_ResetButtonDown(void);
void SYS_Init(void);
void SYS_SwitchFiber(u32 a, u32 b, u32 c, u32 d, u32 pc, u32 sp);
void SYS_ProtectRange(u32 chan, void *addr, u32 bytes, u32 cntrlbits);
typedef struct { u32 a; } syssram;
typedef struct { u32 a; } syssramex;
#define kprintf printf
/* ---------- threads ---------- */
typedef u32 lwp_t; typedef u32 mutex_t; typedef u32 lwpq_t; typedef u32 sem_t; typedef u32 cond_t;
#define LWP_THREAD_NULL 0xffffffff
#define LWP_MUTEX_NULL 0xffffffff
#define LWP_TQUEUE_NULL 0xffffffff
#define LWP_PRIO_IDLE 0
#define LWP_PRIO_HIGHEST 127
s32 LWP_CreateThread(lwp_t *t, void *(*fn)(void *), void *arg, void *stack, u32 size, u8 prio);
s32 LWP_JoinThread(lwp_t t, void **ret);
lwp_t LWP_GetSelf(void);
void LWP_YieldThread(void);
s32 LWP_SuspendThread(lwp_t t); s32 LWP_ResumeThread(lwp_t t);
void LWP_SetThreadPriority(lwp_t t, u32 p);
s32 LWP_MutexInit(mutex_t *m, bool rec); s32 LWP_MutexLock(mutex_t m); s32 LWP_MutexUnlock(mutex_t m);
s32 LWP_MutexTryLock(mutex_t m); s32 LWP_MutexDestroy(mutex_t m);
s32 LWP_InitQueue(lwpq_t *q); void LWP_CloseQueue(lwpq_t q); s32 LWP_ThreadSleep(lwpq_t q); void LWP_ThreadSignal(lwpq_t q); void LWP_ThreadBroadcast(lwpq_t q);
s32 LWP_SemInit(sem_t *s, u32 start, u32 max); s32 LWP_SemWait(sem_t s); s32 LWP_SemPost(sem_t s); s32 LWP_SemDestroy(sem_t s);
s32 LWP_CondInit(cond_t *c); s32 LWP_CondWait(cond_t c, mutex_t m); s32 LWP_CondSignal(cond_t c); s32 LWP_CondBroadcast(cond_t c);
struct timespec;
s32 LWP_CondTimedWait(cond_t c, mutex_t m, const struct timespec *t);
void usleep_ogc(u32 us);
/* ---------- video ---------- */
#define VI_DISPLAY_PIX_SZ 2
#define VI_INTERLACE 0
#define VI_NON_INTERLACE 1
#define VI_PROGRESSIVE 2
#define VI_NTSC 0
#define VI_PAL 1
#define VI_MPAL 2
#define VI_DEBUG 3
#define VI_DEBUG_PAL 4
#define VI_EURGB60 5
#define VI_TVMODE(fmt, mode) (((fmt) << 2) + (mode))
#define VI_TVMODE_NTSC_INT VI_TVMODE(VI_NTSC, VI_INTERLACE)
#define VI_TVMODE_NTSC_PROG VI_TVMODE(VI_NTSC, VI_PROGRESSIVE)
#define VI_TVMODE_PAL_INT VI_TVMODE(VI_PAL, VI_INTERLACE)
#define VI_TVMODE_EURGB60_INT VI_TVMODE(VI_EURGB60, VI_INTERLACE)
#define VI_TVMODE_EURGB60_PROG VI_TVMODE(VI_EURGB60, VI_PROGRESSIVE)
#define VI_XFBMODE_SF 0
#define VI_XFBMODE_DF 1
#define VI_MAX_WIDTH_NTSC 720
#define VI_MAX_WIDTH_PAL 720
#define VI_MAX_WIDTH_EURGB60 720
#define VI_MAX_HEIGHT_NTSC 480
#define VI_MAX_HEIGHT_PAL 574
#define VI_MAX_HEIGHT_EURGB60 480
#define COLOR_BLACK 0x00800080
typedef struct _gx_rmodeobj {
    u32 viTVMode; u16 fbWidth; u16 efbHeight; u16 xfbHeight; u16 viXOrigin; u16 viYOrigin; u16 viWidth; u16 viHeight;
    u32 xfbMode; u8 field_rendering; u8 aa; u8 sample_pattern[12][2]; u8 vfilter[7];
} GXRModeObj;
extern GXRModeObj TVNtsc480IntDf, TVNtsc480Prog, TVPal528IntDf, TVPal576IntDfScale, TVEurgb60Hz480IntDf, TVEurgb60Hz480Prog, TVMpal480IntDf, TVPal524IntAa, TVNtsc480IntAa, TVPal576ProgScale;
typedef void (*VIRetraceCallback)(u32 cnt);
void VIDEO_Init(void); void VIDEO_Configure(GXRModeObj *m); void VIDEO_Flush(void); void VIDEO_WaitVSync(void);
void VIDEO_SetBlack(bool b); void VIDEO_SetNextFramebuffer(void *fb);
void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *fb, u32 color);
GXRModeObj *VIDEO_GetPreferredMode(GXRModeObj *m);
u32 VIDEO_GetCurrentTvMode(void); u32 VIDEO_HaveComponentCable(void); u32 VIDEO_GetNextField(void); u32 VIDEO_GetCurrentLine(void);
VIRetraceCallback VIDEO_SetPostRetraceCallback(VIRetraceCallback cb);
VIRetraceCallback VIDEO_SetPreRetraceCallback(VIRetraceCallback cb);
void *VIDEO_GetCurrentFramebuffer(void); void *VIDEO_GetNextFramebuffer(void);
u32 VIDEO_GetFrameBufferSize(GXRModeObj *m);
void CON_Init(void *fb, int x, int y, int w, int h, int stride);
s32 CON_InitEx(GXRModeObj *m, s32 x, s32 y, s32 w, s32 h);
void CON_GetMetrics(int *cols, int *rows); void CON_GetPosition(int *col, int *row);
/* ---------- manettes ---------- */
#define PAD_CHAN0 0
#define PAD_CHAN1 1
#define PAD_CHAN2 2
#define PAD_CHAN3 3
#define PAD_CHANMAX 4
#define PAD_BUTTON_LEFT 0x0001
#define PAD_BUTTON_RIGHT 0x0002
#define PAD_BUTTON_DOWN 0x0004
#define PAD_BUTTON_UP 0x0008
#define PAD_TRIGGER_Z 0x0010
#define PAD_TRIGGER_R 0x0020
#define PAD_TRIGGER_L 0x0040
#define PAD_BUTTON_A 0x0100
#define PAD_BUTTON_B 0x0200
#define PAD_BUTTON_X 0x0400
#define PAD_BUTTON_Y 0x0800
#define PAD_BUTTON_MENU 0x1000
#define PAD_BUTTON_START 0x1000
#define PAD_MOTOR_STOP 0
#define PAD_MOTOR_RUMBLE 1
#define PAD_MOTOR_STOP_HARD 2
#define PAD_ERR_NONE 0
#define PAD_ERR_NO_CONTROLLER -1
typedef struct _padstatus { u16 button; s8 stickX; s8 stickY; s8 substickX; s8 substickY; u8 triggerL; u8 triggerR; u8 analogA; u8 analogB; s8 err; } PADStatus;
u32 PAD_Init(void); u32 PAD_ScanPads(void); u32 PAD_Read(PADStatus *s); u32 PAD_Reset(u32 mask); u32 PAD_Recalibrate(u32 mask);
u16 PAD_ButtonsUp(int p); u16 PAD_ButtonsDown(int p); u16 PAD_ButtonsHeld(int p);
s8 PAD_StickX(int p); s8 PAD_StickY(int p); s8 PAD_SubStickX(int p); s8 PAD_SubStickY(int p);
u8 PAD_TriggerL(int p); u8 PAD_TriggerR(int p);
void PAD_ControlMotor(s32 chan, u32 cmd);
void PAD_Clamp(PADStatus *s);
/* ---------- SI (clavier GameCube) ---------- */
#define SI_CHAN0 0
#define SI_CHAN1 1
#define SI_CHAN2 2
#define SI_CHAN3 3
#define SI_MAX_CHAN 4
#define SI_MAX_TYPE 4
#define SI_CHAN0_BIT 0x80000000
#define SI_CHAN_BIT(c) (SI_CHAN0_BIT >> (c))
#define SI_ERROR_NO_RESPONSE 0x0008
#define SI_ERROR_BUSY 0x0080
#define SI_TYPE_MASK 0x18000000u
#define SI_TYPE_N64 0x00000000u
#define SI_TYPE_DOLPHIN 0x08000000u
#define SI_TYPE_GC SI_TYPE_DOLPHIN
#define SI_GC_WIRELESS 0x80000000u
#define SI_GC_NOMOTOR 0x20000000u
#define SI_GC_STANDARD 0x01000000u
#define SI_GBA (SI_TYPE_N64 | 0x00040000)
#define SI_GC_CONTROLLER (SI_TYPE_GC | SI_GC_STANDARD)
#define SI_GC_RECEIVER (SI_TYPE_GC | SI_GC_WIRELESS)
#define SI_GC_WAVEBIRD (SI_TYPE_GC | SI_GC_WIRELESS | SI_GC_STANDARD | 0x02000000 | 0x40000000)
#define SI_GC_KEYBOARD (SI_TYPE_GC | 0x00200000)
#define SI_GC_STEERING (SI_TYPE_GC | 0x00000000)
typedef void (*SICallback)(s32 chan, u32 type);
typedef void (*RDSTHandler)(u32 irq, void *ctx);
u32 SI_GetType(s32 chan); u32 SI_GetTypeAsync(s32 chan, SICallback cb); u32 SI_DecodeType(u32 type);
u32 SI_Transfer(s32 chan, void *out, u32 olen, void *in, u32 ilen, SICallback cb, u32 delay);
u32 SI_Sync(void); u32 SI_GetStatus(s32 chan); void SI_SetCommand(s32 chan, u32 cmd); void SI_EnablePolling(u32 poll); void SI_DisablePolling(u32 poll);
u32 SI_IsChanBusy(s32 chan); void SI_TransferCommands(void); u32 SI_GetResponse(s32 chan, void *buf);
/* ---------- son ---------- */
#define AI_SAMPLERATE_32KHZ 0
#define AI_SAMPLERATE_48KHZ 1
typedef void (*AIDCallback)(void);
void AUDIO_Init(u8 *stack); void AUDIO_InitDMA(u32 start, u32 len); void AUDIO_StartDMA(void); void AUDIO_StopDMA(void);
u32 AUDIO_GetDMABytesLeft(void); u32 AUDIO_GetDMAEnableFlag(void); u32 AUDIO_GetDMAStartAddr(void); u32 AUDIO_GetDMALength(void);
AIDCallback AUDIO_RegisterDMACallback(AIDCallback cb);
void AUDIO_SetDSPSampleRate(u8 r); u32 AUDIO_GetDSPSampleRate(void);
void AUDIO_SetStreamVolLeft(u8 v); void AUDIO_SetStreamVolRight(u8 v); void AUDIO_SetStreamSampleRate(u32 r); void AUDIO_SetStreamPlayState(u32 s);
/* ---------- carte memoire (slots A / B) ---------- */
#define CARD_SLOTA 0
#define CARD_SLOTB 1
#define CARD_WORKAREA_SIZE (5 * 8 * 1024)
#define CARD_READSIZE 512
#define CARD_FILENAMELEN 32
#define CARD_MAXFILES 128
#define CARD_ERROR_UNLOCKED 1
#define CARD_ERROR_READY 0
#define CARD_ERROR_BUSY -1
#define CARD_ERROR_WRONGDEVICE -2
#define CARD_ERROR_NOCARD -3
#define CARD_ERROR_NOFILE -4
#define CARD_ERROR_IOERROR -5
#define CARD_ERROR_BROKEN -6
#define CARD_ERROR_EXIST -7
#define CARD_ERROR_NOENT -8
#define CARD_ERROR_INSSPACE -9
#define CARD_ERROR_NOPERM -10
#define CARD_ERROR_LIMIT -11
#define CARD_ERROR_NAMETOOLONG -12
#define CARD_ERROR_ENCODING -13
#define CARD_ERROR_CANCELED -14
#define CARD_ERROR_FATAL_ERROR -128
typedef struct _card_file { s32 chn; s32 filenum; s32 offset; s32 len; u16 iblock; } card_file;
typedef struct _card_dir { s32 chn; u32 fileno; u32 filelen; u8 permissions; char filename[CARD_FILENAMELEN]; u8 gamecode[4]; u8 company[2]; bool showall; } card_dir;
typedef struct _card_direntry { u8 gamecode[4]; u8 company[2]; u8 pad_00; u8 banner_fmt; u8 filename[CARD_FILENAMELEN]; u32 last_modified; u32 icon_addr; u16 icon_fmt; u16 icon_speed; u8 permission; u8 copy_times; u16 block; u16 length; u16 pad_01; u32 comment_addr; } card_direntry;
typedef struct _card_stat { char filename[CARD_FILENAMELEN]; u32 len; u32 time; u8 gamecode[4]; u8 company[2]; u8 banner_fmt; u32 icon_addr; u16 icon_fmt; u16 iconfmt[8]; u16 icon_speed; u16 iconspeed[8]; u32 comment_addr; u32 offset_banner; u32 offset_banner_tlut; u32 offset_icon[8]; u32 offset_icon_tlut[8]; u32 offset_data; } card_stat;
typedef struct _gci { u8 gamecode[4]; u8 company[2]; u8 reserved01; u8 banner_fmt; u8 filename[CARD_FILENAMELEN]; u32 time; u32 icon_addr; u16 icon_fmt; u16 icon_speed; u8 permission; u8 copy_times; u16 first_block; u16 filesize8; u16 reserved02; u32 comment_addr; } __attribute__((packed)) GCI;
typedef void (*cardcallback)(s32 chan, s32 result);
s32 CARD_Init(const char *gamecode, const char *company);
s32 CARD_Mount(s32 chn, void *workarea, cardcallback detach_cb); s32 CARD_Unmount(s32 chn);
s32 CARD_Probe(s32 chn); s32 CARD_ProbeEx(s32 chn, s32 *mem_size, s32 *sect_size);
s32 CARD_Open(s32 chn, const char *filename, card_file *file); s32 CARD_OpenEntry(s32 chn, card_dir *entry, card_file *file);
s32 CARD_Close(card_file *file); s32 CARD_Create(s32 chn, const char *filename, u32 size, card_file *file);
s32 CARD_Read(card_file *file, void *buffer, u32 len, u32 offset); s32 CARD_Write(card_file *file, void *buffer, u32 len, u32 offset);
s32 CARD_Delete(s32 chn, const char *filename); s32 CARD_DeleteEntry(s32 chn, card_dir *dir_entry);
s32 CARD_FindFirst(s32 chn, card_dir *dir, bool showall); s32 CARD_FindNext(card_dir *dir);
s32 CARD_GetStatus(s32 chn, s32 fileno, card_stat *stats); s32 CARD_SetStatus(s32 chn, s32 fileno, card_stat *stats);
s32 CARD_GetStatusEx(s32 chn, s32 fileno, card_direntry *entry); s32 CARD_SetStatusEx(s32 chn, s32 fileno, card_direntry *entry);
s32 CARD_GetSectorSize(s32 chn, u32 *sector_size); s32 CARD_GetBlockCount(s32 chn, u32 *block_count);
s32 CARD_GetFreeBlocks(s32 chn, u16 *freeblocks); s32 CARD_GetMemSize(s32 chn, u16 *mem_size);
s32 CARD_SetCompany(const char *company); s32 CARD_SetGamecode(const char *gamecode);
s32 CARD_Format(s32 chn); s32 CARD_GetSerialNo(s32 chn, u32 *serial1, u32 *serial2);
/* ---------- EXI ---------- */
#define EXI_CHANNEL_0 0
#define EXI_CHANNEL_1 1
#define EXI_CHANNEL_2 2
#define EXI_CHANNEL_MAX 3
#define EXI_DEVICE_0 0
#define EXI_DEVICE_1 1
#define EXI_DEVICE_2 2
#define EXI_SPEED1MHZ 0
#define EXI_SPEED16MHZ 4
#define EXI_SPEED32MHZ 5
#define EXI_READ 0
#define EXI_WRITE 1
#define EXI_READWRITE 2
s32 EXI_Lock(s32 c, s32 d, void *cb); s32 EXI_Unlock(s32 c); s32 EXI_Select(s32 c, s32 d, s32 f); s32 EXI_Deselect(s32 c);
s32 EXI_Imm(s32 c, void *p, u32 l, u32 m, void *cb); s32 EXI_ImmEx(s32 c, void *p, u32 l, u32 m); s32 EXI_Sync(s32 c);
s32 EXI_Dma(s32 c, void *p, u32 l, u32 m, void *cb); s32 EXI_Probe(s32 c); s32 EXI_ProbeEx(s32 c); s32 EXI_GetID(s32 c, s32 d, u32 *id);
s32 EXI_Attach(s32 c, void *cb); s32 EXI_Detach(s32 c);
/* ---------- divers ---------- */
#define ARQ_PRIO_LO 0
#define ARQ_PRIO_HI 1
void *SYS_GetArenaLo(void); void *SYS_GetArenaHi(void);
#include "pcsim_gx.h"
#include "pcsim_gu.h"
#ifdef __cplusplus
}
#endif
#endif
