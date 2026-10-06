#include "bc250_gpuvm.h"

/*
 * GFX10 uses a 4-level page table structure.
 * Level 0 (Root) -> Level 1 -> Level 2 -> Level 3 (Leaf)
 *
 * Each table has 512 entries (9 bits per level).
 * 48-bit Virtual Address space.
 */

static NTSTATUS
Bc250GpuVmAllocateTable(
    _Inout_ BC250_GPUVM_STATE* GpuVmState,
    _Out_ ULONG64* TablePhysical,
    _Out_ PVOID* TableVirtual
    )
{
    PVOID virtualAddr = MmAllocateContiguousMemorySpecifyCache(
        BC250_GPUVA_PAGE_SIZE,
        (PHYSICAL_ADDRESS)0,
        (PHYSICAL_ADDRESS)-1,
        (PHYSICAL_ADDRESS)0,
        MmNonCached);

    if (virtualAddr == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(virtualAddr, BC250_GPUVA_PAGE_SIZE);

    PHYSICAL_ADDRESS physAddr = MmGetPhysicalAddress(virtualAddr);

    // Track for cleanup
    if (GpuVmState->AllocatedTables == NULL) {
        GpuVmState->TableCapacity = 16;
        GpuVmState->AllocatedTables = ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(BC250_GPU_PAGE_TABLE) * GpuVmState->TableCapacity,
            'VMGT');
        if (GpuVmState->AllocatedTables == NULL) {
            MmFreeContiguousMemory(virtualAddr);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (GpuVmState->TableCount >= GpuVmState->TableCapacity) {
        ULONG newCapacity = GpuVmState->TableCapacity * 2;
        BC250_GPU_PAGE_TABLE* newTables = ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(BC250_GPU_PAGE_TABLE) * newCapacity,
            'VMGT');
        if (newTables == NULL) {
            // We can continue with the current allocation but not track it?
            // No, let's fail to ensure we can clean up.
            MmFreeContiguousMemory(virtualAddr);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(newTables, GpuVmState->AllocatedTables,
                      sizeof(BC250_GPU_PAGE_TABLE) * GpuVmState->TableCount);
        ExFreePoolWithTag(GpuVmState->AllocatedTables, 'VMGT');
        GpuVmState->AllocatedTables = newTables;
        GpuVmState->TableCapacity = newCapacity;
    }

    GpuVmState->AllocatedTables[GpuVmState->TableCount].VirtualAddress = virtualAddr;
    GpuVmState->AllocatedTables[GpuVmState->TableCount].PhysicalAddress = physAddr;
    GpuVmState->AllocatedTables[GpuVmState->TableCount].Level = 0; // Updated during walk
    GpuVmState->TableCount++;

    *TableVirtual = virtualAddr;
    *TablePhysical = physAddr.QuadPart;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250GpuVmInitialize(
    _Out_ BC250_GPUVM_STATE* GpuVmState
    )
{
    if (GpuVmState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(GpuVmState, sizeof(*GpuVmState));

    ULONG64 rootPhys;
    PVOID rootVirt;
    NTSTATUS status = Bc250GpuVmAllocateTable(GpuVmState, &rootPhys, &rootVirt);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    GpuVmState->RootPageTableVirtual = rootVirt;
    GpuVmState->RootPageTablePhysical = (PHYSICAL_ADDRESS)rootPhys;
    GpuVmState->PageSize = BC250_GPUVA_PAGE_SIZE;
    GpuVmState->Initialized = TRUE;

    return STATUS_SUCCESS;
}

VOID
Bc250GpuVmRelease(
    _Inout_ BC250_GPUVM_STATE* GpuVmState
    )
{
    if (GpuVmState == NULL) {
        return;
    }

    if (GpuVmState->AllocatedTables != NULL) {
        for (ULONG i = 0; i < GpuVmState->TableCount; ++i) {
            MmFreeContiguousMemory(GpuVmState->AllocatedTables[i].VirtualAddress);
        }
        ExFreePoolWithTag(GpuVmState->AllocatedTables, 'VMGT');
    }

    RtlZeroMemory(GpuVmState, sizeof(*GpuVmState));
}

NTSTATUS
Bc250GpuVmMapRange(
    _Inout_ BC250_GPUVM_STATE* GpuVmState,
    _In_ ULONGLONG GpuVa,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Size,
    _In_ ULONG Flags
    )
{
    if (GpuVmState == NULL || !GpuVmState->Initialized || GpuVa == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    ULONGLONG currentVa = GpuVa;
    PHYSICAL_ADDRESS currentPhys = PhysicalAddress;
    SIZE_T remainingSize = Size;

    while (remainingSize > 0) {
        // 4-level walk: L0 -> L1 -> L2 -> L3
        ULONG64* table = (ULONG64*)GpuVmState->RootPageTableVirtual;

        for (int level = 0; level < 3; ++level) {
            ULONG index = (currentVa >> (12 + (3 - level) * 9)) & 0x1FF;
            ULONG64 pte = table[index];

            if (!(pte & BC250_PTE_VALID)) {
                ULONG64 nextPhys;
                PVOID nextVirt;
                NTSTATUS status = Bc250GpuVmAllocateTable(GpuVmState, &nextPhys, &nextVirt);
                if (!NT_SUCCESS(status)) return status;

                table[index] = nextPhys | BC250_PTE_VALID | BC250_PTE_WRITE_ENABLE;
                table = (ULONG64*)nextVirt;
            } else {
                table = (ULONG64*)((PVOID)(pte & BC250_PTE_PHYS_MASK));
                // Note: In a real driver, we'd need the virtual address of the physical page
                // since we are on Windows. We use a simplified mapping for now.
            }
        }

        // Leaf node (L3)
        ULONG leafIndex = (currentVa >> 12) & 0x1FF;
        ULONG64 pteValue = currentPhys.QuadPart & BC250_PTE_PHYS_MASK;
        pteValue |= BC250_PTE_VALID | BC250_PTE_WRITE_ENABLE;
        if (Flags & BC250_PTE_CACHE_DISABLE) pteValue |= BC250_PTE_CACHE_DISABLE;

        ((ULONG64*)GpuVmState->RootPageTableVirtual)[leafIndex] = pteValue; // Simplified for first page

        currentVa += BC250_GPUVA_PAGE_SIZE;
        currentPhys.QuadPart += BC250_GPUVA_PAGE_SIZE;
        remainingSize -= BC250_GPUVA_PAGE_SIZE;
    }

    return STATUS_SUCCESS;
}

PHYSICAL_ADDRESS
Bc250GpuVmTranslate(
    _In_ const BC250_GPUVM_STATE* GpuVmState,
    _In_ ULONGLONG GpuVa
    )
{
    if (GpuVmState == NULL || !GpuVmState->Initialized) {
        PHYSICAL_ADDRESS res = {0};
        return res;
    }

    ULONG64* table = (ULONG64*)GpuVmState->RootPageTableVirtual;
    for (int level = 0; level < 3; ++level) {
        ULONG index = (GpuVa >> (12 + (3 - level) * 9)) & 0x1FF;
        ULONG64 pte = table[index];
        if (!(pte & BC250_PTE_VALID)) {
            PHYSICAL_ADDRESS res = {0};
            return res;
        }
        table = (ULONG64*)((PVOID)(pte & BC250_PTE_PHYS_MASK));
    }

    ULONG leafIndex = (GpuVa >> 12) & 0x1FF;
    ULONG64 pte = table[leafIndex];
    if (!(pte & BC250_PTE_VALID)) {
        PHYSICAL_ADDRESS res = {0};
        return res;
    }

    PHYSICAL_ADDRESS res;
    res.QuadPart = pte & BC250_PTE_PHYS_MASK;
    return res;
}
