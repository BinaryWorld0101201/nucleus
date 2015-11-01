/**
 * (c) 2015 Alexandro Sanchez Bach. All rights reserved.
 * Released under GPL v2 license. Read LICENSE for more details.
 */

#include "window_opengl.h"

// OpenGL dependencies
#include "nucleus/graphics/backend/opengl/opengl.h"

static const PIXELFORMATDESCRIPTOR pfd = {
    sizeof(PIXELFORMATDESCRIPTOR),  // Size of this Pixel Format Descriptor
    1,                              // Version
    PFD_DRAW_TO_WINDOW |            // Format must support Window
    PFD_SUPPORT_OPENGL |            // Format must support OpenGL
    PFD_DOUBLEBUFFER,               // Format must support double buffering
    PFD_TYPE_RGBA,                  // Request an RGBA format
    24,                             // Select our color depth
    0, 0, 0, 0, 0, 0,               // Color bits ignored
    8,                              // No alpha buffer
    0,                              // Shift bit ignored
    0,                              // No accumulation Buffer
    0, 0, 0, 0,                     // Accumulation bits Ignored
    16,                             // At least a 16-bit Z-buffer (depth buffer)
    8,                              // 8-bit stencil buffer
    0,                              // No auxiliary buffer
    PFD_MAIN_PLANE,                 // Main drawing layer
    0,                              // Reserved
    0, 0, 0                         // Layer masks ignored
};

WindowOpenGL::WindowOpenGL(const std::string& title, int width, int height) : Window(title, width, height)
{
    hdc = GetDC(m_hwnd);

    /**
     * Setting up drawing context
     */
    if (!hdc) {
        MessageBox(m_hwnd, "Failed to get a device context.", "Nucleus", MB_ICONEXCLAMATION | MB_OK);
        return;
    }
    GLuint PixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (!PixelFormat) {
        MessageBox(m_hwnd, "Can't find a suitable PixelFormat.", "Nucleus", MB_ICONEXCLAMATION | MB_OK);
        return;
    }
    if (!SetPixelFormat(hdc, PixelFormat, &pfd)) {
        MessageBox(m_hwnd, "Can't set the PixelFormat.", "Nucleus", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    /**
     * Initialize OpenGL extensions (through a dummy context)
     */
    HGLRC dummyRc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, dummyRc);
    if (!gfx::initializeOpenGL()) {
        MessageBox(m_hwnd, "Could not initialize all OpenGL extensions.", "Nucleus", MB_ICONEXCLAMATION | MB_OK);
        return;
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummyRc);
}

void WindowOpenGL::swap_buffers()
{
    SwapBuffers(hdc);
}
