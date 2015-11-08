/**
 * (c) 2015 Alexandro Sanchez Bach. All rights reserved.
 * Released under GPL v2 license. Read LICENSE for more details.
 */

#include "direct3d12_command_buffer.h"
#include "nucleus/logger/logger.h"
#include "nucleus/graphics/backend/direct3d12/direct3d12_target.h"

namespace gfx {

Direct3D12CommandBuffer::Direct3D12CommandBuffer() {
}

Direct3D12CommandBuffer::~Direct3D12CommandBuffer() {
}

bool Direct3D12CommandBuffer::reset() {
    return true;
}

void Direct3D12CommandBuffer::cmdBindPipeline(IPipelineState* pipeline) {
}

void Direct3D12CommandBuffer::cmdClearColor(IColorTarget* target, const F32* colorValue) {
    auto d3dTarget = static_cast<Direct3D12ColorTarget*>(target);
    if (!d3dTarget) {
        logger.error(LOG_GRAPHICS, "Direct3D12CommandBuffer::cmdClearColor: Invalid target specified");
        return;
    }

    list->ClearRenderTargetView(d3dTarget->handle, colorValue, 0, nullptr);
}

void Direct3D12CommandBuffer::cmdClearDepthStencil(IDepthStencilTarget* target, F32 depthValue, U8 stencilValue) {
    auto d3dTarget = static_cast<Direct3D12DepthStencilTarget*>(target);
    if (!d3dTarget) {
        logger.error(LOG_GRAPHICS, "Direct3D12CommandBuffer::cmdClearDepthStencil: Invalid target specified");
        return;
    }

    list->ClearDepthStencilView(d3dTarget->handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depthValue, stencilValue, 0, nullptr);
}

void Direct3D12CommandBuffer::cmdDraw() {
}

void Direct3D12CommandBuffer::cmdDrawIndexed() {
}

void Direct3D12CommandBuffer::cmdDrawIndirect() {
}

void Direct3D12CommandBuffer::cmdDrawIndexedIndirect() {
}

void Direct3D12CommandBuffer::cmdSetTargets(U32 colorCount, IColorTarget** colorTargets, IDepthStencilTarget* depthStencilTarget) {
    list->OMSetRenderTargets(colorCount, nullptr, FALSE, nullptr); // TODO
}

}  // namespace gfx