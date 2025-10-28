#ifndef DRIVER_H
#define DRIVER_H

#include <ntifs.h>
#include <windef.h>
#include <intrin.h>

#pragma intrinsic(__readcr3)

// IOCTLs
#define IOCTL_RW           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x9C2, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_BASE         CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA7F, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GUARD        CTL_CODE(FILE_DEVICE_UNKNOWN, 0xBD1, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define AUTH               0x7461F3B9

#define PAGE_SHIFT 12
#define PAGE_SIZE 0x1000
#define CACHE_SIZE 4096
#define PAGE_MASK 0xFFFFFFFFF000ULL

typedef struct _IO_RW {
    INT32 key;
    INT32 pid;
    ULONGLONG addr;
    ULONGLONG buf;
    ULONGLONG size;
    BOOLEAN wr;
} IO_RW, *PIO_RW;

typedef struct _IO_BASE {
    INT32 key;
    INT32 pid;
    ULONGLONG* out;
} IO_BASE, *PIO_BASE;

typedef struct _IO_GUARD {
    INT32 key;
    ULONGLONG* out;
} IO_GUARD, *PIO_GUARD;

typedef struct _POOL_ENTRY {
    PVOID addr;
    ULONG_PTR np : 1;
    ULONG_PTR size;
    UCHAR tag[4];
} POOL_ENTRY, *PPOOL_ENTRY;

typedef struct _POOL_INFO {
    ULONG count;
    POOL_ENTRY entries[1];
} POOL_INFO, *PPOOL_INFO;

typedef enum _SYSINFO {
    SystemBigPoolInformation = 0x42,
} SYSINFO;

typedef struct _PTE_CACHE_ENTRY {
    UINT64 virtPage;
    UINT64 physPage;
} PTE_CACHE_ENTRY;

extern NTSTATUS NTAPI IoCreateDriver(PUNICODE_STRING, PDRIVER_INITIALIZE);
extern PVOID NTAPI PsGetProcessSectionBaseAddress(PEPROCESS);
extern NTSTATUS NTAPI ZwQuerySystemInformation(SYSINFO, PVOID, ULONG, PULONG);

extern UINT64 AsmCachedVirtToPhys(UINT64 cr3, UINT64 va, PTE_CACHE_ENTRY* cache);
extern int AsmCacheIndex(UINT64 virtPage);
extern void AsmFastMemCopy(void* dst, const void* src, SIZE_T len);

NTSTATUS ReadPhys(PVOID paddr, PVOID out, SIZE_T len, SIZE_T* done);
NTSTATUS WritePhys(PVOID paddr, PVOID in, SIZE_T len, SIZE_T* done);
INT32 FindCr3OffsetDynamic(void);
UINT64 GetCr3(PEPROCESS p);
NTSTATUS HandleRWBatch(PIO_RW r);
NTSTATUS HandleBase(PIO_BASE r);
NTSTATUS HandleGuard(PIO_GUARD r);

#endif // DRIVER_H