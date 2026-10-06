#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <map>

/*
 * BC-250 PM4 (Packet Memory 4) Command Definitions.
 * Updated based on Mesa 26.1 / GFX10 ISA analysis.
 */
enum class Bc250Pm4Packet : uint32_t {
    // Packet Header: 0xC0000000 | (PacketId << 8) | PayloadSize
    WRITE_DATA      = 0x37,
    INDIRECT_BUFFER = 0x3F,
    SET_STATE       = 0x10,
    S_WAIT_ON_FENCE = 0x50,
    S_SIGNAL_FENCE  = 0x51,
    // Rendering Packets (Updated for BC-250)
    SET_RENDER_TARGET = 0x42,
    SET_PIPELINE_STATE = 0x45,
    DRAW_INDEXED       = 0x48,
    S_CLEAR_COLOR      = 0x60,
    S_SET_SCISSOR      = 0x62,
    S_SET_VIEWPORT     = 0x63
};

inline uint32_t MakePm4Header(Bc250Pm4Packet packet, uint32_t payloadSize) {
    return 0xC0000000u | ((uint32_t)packet << 8) | payloadSize;
}

namespace Bc250Umd {

    struct UmdDevice {
        HMODULE DriverModule;
        HANDLE HAdapter;
        bool IsInitialized;
        bool TraceEnabled;

        UmdDevice() : DriverModule(nullptr), HAdapter(nullptr), IsInitialized(false), TraceEnabled(true) {}
        ~UmdDevice() {}
    };

    class Bc250UmdContext {
    public:
        Bc250UmdContext(UmdDevice* device) : m_device(device) {}
        virtual ~Bc250UmdContext() {}

        virtual HRESULT CreateCommandBuffer(ID3D12CommandList* pList, void** ppBuffer) = 0;
        virtual HRESULT CloseCommandBuffer(void* pBuffer) = 0;
        virtual HRESULT SubmitCommandBuffer(void* pBuffer, UINT64 fenceValue) = 0;

        virtual void EmitPm4Packet(void* pBuffer, uint32_t* cursor, Bc250Pm4Packet packet, const void* data, uint32_t size) {
            *cursor = MakePm4Header(packet, size);
            if (data && size > 0) {
                memcpy(&(*cursor + 1), data, size);
                *cursor += (size + 3) / 4;
            }
            (*cursor)++;
        }

    private:
        UmdDevice* m_device;
    };

    class UmdFactory {
    public:
        static std::unique_ptr<UmdDevice> CreateDevice();
        static std::unique_ptr<Bc250UmdContext> CreateContext(UmdDevice* device);
    };

} // namespace Bc250Umd
