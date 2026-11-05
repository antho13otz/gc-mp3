/* GC Station v326 : les noms de FatFs prefixes (exf_) : la libfat de devkitPro (libdvm) contient deja un FatFs
   (sans exFAT), avec les memes noms ; sans prefixe, la liaison echoue (definitions multiples). */
#ifndef FF_PREFIX_H
#define FF_PREFIX_H
#define disk_initialize exf_disk_initialize
#define disk_ioctl exf_disk_ioctl
#define disk_read exf_disk_read
#define disk_status exf_disk_status
#define disk_write exf_disk_write
#define f_chdir exf_f_chdir
#define f_chdrive exf_f_chdrive
#define f_chmod exf_f_chmod
#define f_close exf_f_close
#define f_closedir exf_f_closedir
#define f_expand exf_f_expand
#define f_fdisk exf_f_fdisk
#define f_findfirst exf_f_findfirst
#define f_findnext exf_f_findnext
#define f_forward exf_f_forward
#define f_getcwd exf_f_getcwd
#define f_getfree exf_f_getfree
#define f_getlabel exf_f_getlabel
#define f_gets exf_f_gets
#define f_lseek exf_f_lseek
#define f_mkdir exf_f_mkdir
#define f_mkfs exf_f_mkfs
#define f_mount exf_f_mount
#define f_open exf_f_open
#define f_opendir exf_f_opendir
#define f_printf exf_f_printf
#define f_putc exf_f_putc
#define f_puts exf_f_puts
#define f_read exf_f_read
#define f_readdir exf_f_readdir
#define f_rename exf_f_rename
#define f_setcp exf_f_setcp
#define f_setlabel exf_f_setlabel
#define f_stat exf_f_stat
#define f_sync exf_f_sync
#define f_truncate exf_f_truncate
#define f_unlink exf_f_unlink
#define f_utime exf_f_utime
#define f_write exf_f_write
#define ff_memalloc exf_ff_memalloc
#define ff_memfree exf_ff_memfree
#define ff_mutex_create exf_ff_mutex_create
#define ff_mutex_delete exf_ff_mutex_delete
#define ff_mutex_give exf_ff_mutex_give
#define ff_mutex_take exf_ff_mutex_take
#define ff_oem2uni exf_ff_oem2uni
#define ff_uni2oem exf_ff_uni2oem
#define ff_wtoupper exf_ff_wtoupper
#define get_fattime exf_get_fattime
#endif
