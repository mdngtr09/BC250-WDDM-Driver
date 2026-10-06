#include <dispmprt.h>
#include "bc250_memory.h"

/*
 * UMA Heap Management Implementation
 *
 * For the BC-250 (Cyan Skillfish), the "Local" memory is actually a carved-out
 * portion of system RAM. We implement a simplified heap manager that
 * tracks this carved-out range and provides allocation for KMD-internal
 * objects (like Rings and Fences).
 */

typedef struct _BC250_UMA_HEAP {
    PHYSICAL_ADDRESS BaseAddress;
    SIZE_T TotalSize;
    SIZE_T AllocatedSize;
    PVOID CpuVirtualAddress;
    BOOLEAN IsInitialized;
} BC250_UMA_HEAP;

static BC250_UMA_HEAP g_UmaHeap = {0};

static SIZE_T
Bc250AlignDownSize(
    _In_ SIZE_T Value,
    _In_ SIZE_T Alignment
    )
{
    if (Alignment == 0) {
        return Value;
    }
    return Value - (Value % Alignment);
}

static SIZE_T
Bc250MinSize(
    _In_ SIZE_T Left,
    _In_ SIZE_T Right
    )
{
    return Left < Right ? Left : Right;
}

NTSTATUS
Bc250UmaHeapInitialize(
    _In_ BC250_MEMORY_STATE* MemoryState
    )
{
    if (MemoryState == NULL || !MemoryState->ReadyForVidMm) {
        return STATUS_INVALID_PARAMETER;
    }

    // Find the Local UMA segment
    BC250_MEMORY_SEGMENT* localSeg = NULL;
    for (ULONG i = 0; i < MemoryState->SegmentCount; ++i) {
        if (MemoryState->Segments[i].Kind == Bc250MemoryLocalUma) {
            localSeg = &MemoryState->Segments[i];
            break;
        }
    }

    if (localSeg == NULL) {
        return STATUS_NOT_SUPPORTED; // No local carve-out found
    }

    g_UmaHeap.BaseAddress = localSeg->BaseAddress;
    g_UmaHeap.TotalSize = localSeg->Size;
    g_UmaHeap.AllocatedSize = 0;

    // Map the carve-out into kernel space for CPU access (if CPU visible)
    if (localSeg->CpuVisible) {
        g_UmaHeap.CpuVirtualAddress = MmMapIoSpace(
            localSeg->BaseAddress,
            localSeg->Size,
            MmNonCached);

        if (g_UmaHeap.CpuVirtualAddress == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    g_UmaHeap.IsInitialized = TRUE;
    return STATUS_SUCCESS;
}

PHYSICAL_ADDRESS
Bc250UmaHeapAllocate(
    _In_ SIZE_T Size,
    _Out_ PVOID* OutCpuAddress
    )
{
    if (!g_UmaHeap.IsInitialized || OutCpuAddress == NULL) {
        return (PHYSICAL_ADDRESS){0};
    }

    // Align size to page boundary
    SIZE_T alignedSize = (Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (g_UmaHeap.AllocatedSize + alignedSize > g_UmaHeap.TotalSize) {
        return (PHYSICAL_ADDRESS){0};
    }

    PHYSICAL_ADDRESS allocPhys;
    allocPhys.QuadPart = g_UmaHeap.BaseAddress.QuadPart + g_UmaHeap.AllocatedSize;

    if (OutCpuAddress != NULL && g_UmaHeap.CpuVirtualAddress != NULL) {
        *OutCpuAddress = (PVOID)((ULONG_PTR)g_UmaHeap.CpuVirtualAddress + g_UmaHeap.AllocatedSize);
    }

    g_UmaHeap.AllocatedSize += alignedSize;
    return allocPhys;
}

VOID
Bc250UmaHeapRelease(
    VOID
    )
{
    if (g_UmaHeap.IsInitialized) {
        if (g_UmaHeap.CpuVirtualAddress != NULL) {
            MmUnmapIoSpace(g_UmaHeap.CpuVirtualAddress, g_UmaHeap.TotalSize);
        }
        RtlZeroMemory(&g_UmaHeap, sizeof(BC250_UMA_HEAP));
    }
}

NTSTATUS
Bc250BuildUmaProfile(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const BC250_UMA_DISCOVERY_INPUT* Input
    )
{
    SIZE_T pageSize;
    SIZE_T systemBytes;
    SIZE_T reservedBytes = 0;
    SIZE_T visibleBytes = 0;
    SIZE_T apertureBytes;
    SIZE_T maxGpuBytes;
    ULONG segmentIndex = 0;

    if (MemoryState == NULL || Input == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(MemoryState, sizeof(*MemoryState));
    MemoryState->Uma = TRUE;
    MemoryState->GpuVaBits = 48;

    pageSize = (Input->PageSize == 65536u || Input->PageSize == 4096u)
        ? Input->PageSize
        : 4096u;
    MemoryState->PageSize = (ULONG)pageSize;

    if (!Input->SystemRamKnown || Input->SystemRamBytes == 0) {
        MemoryState->Conservative = TRUE;
        return STATUS_DEVICE_NOT_READY;
    }

    systemBytes = Bc250AlignDownSize(Input->SystemRamBytes, pageSize);
    MemoryState->TotalSystemBytes = systemBytes;

    if (Input->FirmwareReservationKnown &&
        Input->FirmwareReservedBytes != 0 &&
        Input->FirmwareReservedBytes <= systemBytes) {
        reservedBytes = Bc250AlignDownSize(Input->FirmwareReservedBytes, pageSize);
    }
    MemoryState->FirmwareReservedBytes = reservedBytes;

    if (Input->GpuVisibleRangeKnown && Input->GpuVisibleBytes != 0) {
        visibleBytes = Bc250AlignDownSize(Input->GpuVisibleBytes, pageSize);
        maxGpuBytes = systemBytes;
        if (reservedBytes != 0) {
            maxGpuBytes = Bc250MinSize(maxGpuBytes, reservedBytes);
        }
        visibleBytes = Bc250MinSize(visibleBytes, maxGpuBytes);
    }

    if (visibleBytes != 0 && segmentIndex < RTL_NUMBER_OF(MemoryState->Segments)) {
        MemoryState->Segments[segmentIndex].Kind = Bc250MemoryLocalUma;
        MemoryState->Segments[segmentIndex].Source =
            Input->FirmwareReservationKnown
                ? Bc250MemorySourceFirmware
                : Bc250MemorySourceRuntimeQuery;
        MemoryState->Segments[segmentIndex].BaseAddress.QuadPart =
            (LONGLONG)Input->GpuVisibleBase;
        MemoryState->Segments[segmentIndex].Size = visibleBytes;
        MemoryState->Segments[segmentIndex].CpuVisibleSize =
            Input->GpuVisibleCpuAccessible ? visibleBytes : 0;
        MemoryState->Segments[segmentIndex].CpuVisible =
            Input->GpuVisibleCpuAccessible;
        MemoryState->Segments[segmentIndex].GpuVisible = TRUE;
        MemoryState->Segments[segmentIndex].IsAperture = FALSE;
        ++segmentIndex;
    }

    apertureBytes = Input->CpuHostApertureBytes != 0
        ? Input->CpuHostApertureBytes
        : Input->ApertureBytes;
    apertureBytes = Bc250AlignDownSize(apertureBytes, pageSize);
    if (apertureBytes == 0) {
        MemoryState->Conservative = TRUE;
        return STATUS_DEVICE_NOT_READY;
    }

    if (segmentIndex >= RTL_NUMBER_OF(MemoryState->Segments)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    MemoryState->Segments[segmentIndex].Kind = Bc250MemoryGart;
    MemoryState->Segments[segmentIndex].Source = Bc250MemorySourceRuntimeQuery;
    MemoryState->Segments[segmentIndex].BaseAddress.QuadPart = 0;
    MemoryState->Segments[segmentIndex].Size = apertureBytes;
    MemoryState->Segments[segmentIndex].CpuVisibleSize = 0;
    MemoryState->Segments[segmentIndex].CpuVisible = FALSE;
    MemoryState->Segments[segmentIndex].GpuVisible = TRUE;
    MemoryState->Segments[segmentIndex].IsAperture = TRUE;
    ++segmentIndex;

    MemoryState->SegmentCount = segmentIndex;
    MemoryState->ApertureBytes = apertureBytes;
    MemoryState->EffectiveGpuBytes = visibleBytes != 0
        ? visibleBytes
        : apertureBytes;
    MemoryState->Conservative = !(Input->FirmwareReservationKnown &&
                                  Input->GpuVisibleRangeKnown);
    MemoryState->ReadyForVidMm = TRUE;

    UNREFERENCED_PARAMETER(Input->FirmwareReservedBase);
    UNREFERENCED_PARAMETER(Input->FirmwareReservedByBios);
    UNREFERENCED_PARAMETER(Input->IsSkillfish2);

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250InitializeMemoryState(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const DXGK_DEVICE_INFO* DeviceInfo
    )
{
    BC250_UMA_DISCOVERY_INPUT input;

    if (MemoryState == NULL || DeviceInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&input, sizeof(input));
    input.SystemRamKnown = TRUE;
    input.SystemRamBytes = (SIZE_T)DeviceInfo->SystemMemorySize.QuadPart;
    input.PageSize = 4096u;

    input.ApertureBytes = Bc250MinSize(input.SystemRamBytes, 0x100000000u);
    input.FirmwareReservationKnown = FALSE;
    input.GpuVisibleRangeKnown = FALSE;
    input.GpuVisibleCpuAccessible = FALSE;

    return Bc250BuildUmaProfile(MemoryState, &input);
}
