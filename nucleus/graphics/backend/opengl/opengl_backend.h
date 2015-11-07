/**
 * (c) 2015 Alexandro Sanchez Bach. All rights reserved.
 * Released under GPL v2 license. Read LICENSE for more details.
 */

#pragma once

#include "nucleus/common.h"
#include "nucleus/graphics/graphics.h"

namespace gfx {

// OpenGL context handler
#if defined(NUCLEUS_PLATFORM_WINDOWS)
using OpenGLContext = HGLRC;
#elif defined(NUCLEUS_PLATFORM_LINUX)
using OpenGLContext = GLXContext;
#elif defined(NUCLEUS_PLATFORM_OSX)
using OpenGLContext = GLXContext;
#elif defined(NUCLEUS_PLATFORM_ANDROID)
using OpenGLContext = EGLContext;
#elif defined(NUCLEUS_PLATFORM_IOS)
using OpenGLContext = EGLContext;
#endif

class OpenGLBackend : public IBackend {
private:
    OpenGLContext context;

public:
    OpenGLBackend();
    ~OpenGLBackend();

    virtual bool initialize(const BackendParameters& params) override;

    ICommandQueue* createCommandQueue() override;
    ICommandBuffer* createCommandBuffer() override;
    IHeap* createHeap(const HeapDesc& desc) override;
    void createPipeline() override;
    void createShader() override;
    ITexture* createTexture(const TextureDesc& desc) override;
};

}  // namespace gfx
