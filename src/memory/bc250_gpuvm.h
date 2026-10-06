#pragma once

#include <ntddk.h>
#include "bc250_vidmm.h"

/*
 * GFX10 GPU Virtual Address (GPUVA) Constants.
 * The BC-250 uses a 4-level page table structure.
 */
#define BC250_GPUVA_BITS            48u
#define BC250_GPUVA_PAGE_SIZE      4096u
#define BC250_GPUVA_LEVELS          4u

/*
 * Page Table Entry (PTE) bitfields for GFX10.
 * These are simplified representations for the bring-up phase.
 */
#define BC250_PTE_VALID             (1uLL << 0)
#define BC250_PTE_CACHE_DISABLE   (1uLL << 1)
#define BC250_PTE_WRITE_ENABLE     (1uLL << 2)
#define BC250_PTE_USER_MODE        (1uLL << 3)
#define BC250_PTE_PHYS_MASK       0x00000000FFFFFFFF000ULL

typedef struct _BC250_GPU_PAGE_TABLE {
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Level;
} BC250_GPU_PAGE_TABLE;

typedef struct _BC250_GPUVM_STATE {
    PVOID RootPageTableVirtual;
    PHYSICAL_ADDRESS RootPageTablePhysical;
    ULONG PageSize;
    BOOLEAN Initialized;

    // Tracking allocated tables for cleanup
    BC250_GPU_PAGE_TABLE* AllocatedTables;
    ULONG TableCount;
    ULONG TableCapacity;
} BC250_GPUVM_STATE;

NTSTATUS
Bc250GpuVmInitialize(
    _Out_ BC250_GPUVM_STATE* GpuVmState
    );

VOID
Bc250GpuVmRelease(
    _Inout_ BC250_GPUVM_STATE* GpuVmState
    );

NTSTATUS
Bc250GpuVmMapRange(
    _Inout_ BC250_GPUVM_STATE* GpuVmState,
    _In_ ULONGLONG GpuVa,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Size,
    _In_ ULONG Flags
    );

PHYSICAL_ADDRESS
Bc250GpuVmTranslate(
    _In_ const BC250_GPUVM_STATE* GpuVmState,
    _In_ ULONGLONG GpuVa
    );

#ifdef __cplusplus
}
#endif
