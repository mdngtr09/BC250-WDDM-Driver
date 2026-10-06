#include "bc250_hw.h"

static const USHORT g_Bc250DeviceIds[] = {
    BC250_DEVICE_ID_13DB,
    BC250_DEVICE_ID_13F9,
    BC250_DEVICE_ID_13FA,
    BC250_DEVICE_ID_13FB,
    BC250_DEVICE_ID_13FC,
    BC250_DEVICE_ID_13FE,
    BC250_DEVICE_ID_143F,
};

BOOLEAN
Bc250IsSupportedDevice(
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId
    )
{
    SIZE_T index;

    if (VendorId != BC250_VENDOR_ID) {
        return FALSE;
    }

    for (index = 0; index < RTL_NUMBER_OF(g_Bc250DeviceIds); ++index) {
        if (g_Bc250DeviceIds[index] == DeviceId) {
            return TRUE;
        }
    }

    return FALSE;
}

VOID
Bc250InitializeHwState(
    _Out_ BC250_HW_STATE* HwState,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _In_ UCHAR RevisionId
    )
{
    RtlZeroMemory(HwState, sizeof(*HwState));

    HwState->VendorId = VendorId;
    HwState->DeviceId = DeviceId;
    HwState->RevisionId = RevisionId;
    HwState->IsCyanSkillfish = Bc250IsSupportedDevice(VendorId, DeviceId);
    HwState->Variant = (DeviceId == BC250_DEVICE_ID_13FE ||
                        DeviceId == BC250_DEVICE_ID_143F)
        ? Bc250VariantCyanSkillfish2
        : (HwState->IsCyanSkillfish
            ? Bc250VariantCyanSkillfish
            : Bc250VariantUnknown);
    HwState->GfxIpVersion = (BC250_GFX_MAJOR << 16) |
                            (BC250_GFX_MINOR << 8) |
                            BC250_GFX_REVISION;
    HwState->SdmaIpVersion = (BC250_SDMA_MAJOR << 8) | BC250_SDMA_MINOR;
    HwState->SdmaInstances = 2;
}

NTSTATUS
Bc250MapMmioBar(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG BarIndex,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Length
    )
{
    PVOID virtualAddress;

    if (HwState == NULL || BarIndex >= BC250_MAX_BARS || Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (HwState->Bars[BarIndex].Mapped) {
        return STATUS_DEVICE_BUSY;
    }

    virtualAddress = MmMapIoSpaceEx(
        PhysicalAddress,
        Length,
        PAGE_READWRITE | PAGE_NOCACHE);

    if (virtualAddress == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    HwState->Bars[BarIndex].PhysicalAddress = PhysicalAddress;
    HwState->Bars[BarIndex].Length = Length;
    HwState->Bars[BarIndex].VirtualAddress = virtualAddress;
    HwState->Bars[BarIndex].Mapped = TRUE;
    ++HwState->MappedBarCount;

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250MapTranslatedResources(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ PCM_RESOURCE_LIST ResourceList
    )
{
    ULONG resourceIndex;
    ULONG barIndex = 0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR resource;
    PCM_PARTIAL_RESOURCE_LIST partialList;

    if (HwState == NULL || ResourceList == NULL || ResourceList->Count == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    partialList = &ResourceList->List[0].PartialResourceList;

    for (resourceIndex = 0;
         resourceIndex < partialList->Count && barIndex < BC250_MAX_BARS;
         ++resourceIndex) {
        resource = &partialList->PartialDescriptors[resourceIndex];

        if (resource->Type != CmResourceTypeMemory ||
            resource->u.Memory.Length == 0) {
            continue;
        }

        NTSTATUS status = Bc250MapMmioBar(
            HwState,
            barIndex,
            resource->u.Memory.Start,
            resource->u.Memory.Length);

        if (!NT_SUCCESS(status)) {
            Bc250UnmapMmioBars(HwState);
            return status;
        }

        ++barIndex;
    }

    return barIndex != 0 ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

VOID
Bc250UnmapMmioBars(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    ULONG index;

    if (HwState == NULL) {
        return;
    }

    for (index = 0; index < BC250_MAX_BARS; ++index) {
        if (HwState->Bars[index].Mapped) {
            MmUnmapIoSpace(
                HwState->Bars[index].VirtualAddress,
                HwState->Bars[index].Length);
            RtlZeroMemory(&HwState->Bars[index], sizeof(HwState->Bars[index]));
            if (HwState->MappedBarCount != 0) {
                --HwState->MappedBarCount;
            }
        }
    }
}


NTSTATUS
Bc250HwProbeSmn(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    ULONG index;
    ULONG value;

    if (HwState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (index = 0; index < BC250_MAX_BARS; ++index) {
        if (!HwState->Bars[index].Mapped) {
            continue;
        }

        /* Attempt to read SMU signature from this BAR */
        volatile ULONG* indexReg = (volatile ULONG*)((PUCHAR)HwState->Bars[index].VirtualAddress + BC250_NBIO_SMN_INDEX_OFFSET);
        volatile ULONG* dataReg = (volatile ULONG*)((PUCHAR)HwState->Bars[index].VirtualAddress + BC250_NBIO_SMN_DATA_OFFSET);

        if (indexReg == NULL || dataReg == NULL) continue;

        WRITE_REGISTER_ULONG(indexReg, BC250_SMU_SIGNATURE_REG);
        KeMemoryBarrier();
        value = READ_REGISTER_ULONG(dataReg);

        if (value == BC250_SMU_SIGNATURE_VALUE) {
            DbgPrint("BC250: SMU Signature found on BAR[%u]!\n", index);
            HwState->SmnBarIndex = index;
            return STATUS_SUCCESS;
        }
    }

    DbgPrint("BC250: SMU Signature probe failed on all BARs.\n");
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
Bc250HwInitializeSequence(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    NTSTATUS status;

    if (HwState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    DbgPrint("BC250: Starting Hardware Initialization Sequence...\n");

    /* 1. Discover BARs and resource layout */
    status = Bc250DiscoverRuntimeHardware(HwState);
    if (!NT_SUCCESS(status)) {
        DbgPrint("BC250: Init failed at Discovery: 0x%08X\n", status);
        return status;
    }

    /* 2. Validate SMN (System Management Network) Access */
    status = Bc250HwProbeSmn(HwState);
    if (!NT_SUCCESS(status)) {
        DbgPrint("BC250: Init failed at SMN Probe: 0x%08X\n", status);
        return status;
    }

    /* 3. Validate PSP (Platform Security Processor) Liveliness */
    status = Bc250HwProbePsp(HwState);
    if (!NT_SUCCESS(status)) {
        DbgPrint("BC250: Init failed at PSP Probe: 0x%08X\n", status);
        return status;
    }

    /*
     * If we reached here, the PCIe -> BAR -> Register path is functional.
     * We can now safely allow writes to the device registers.
     */
    HwState->AllowRegisterWrites = TRUE;
    HwState->IsStarted = TRUE;
    HwState->RuntimeDiscovered = TRUE;

    DbgPrint("BC250: Hardware Initialization Sequence COMPLETED successfully.\n");
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DiscoverRuntimeHardware(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    ULONG index;
    ULONG candidateCount = 0;

    if (HwState == NULL || !HwState->IsCyanSkillfish) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    /*
     * BAR ordering is not assumed. The first sufficiently large memory BAR is
     * retained as a candidate register aperture; BAR5/PSP offsets are only
     * used after the runtime resource parser confirms their range.
     */
    HwState->RegisterBarIndex = MAXULONG;
    HwState->SmnBarIndex = MAXULONG;
    HwState->PspBarIndex = MAXULONG;
    for (index = 0; index < BC250_MAX_BARS; ++index) {
        ULONGLONG physicalAddress;

        if (!HwState->Bars[index].Mapped) {
            continue;
        }

        physicalAddress = (ULONGLONG)HwState->Bars[index].PhysicalAddress.QuadPart;
        if (physicalAddress == BC250_EXPECTED_BAR5_PHYSICAL &&
            HwState->Bars[index].Length >= BC250_EXPECTED_BAR5_LENGTH) {
            HwState->SmnBarIndex = index;
        }

        if (HwState->Bars[index].Length >= 0x100000u &&
            physicalAddress != BC250_EXPECTED_BAR5_PHYSICAL &&
            HwState->RegisterBarIndex == MAXULONG) {
            HwState->RegisterBarIndex = index;
        }

        if (HwState->Bars[index].Length >= 0x60000u &&
            HwState->PspBarIndex == MAXULONG) {
            HwState->PspBarIndex = index;
        }

        ++candidateCount;
    }

    /*
     * The resource parser cannot infer the original PCI BAR number from a
     * generic CM_RESOURCE_LIST. Refuse SMN access unless the known BAR5
     * physical range is confirmed, avoiding writes into an arbitrary BAR.
     */
    if (candidateCount == 0 ||
        HwState->RegisterBarIndex == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Probe for SMN BAR using hardware signature instead of fixed physical address */
    NTSTATUS status = Bc250HwProbeSmn(HwState);
    if (!NT_SUCCESS(status)) {
        DbgPrint("BC250: Failed to probe SMN BAR. Falling back to candidate search.\n");
        for (index = 0; index < BC250_MAX_BARS; ++index) {
            if (HwState->Bars[index].Mapped &&
                HwState->Bars[index].PhysicalAddress.QuadPart == BC250_EXPECTED_BAR5_PHYSICAL) {
                HwState->SmnBarIndex = index;
                break;
            }
        }
    }

    if (HwState->SmnBarIndex == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }

    HwState->RuntimeDiscovered = TRUE;
    HwState->FirmwareReady = FALSE;
    HwState->PspReady = FALSE;
    HwState->SmuReady = FALSE;
    HwState->GfxReady = FALSE;
    HwState->VmReady = FALSE;
    HwState->AllowRegisterWrites = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250HwDumpBar(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG BarIndex,
    _In_ ULONG StartOffset,
    _In_ ULONG Length
    )
{
    ULONG index;
    ULONG value;
    PVOID base;

    if (HwState == NULL || BarIndex >= BC250_MAX_BARS || !HwState->Bars[BarIndex].Mapped) {
        return STATUS_INVALID_PARAMETER;
    }

    base = HwState->Bars[BarIndex].VirtualAddress;

    DbgPrint("BC250: Dumping BAR[%u] from offset 0x%X (Length: 0x%X)\n",
             BarIndex, StartOffset, Length);

    for (index = 0; index < Length; index += sizeof(ULONG)) {
        value = READ_REGISTER_ULONG((volatile ULONG*)((PUCHAR)base + StartOffset + index));
        DbgPrint("BC250: [BAR%u] 0x%08X: 0x%08X\n", BarIndex, StartOffset + index, value);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250HwProbePsp(
    _In_ const BC250_HW_STATE* HwState
    )
{
    ULONG value;
    ULONG pspBar;

    if (HwState == NULL || HwState->PspBarIndex >= BC250_MAX_BARS) {
        return STATUS_DEVICE_NOT_READY;
    }

    pspBar = HwState->PspBarIndex;
    DbgPrint("BC250: Probing PSP on BAR[%u]\n", pspBar);

    /*
     * Test 1: Mailbox Mode (Liveliness Test)
     * The Mailbox is the simplest way to check if the BAR is mapped and responding.
     */
    value = READ_REGISTER_ULONG((volatile ULONG*)((PUCHAR)
        HwState->Bars[pspBar].VirtualAddress + BC250_PSP_MAILBOX_OFFSET));

    DbgPrint("BC250: PSP Mailbox (0x%X) Read Value: 0x%08X\n",
             BC250_PSP_MAILBOX_OFFSET, value);

    if (value != 0 && value != 0xFFFFFFFF) {
        DbgPrint("BC250: PSP Mailbox probe SUCCESS: Hardware responded.\n");
        return STATUS_SUCCESS;
    }

    /*
     * Test 2: Ring Mode (GPCOM)
     * Only likely to respond if firmware is already initialized.
     */
    DbgPrint("BC250: Mailbox failed, trying Ring Mode at 0x%X\n", BC250_PSP_C2PMSG_64_OFFSET);
    value = READ_REGISTER_ULONG((volatile ULONG*)((PUCHAR)
        HwState->Bars[pspBar].VirtualAddress + BC250_PSP_C2PMSG_64_OFFSET));

    DbgPrint("BC250: PSP C2PMSG_64 Read Value: 0x%08X\n", value);

    if (value != 0 && value != 0xFFFFFFFF) {
        DbgPrint("BC250: PSP Ring probe SUCCESS: Hardware responded.\n");
        return STATUS_SUCCESS;
    }

    DbgPrint("BC250: PSP Probe failed on all modes: No response or invalid BAR.\n");
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
Bc250ReadRegister32(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
    )
{
    volatile ULONG* registerAddress;

    if (HwState == NULL || Value == NULL ||
        HwState->RegisterBarIndex >= BC250_MAX_BARS ||
        !HwState->Bars[HwState->RegisterBarIndex].Mapped ||
        HwState->Bars[HwState->RegisterBarIndex].Length < sizeof(ULONG) ||
        Offset > HwState->Bars[HwState->RegisterBarIndex].Length - sizeof(ULONG)) {
        return STATUS_INVALID_PARAMETER;
    }

    registerAddress = (volatile ULONG*)((PUCHAR)
        HwState->Bars[HwState->RegisterBarIndex].VirtualAddress + Offset);
    *Value = READ_REGISTER_ULONG(registerAddress);
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250WriteRegister32(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    volatile ULONG* registerAddress;

    if (HwState == NULL ||
        HwState->RegisterBarIndex >= BC250_MAX_BARS ||
        !HwState->Bars[HwState->RegisterBarIndex].Mapped ||
        HwState->Bars[HwState->RegisterBarIndex].Length < sizeof(ULONG) ||
        Offset > HwState->Bars[HwState->RegisterBarIndex].Length - sizeof(ULONG)) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HwState->AllowRegisterWrites) {
        return STATUS_DEVICE_NOT_READY;
    }

    registerAddress = (volatile ULONG*)((PUCHAR)
        HwState->Bars[HwState->RegisterBarIndex].VirtualAddress + Offset);
    WRITE_REGISTER_ULONG(registerAddress, Value);
    return STATUS_SUCCESS;
}
