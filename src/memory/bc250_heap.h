#pragma once

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _BC250_MEMORY_HEAP_KIND {
    Bc250HeapLocal = 0,      // Carve-out rápido (GPU Visible)
    Bc250HeapAperture,       // Janela de abertura (CPU $\rightarrow$ GPU)
    Bc250HeapSystem,         // RAM do sistema (CPU Visible)
    Bc250HeapCount
} BC250_MEMORY_HEAP_KIND;

typedef struct _BC250_MEMORY_HEAP {
    BC250_MEMORY_HEAP_KIND Kind;
    PHYSICAL_ADDRESS BaseAddress;
    SIZE_T TotalSize;
    SIZE_T UsedSize;
    BOOLEAN IsCpuVisible;
    BOOLEAN IsGpuVisible;
} BC250_MEMORY_HEAP;

typedef struct _BC250_HEAP_MANAGER {
    BC250_MEMORY_HEAP Heaps[BC250HeapCount];
    ULONG ActiveHeapCount;
    BOOLEAN Initialized;
} BC250_HEAP_MANAGER;

NTSTATUS
Bc250HeapInitialize(
    _Inout_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ const struct _BC250_MEMORY_STATE* MemoryState
    );

PHYSICAL_ADDRESS
Bc250HeapAllocate(
    _Inout_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ SIZE_T Size,
    _In_ BC250_MEMORY_HEAP_KIND PreferredKind,
    _Out_ BC250_MEMORY_HEAP_KIND* ActualKind
    );

VOID
Bc250HeapRelease(
    _In_ BC250_HEAP_MANAGER* HeapMgr,
    _In_ PHYSICAL_ADDRESS Address,
    _In_ SIZE_T Size
    );

#ifdef __cplusplus
}
#endif
