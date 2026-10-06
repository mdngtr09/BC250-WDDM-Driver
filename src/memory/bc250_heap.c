#include "bc250_heap.h"
#include "bc250_memory.h"

/*
 * BC-250 Advanced Heap Management Implementation.
 *
 * This module handles the allocation of buffers across different memory
 * segments (Local, Aperture, System), ensuring the GPU uses the fastest
 * available memory for a given request.
 */

NTSTATUS
Bc250HeapInitialize(
    _Inout_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ const BC250_MEMORY_STATE* MemoryState
    )
{
    if (HeapMgr == NULL || MemoryState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(HeapMgr, sizeof(*HeapMgr));

    // 1. Initialize Local Heap (Carve-out)
    for (ULONG i = 0; i < MemoryState->SegmentCount; ++i) {
        if (MemoryState->Segments[i].Kind == Bc250MemoryLocalUma) {
            HeapMgr->Heaps[Bc250HeapLocal].Kind = Bc250HeapLocal;
            HeapMgr->Heaps[Bc250HeapLocal].BaseAddress = MemoryState->Segments[i].BaseAddress;
            HeapMgr->Heaps[Bc250HeapLocal].TotalSize = MemoryState->Segments[i].Size;
            HeapMgr->Heaps[Bc250HeapLocal].IsCpuVisible = MemoryState->Segments[i].CpuVisible;
            HeapMgr->Heaps[Bc250HeapLocal].IsGpuVisible = TRUE;
            HeapMgr->ActiveHeapCount++;
        }
    }

    // 2. Initialize Aperture Heap
    if (MemoryState->ApertureBytes > 0) {
        HeapMgr->Heaps[Bc250HeapAperture].Kind = Bc250HeapAperture;
        // Aperture is usually a window into system RAM, BaseAddress is the BAR offset
        HeapMgr->Heaps[Bc250HeapAperture].BaseAddress.QuadPart = 0;
        HeapMgr->Heaps[Bc250HeapAperture].TotalSize = MemoryState->ApertureBytes;
        HeapMgr->Heaps[Bc250HeapAperture].IsCpuVisible = TRUE;
        HeapMgr->Heaps[Bc250HeapAperture].IsGpuVisible = TRUE;
        HeapMgr->ActiveHeapCount++;
    }

    // 3. System Heap (Implicitly all available system RAM)
    HeapMgr->Heaps[Bc250HeapSystem].Kind = Bc250HeapSystem;
    HeapMgr->Heaps[Bc250HeapSystem].BaseAddress.QuadPart = 0;
    HeapMgr->Heaps[Bc250HeapSystem].TotalSize = MemoryState->TotalSystemBytes;
    HeapMgr->Heaps[Bc250HeapSystem].IsCpuVisible = TRUE;
    HeapMgr->Heaps[Bc250HeapSystem].IsGpuVisible = FALSE; // Needs GPUVM mapping
    HeapMgr->ActiveHeapCount++;

    HeapMgr->Initialized = TRUE;
    return STATUS_SUCCESS;
}

PHYSICAL_ADDRESS
Bc250HeapAllocate(
    _Inout_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ SIZE_T Size,
    _In_ BC250_MEMORY_HEAP_KIND PreferredKind,
    _Out_ BC250_MEMORY_HEAP_KIND* ActualKind
    )
{
    if (HeapMgr == NULL || !HeapMgr->Initialized) {
        return (PHYSICAL_ADDRESS){0};
    }

    // Align size to page
    SIZE_T alignedSize = (Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Tiered allocation strategy: Preferred -> Local -> Aperture -> System
    BC250_MEMORY_HEAP_KIND candidates[3] = { PreferredKind, Bc250HeapLocal, Bc250HeapAperture };

    for (int i = 0; i < 3; ++i) {
        BC250_MEMORY_HEAP_KIND kind = candidates[i];
        if (kind >= Bc250HeapCount) continue;

        BC250_MEMORY_HEAP* heap = &HeapMgr->Heaps[kind];
        if (heap->TotalSize > 0 && (heap->UsedSize + alignedSize <= heap->TotalSize)) {
            PHYSICAL_ADDRESS addr;
            addr.QuadPart = heap->BaseAddress.QuadPart + heap->UsedSize;

            heap->UsedSize += alignedSize;
            if (ActualKind) *ActualKind = kind;
            return addr;
        }
    }

    return (PHYSICAL_ADDRESS){0}; // Out of memory in all suitable heaps
}

VOID
Bc250HeapRelease(
    _In_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ PHYSICAL_ADDRESS Address,
    _In_ SIZE_T Size
    )
{
    if (HeapMgr == NULL || !HeapMgr->Initialized) return;

    // Simplified release: for a linear allocator, we only support full heap reset
    // In a production driver, we would use a free-list or buddy allocator here.
}
