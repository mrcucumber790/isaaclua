#pragma once
/* The few Nintendo SDK / rtld functions we call, declared for C by their mangled names.
   All of them are imported by Repentance.nro itself, so the SDK is known to export them. */
#include <stddef.h>
#include <stdint.h>

typedef struct { void *handle; } nnfs_FileHandle;
typedef struct { void *handle; } nnfs_DirectoryHandle;
typedef struct {
    char name[769];
    char pad0[3];
    int8_t type;  /* 0 = directory, 1 = file */
    char pad1[3];
    int64_t size;
} nnfs_DirectoryEntry;  /* nn::fs::DirectoryEntry, 0x310 bytes */

enum { NNFS_OPEN_READ = 1, NNFS_OPEN_WRITE = 2, NNFS_DIR_DIRECTORIES = 1, NNFS_DIR_FILES = 2 };
typedef struct { int flags; } nnfs_WriteOption;  /* 1 = flush */

uint32_t nnfs_OpenFile(nnfs_FileHandle *out, const char *path, int mode)
    __asm__("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
uint32_t nnfs_GetFileSize(int64_t *out, nnfs_FileHandle f) __asm__("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE");
uint32_t nnfs_ReadFile(size_t *out, nnfs_FileHandle f, int64_t offset, void *buf, size_t size)
    __asm__("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
void nnfs_CloseFile(nnfs_FileHandle f) __asm__("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
uint32_t nnfs_OpenDirectory(nnfs_DirectoryHandle *out, const char *path, int mode)
    __asm__("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
uint32_t nnfs_ReadDirectory(int64_t *count, nnfs_DirectoryEntry *entries, nnfs_DirectoryHandle d, int64_t max)
    __asm__("_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
void nnfs_CloseDirectory(nnfs_DirectoryHandle d) __asm__("_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");
uint32_t nnfs_WriteFile(nnfs_FileHandle f, int64_t offset, const void *buf, size_t size, const nnfs_WriteOption *opt)
    __asm__("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE");
uint32_t nnfs_CreateFile(const char *path, int64_t size) __asm__("_ZN2nn2fs10CreateFileEPKcl");
uint32_t nnfs_DeleteFile(const char *path) __asm__("_ZN2nn2fs10DeleteFileEPKc");
uint32_t nnfs_CreateDirectory(const char *path) __asm__("_ZN2nn2fs15CreateDirectoryEPKc");
uint32_t nnfs_CommitSaveData(const char *mount) __asm__("_ZN2nn2fs14CommitSaveDataEPKc");
_Bool nnfs_IsMounted(const char *mount) __asm__("_ZN2nn2fs9IsMountedEPKc");
typedef struct { uint64_t hi, lo; } nnacc_Uid;  /* nn::account::Uid */
uint32_t nnfs_MountSaveData(const char *mount, const nnacc_Uid *uid) __asm__("_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE");
void nnfs_Unmount(const char *mount) __asm__("_ZN2nn2fs7UnmountEPKc");

typedef struct {
    int64_t delta_time;
    uint32_t attributes;
    int32_t finger_id, x, y, diameter_x, diameter_y, rotation, reserved;
} nnhid_TouchState;  /* nn::hid::TouchState, 40 bytes; x, y in 1280x720 */
typedef struct {
    int64_t sampling;
    int32_t count, reserved;
    nnhid_TouchState touches[16];
} nnhid_TouchScreenState16;
void nnhid_GetTouchScreenState16(nnhid_TouchScreenState16 *out)
    __asm__("_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE");

int64_t nnosGetSystemTick(void);
int64_t nnosGetSystemTickFrequency(void);

struct nnro_Module;
uint32_t nnro_LoadModule(struct nnro_Module *m, const void *image, void *buf, size_t size, int flag)
    __asm__("_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmi");
uint32_t nnro_LookupModuleSymbol(uintptr_t *out, const struct nnro_Module *m, const char *name)
    __asm__("_ZN2nn2ro18LookupModuleSymbolEPmPKNS0_6ModuleEPKc");
void *nnosGetCurrentThread(void) __asm__("_ZN2nn2os16GetCurrentThreadEv");

/* debug build only (screenshots of the autotest): exported by nnSdk, not imported by the game itself */
uint32_t nnalbum_Initialize(void) __asm__("_ZN2nn5album10InitializeEv");
uint32_t nnalbum_SaveCurrentScreenshot(int report) __asm__("_ZN2nn5album21SaveCurrentScreenshotENS0_17AlbumReportOptionE");
void nnoe_RequestToRelaunchApplication(void) __asm__("_ZN2nn2oe28RequestToRelaunchApplicationEv");
