#include "driver.h"

static UNICODE_STRING g_devName;
static UNICODE_STRING g_symLink;
static PTE_CACHE_ENTRY g_pteCache[CACHE_SIZE] = {0};

NTSTATUS ReadPhys(PVOID paddr, PVOID out, SIZE_T len, SIZE_T* done) {
    MM_COPY_ADDRESS mm = {0};
    mm.PhysicalAddress.QuadPart = (LONGLONG)paddr;
    return MmCopyMemory(out, mm, len, MM_COPY_MEMORY_PHYSICAL, done);
}

NTSTATUS WritePhys(PVOID paddr, PVOID in, SIZE_T len, SIZE_T* done) {
    PHYSICAL_ADDRESS pa;
    PVOID map;
    
    if (!paddr) return STATUS_UNSUCCESSFUL;
    
    pa.QuadPart = (LONGLONG)paddr;
    map = MmMapIoSpaceEx(pa, len, PAGE_READWRITE);
    
    if (!map) return STATUS_UNSUCCESSFUL;
    
    if (len <= 64) {
        AsmFastMemCopy(map, in, len);
    } else {
        RtlCopyMemory(map, in, len);
    }
    
    *done = len;
    MmUnmapIoSpace(map, len);
    return STATUS_SUCCESS;
}

INT32 FindCr3OffsetDynamic(void) {
    PEPROCESS currentProcess;
    ULONG_PTR knownCr3;
    PUCHAR proc;
    int offset;
    
    currentProcess = PsGetCurrentProcess();
    if (!currentProcess) return 0;
    
    knownCr3 = __readcr3() & ~0xFULL;
    proc = (PUCHAR)currentProcess;
    
    for (offset = 0; offset < 0x500; offset += sizeof(ULONG_PTR)) {
        ULONG_PTR val = *(PULONG_PTR)(proc + offset);
        if ((val & ~0xFULL) == knownCr3) {
            return offset;
        }
    }
    
    return 0x2E8;
}

UINT64 GetCr3(PEPROCESS p) {
    PUCHAR ptr = (PUCHAR)p;
    ULONG_PTR val = *(PULONG_PTR)(ptr + 0x28);
    
    if (!val) {
        val = *(PULONG_PTR)(ptr + FindCr3OffsetDynamic());
    }
    
    return val;
}

NTSTATUS HandleRWBatch(PIO_RW r) {
    PEPROCESS p = NULL;
    UINT64 cr3;
    SIZE_T totalMoved = 0;
    SIZE_T toMove = 0;
    SIZE_T sizeLeft;
    UINT64 currVirt;
    UINT64 currBuf;
    UINT64 physAddr;
    SIZE_T pageOffset;
    NTSTATUS st;
    
    if (!r || r->key != AUTH || !r->pid || !r->size) {
        return STATUS_UNSUCCESSFUL;
    }
    
    if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(LONG_PTR)r->pid, &p))) {
        return STATUS_UNSUCCESSFUL;
    }
    
    cr3 = GetCr3(p);
    
    sizeLeft = (SIZE_T)r->size;
    currVirt = r->addr;
    currBuf = r->buf;
    
    while (sizeLeft > 0) {
        physAddr = AsmCachedVirtToPhys(cr3, currVirt, g_pteCache);
        
        if (!physAddr) break;
        
        pageOffset = physAddr & 0xFFF;
        toMove = (sizeLeft < (PAGE_SIZE - pageOffset)) ? sizeLeft : (PAGE_SIZE - pageOffset);
        
        if (r->wr) {
            st = WritePhys((PVOID)physAddr, (PVOID)(UINT_PTR)currBuf, toMove, &toMove);
            if (!NT_SUCCESS(st)) {
                ObDereferenceObject(p);
                return st;
            }
        } else {
            st = ReadPhys((PVOID)physAddr, (PVOID)(UINT_PTR)currBuf, toMove, &toMove);
            if (!NT_SUCCESS(st)) {
                ObDereferenceObject(p);
                return st;
            }
        }
        
        currVirt += toMove;
        currBuf += toMove;
        sizeLeft -= toMove;
        totalMoved += toMove;
    }
    
    ObDereferenceObject(p);
    
    return (totalMoved == r->size) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS HandleBase(PIO_BASE r) {
    PEPROCESS p = NULL;
    ULONGLONG b;
    
    if (!r || r->key != AUTH || !r->pid) {
        return STATUS_UNSUCCESSFUL;
    }
    
    if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(LONG_PTR)r->pid, &p))) {
        return STATUS_UNSUCCESSFUL;
    }
    
    b = (ULONGLONG)PsGetProcessSectionBaseAddress(p);
    RtlCopyMemory(r->out, &b, sizeof(b));
    
    ObDereferenceObject(p);
    return STATUS_SUCCESS;
}

NTSTATUS HandleGuard(PIO_GUARD r) {
    ULONG sz = 0;
    PPOOL_INFO pool;
    ULONG i;
    NTSTATUS st;
    
    if (!r || r->key != AUTH) {
        return STATUS_UNSUCCESSFUL;
    }
    
    ZwQuerySystemInformation(SystemBigPoolInformation, &sz, 0, &sz);
    
    pool = (PPOOL_INFO)ExAllocatePool(NonPagedPool, sz);
    if (!pool) return STATUS_INSUFFICIENT_RESOURCES;
    
    st = ZwQuerySystemInformation(SystemBigPoolInformation, pool, sz, &sz);
    if (!NT_SUCCESS(st)) {
        ExFreePool(pool);
        return STATUS_UNSUCCESSFUL;
    }
    
    for (i = 0; i < pool->count; ++i) {
        PPOOL_ENTRY e = &pool->entries[i];
        if (e->np && e->size == 0x200000 && *(ULONG*)e->tag == 'CnoT') {
            *r->out = (ULONGLONG)e->addr;
            ExFreePool(pool);
            return STATUS_SUCCESS;
        }
    }
    
    ExFreePool(pool);
    return STATUS_NOT_FOUND;
}

NTSTATUS IoCtl(PDEVICE_OBJECT dev, PIRP irp) {
    PIO_STACK_LOCATION io;
    PVOID buf;
    ULONG code;
    ULONG inSize;
    NTSTATUS st = STATUS_INVALID_DEVICE_REQUEST;
    ULONG out = 0;
    
    UNREFERENCED_PARAMETER(dev);
    
    io = IoGetCurrentIrpStackLocation(irp);
    buf = irp->AssociatedIrp.SystemBuffer;
    code = io->Parameters.DeviceIoControl.IoControlCode;
    inSize = io->Parameters.DeviceIoControl.InputBufferLength;
    
    switch (code) {
        case IOCTL_RW:
            if (inSize == sizeof(IO_RW)) {
                st = HandleRWBatch((PIO_RW)buf);
                out = sizeof(IO_RW);
            }
            break;
            
        case IOCTL_BASE:
            if (inSize == sizeof(IO_BASE)) {
                st = HandleBase((PIO_BASE)buf);
                out = sizeof(IO_BASE);
            }
            break;
            
        case IOCTL_GUARD:
            if (inSize == sizeof(IO_GUARD)) {
                st = HandleGuard((PIO_GUARD)buf);
                out = sizeof(IO_GUARD);
            }
            break;
    }
    
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = out;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

NTSTATUS NoSupport(PDEVICE_OBJECT dev, PIRP irp) {
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS OkDispatch(PDEVICE_OBJECT dev, PIRP irp) {
    UNREFERENCED_PARAMETER(dev);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return irp->IoStatus.Status;
}

void Unload(PDRIVER_OBJECT drv) {
    IoDeleteSymbolicLink(&g_symLink);
    IoDeleteDevice(drv->DeviceObject);
}

NTSTATUS Init(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    PDEVICE_OBJECT dev = NULL;
    NTSTATUS st;
    int i;
    
    UNREFERENCED_PARAMETER(reg);
    
    RtlInitUnicodeString(&g_devName, L"\\Device\\xvnk_j4R2gW");
    RtlInitUnicodeString(&g_symLink, L"\\DosDevices\\xvnk_j4R2gW");
    
    st = IoCreateDevice(drv, 0, &g_devName, FILE_DEVICE_UNKNOWN, 
                        FILE_DEVICE_SECURE_OPEN, FALSE, &dev);
    if (!NT_SUCCESS(st)) return st;
    
    st = IoCreateSymbolicLink(&g_symLink, &g_devName);
    if (!NT_SUCCESS(st)) {
        IoDeleteDevice(dev);
        return st;
    }
    
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        drv->MajorFunction[i] = NoSupport;
    }
    
    drv->MajorFunction[IRP_MJ_CREATE] = OkDispatch;
    drv->MajorFunction[IRP_MJ_CLOSE] = OkDispatch;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoCtl;
    drv->DriverUnload = Unload;
    
    dev->Flags |= DO_BUFFERED_IO;
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    UNREFERENCED_PARAMETER(reg);
    return IoCreateDriver(NULL, Init);
}