#include "retracer/state.hpp"
#include "retracer/glws.hpp"
#include "retracer/glws_egl.hpp"
#include "retracer/retracer.hpp"
#include "dispatch/eglproc_auto.hpp"
#include "forceoffscreen/offscrmgr.h"

using namespace retracer;

EglDrawable::EglDrawable(int w, int h, EGLDisplay eglDisplay, EGLConfig eglConfig, NativeWindow* nativeWindow)
    : Drawable(w, h)
    , mEglDisplay(eglDisplay)
    , mEglConfig(eglConfig)
    , mNativeWindow(nativeWindow)
    , mSurface(_createWindowSurface())
{
    show();
}

EglDrawable::~EglDrawable()
{
    eglDestroySurface(mEglDisplay, mSurface);
    eglWaitNative(EGL_CORE_NATIVE_ENGINE);
}

void EglDrawable::show(void)
{
    if (visible)
    {
        return;
    }

    mNativeWindow->show();

    Drawable::show();
}

void EglDrawable::resize(int w, int h)
{
    if (w == width && h == height)
    {
        return;
    }

    Drawable::resize(w, h);

    mNativeWindow->resize(w, h);

    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(mEglDisplay, mSurface);
    mSurface = _createWindowSurface();

    EGLContext current_context = eglGetCurrentContext();
    eglMakeCurrent(mEglDisplay, mSurface, mSurface, current_context);
    show();
}

void EglDrawable::swapBuffers()
{
    eglSwapBuffers(mEglDisplay, mSurface);
}

void EglDrawable::swapBuffersWithDamage(int *rects, int n_rects)
{
    static bool notSupported = false;

    if (notSupported)
    {
        swapBuffers();
        return;
    }

    if (!eglSwapBuffersWithDamageKHR(mEglDisplay, mSurface, rects, n_rects))
    {
        DBG_LOG("WARNING: eglSwapBuffersWithDamageKHR() may not be suppported, fallback to eglSwapBuffers()\n");
        swapBuffers();
        notSupported = true;
    }
}

EGLSurface EglDrawable::_createWindowSurface()
{
    EGLNativeWindowType handle = mNativeWindow->getHandle();
    EGLSurface surface = eglCreateWindowSurface(mEglDisplay, mEglConfig, handle, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        gRetracer.reportAndAbort("Error creating window surface! size (%dx%d), eglGetError: 0x%04x",
                                 mNativeWindow->getWidth(), mNativeWindow->getHeight(), eglGetError());
    }
    return surface;
}

EglPbufferDrawable::EglPbufferDrawable(EGLDisplay eglDisplay, EGLConfig eglConfig, EGLint const* attribList)
    : PbufferDrawable(attribList)
    , mEglDisplay(eglDisplay)
    , mSurface(eglCreatePbufferSurface(eglDisplay, eglConfig, attribList))
{
    if (mSurface == EGL_NO_SURFACE)
    {
        gRetracer.reportAndAbort("Error creating pbuffer surface: 0x%04x", eglGetError());
    }
}

EglPbufferDrawable::~EglPbufferDrawable()
{
    eglDestroySurface(mEglDisplay, mSurface);
}


namespace retracer
{

NativeWindow::NativeWindow(int width, int height, const std::string& title)
    : mHandle()
    , mVisible(false)
    , mWidth(width)
    , mHeight(height)
{
}

void NativeWindow::show()
{
    mVisible = true;
}

bool NativeWindow::resize(int w, int h)
{
    if (w == mWidth && h == mHeight)
    {
        return false;
    }

    mWidth = w;
    mHeight = h;
    return true;
}

EglContext::EglContext(Profile profile, EGLDisplay eglDisplay, EGLContext ctx, Context* shareContext)
    : Context(profile, shareContext)
    , mEglDisplay(eglDisplay)
    , mContext(ctx)
{}

EglContext::~EglContext()
{
    if (_offscrMgr)
    {
        if (_offscrMgr == gRetracer.mpOffscrMgr)
        {
            gRetracer.mpOffscrMgr = NULL;
        }
        // At this point, there might not be a current context,
        // and the context to which the offscreen-manager's created GL-objects
        // belong might have already been destroyed by the application.
        // Therefore, we take ownership of the manager's objects so that the
        // manager doesn't try to delete them (potentially causing a crash).
        // Since the context is going to be destroyed anyway, this is not a leak.
        _offscrMgr->ReleaseOwnershipOfGLObjects();
        delete _offscrMgr;
        _offscrMgr = NULL;
    }
    eglDestroyContext(mEglDisplay, mContext);
#ifndef ANDROID
    // delete all dma buffers
    for (auto iter2 : mGraphicBuffers) {
        if (iter2 != NULL) {
            unmap_fixture_memory_bufs(iter2);
            delete iter2;
        }
    }
#endif
}

GlwsEgl::GlwsEgl()
    : GLWS()
    , mEglNativeDisplay(0)
    , mEglNativeWindow(0)
    , mEglConfig(0)
    , mEglDisplay(EGL_NO_DISPLAY)
    , mNativeVisualId(0)
{
}

GlwsEgl::~GlwsEgl()
{
}

void GlwsEgl::Init(Profile /*profile*/)
{
    mEglNativeDisplay = getNativeDisplay();
    if (gRetracer.mOptions.mPbufferRendering)
    {
        mEglDisplay = EGL_NO_DISPLAY;

        PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
        if (eglQueryDevicesEXT)
        {
            static const int MAX_DEVICES = 10;
            EGLDeviceEXT eglDevs[MAX_DEVICES];
            EGLint numDevices;

            if (eglQueryDevicesEXT(MAX_DEVICES, eglDevs, &numDevices) == EGL_TRUE)
            {
                DBG_LOG("Detected %d devices -- choosing the first\n", numDevices);
                PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
                if (eglGetPlatformDisplayEXT)
                {
                    mEglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, eglDevs[0], 0);
                }
            }
            else
            {
                DBG_LOG("eglQueryDevicesEXT() failed: 0x%04x\n", eglGetError());
            }
        }

        if (mEglDisplay == EGL_NO_DISPLAY) // fallback
        {
            DBG_LOG("Fallback to eglGetDisplay() for pbuffer target\n");
            mEglDisplay = eglGetDisplay(mEglNativeDisplay);
        }
    }
    else
    {
        mEglDisplay = eglGetDisplay(mEglNativeDisplay);
    }
    gRetracer.mState.mEglDisplay = mEglDisplay;

    if (mEglDisplay == EGL_NO_DISPLAY)
    {
        int error = eglGetError();
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("Unable to get EGL display: 0x%04x", error);
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(mEglDisplay, &major, &minor))
    {
        int error = eglGetError();
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("Unable to initialize EGL display: 0x%04x", error);
    }
    DBG_LOG("eglInitialize %d.%d\n", major, minor);

    RetraceOptions& o = gRetracer.mOptions;
    EGLint samples = o.mOnscreenConfig.msaa_samples;
    if(samples == -1)
    {
        samples = EGL_DONT_CARE;
    }

    // For onscreen, colorbits (RGBA), depth, stencil and MSAA have an impact on perf.
    // For offscreen mode, the attribs for the onscreen framebuffer dont matter as much, except MSAA.
    const EGLint attribs[] = {
        EGL_SAMPLE_BUFFERS, o.mOnscreenConfig.msaa_samples ? 1 : 0,
        EGL_SAMPLES, o.mOnscreenConfig.msaa_samples,
        EGL_SURFACE_TYPE, gRetracer.mOptions.mPbufferRendering ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
        EGL_RED_SIZE, o.mOnscreenConfig.red,
        EGL_GREEN_SIZE, o.mOnscreenConfig.green,
        EGL_BLUE_SIZE, o.mOnscreenConfig.blue,
        EGL_ALPHA_SIZE, o.mOnscreenConfig.alpha,
        EGL_DEPTH_SIZE, o.mOnscreenConfig.depth,
        EGL_STENCIL_SIZE, o.mOnscreenConfig.stencil,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint configCnt = 1024;
    EGLConfig configs[configCnt];
    EGLint numConfigs = 1024;

    EGLBoolean result = eglChooseConfig(mEglDisplay, attribs, configs, configCnt, &numConfigs);
    if (result == EGL_FALSE)
    {
        int error = eglGetError();
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("eglChooseConfig failed: 0x%04x", error);
    }

    int best_config = PAFW_Choose_EGL_Config(attribs, mEglDisplay, configs, numConfigs);
    if (best_config == -1)
    {
        int error = eglGetError();
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("No suitable EGL configuration found: 0x%04x", error);
    }

    if (best_config >= numConfigs)
    {
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("Internal error");
    }

    mEglConfig = configs[best_config];
    mEglConfigInfo = EglConfigInfo(mEglDisplay, mEglConfig);
    std::stringstream ss;
    ss << "EGL Configurations:\n" <<
        "Specified EGL configuration:\n" << o.mOnscreenConfig << "\n" <<
        "Selected EGL configuration:\n" << mEglConfigInfo << "\n";

    DBG_LOG("%s\n", ss.str().c_str());

    bool passed = false;
    if (o.mStrictEGLMode)
    {
        DBG_LOG("Checking strict EGL config.\n");
        passed = o.mOnscreenConfig.isStrictEgl(mEglConfigInfo);
    }
    else if (o.mStrictColorMode)
    {
        DBG_LOG("Checking strict EGL color config.\n");
        passed = o.mOnscreenConfig.isStrictColor(mEglConfigInfo);
    }
    else
    {
        DBG_LOG("Checking sane EGL config.\n");
        passed = o.mOnscreenConfig.isSane(mEglConfigInfo);
    }
    if (!passed)
    {
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("Failed EGL sanity check");
    }

    DBG_LOG("Successful EGL check - using this configuration.\n");

    if (!eglGetConfigAttrib(mEglDisplay, mEglConfig, EGL_NATIVE_VISUAL_ID, &mNativeVisualId))
    {
        int error = eglGetError();
        releaseNativeDisplay(mEglNativeDisplay);
        gRetracer.reportAndAbort("eglGetConfigAttrib with EGL_NATIVE_VISUAL_ID failed: 0x%04x", error);
    }

    postInit();
}

Drawable* GlwsEgl::CreatePbufferDrawable(EGLint const* attribList)
{
    Drawable* handler;
    NativeWindowMutex.lock();
    handler = new EglPbufferDrawable(mEglDisplay, mEglConfig, attribList);
    NativeWindowMutex.unlock();
    return handler;
}

Context* GlwsEgl::CreateContext(Context* shareContext, Profile profile)
{
    EGLint attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile,
        EGL_NONE
    };

    EGLContext eglShareContext = EGL_NO_CONTEXT;
    if (shareContext)
    {
        eglShareContext = static_cast<EglContext*>(shareContext)->mContext;
    }

    EGLContext newCtx = eglCreateContext(mEglDisplay, mEglConfig, eglShareContext, attribs);

    if (!newCtx)
    {
        return 0;
    }

    return new EglContext(profile, mEglDisplay, newCtx, shareContext);
}

bool GlwsEgl::MakeCurrent(Drawable* drawable, Context* context)
{
    EGLSurface eglSurface = EGL_NO_SURFACE;
    NativeWindowMutex.lock();
    if (dynamic_cast<EglDrawable*>(drawable))
    {
        eglSurface = static_cast<EglDrawable*>(drawable)->getSurface();
    }
    else if (dynamic_cast<EglPbufferDrawable*>(drawable))
    {
        eglSurface = static_cast<EglPbufferDrawable*>(drawable)->getSurface();
    }

    EGLContext eglContext = EGL_NO_CONTEXT;

    if (context)
    {
        eglContext = static_cast<EglContext*>(context)->mContext;
    }

    EGLBoolean ok = eglMakeCurrent(mEglDisplay, eglSurface, eglSurface, eglContext);
    NativeWindowMutex.unlock();

    return ok == EGL_TRUE;
}

EGLImageKHR GlwsEgl::createImageKHR(Context* context, EGLenum target, uintptr_t buffer, const EGLint* attrib_list)
{
    EGLContext eglContext = EGL_NO_CONTEXT;
    if (context)
    {
        eglContext = static_cast<EglContext*>(context)->mContext;
    }

    if (target != EGL_GL_TEXTURE_2D_KHR)
    {
        DBG_LOG("WARNING: Patrace currently only supports target EGL_GL_TEXTURE_2D_KHR for eglCreateImageKHR\n");
    }

    uintptr_t uintBuffer = static_cast<uintptr_t>(buffer);
    EGLClientBuffer eglBuffer = reinterpret_cast<EGLClientBuffer>(uintBuffer);
    return _eglCreateImageKHR(mEglDisplay, eglContext, target, eglBuffer, attrib_list);
}

EGLBoolean GlwsEgl::destroyImageKHR(EGLImageKHR image)
{
    return _eglDestroyImageKHR(mEglDisplay, image);
}

bool GlwsEgl::setAttribute(Drawable* drawable, int attibute, int value)
{
    EGLSurface eglSurface = EGL_NO_SURFACE;

    NativeWindowMutex.lock();
    if (dynamic_cast<EglDrawable*>(drawable))
    {
        eglSurface = static_cast<EglDrawable*>(drawable)->getSurface();
    }
    else if (dynamic_cast<EglPbufferDrawable*>(drawable))
    {
        eglSurface = static_cast<EglPbufferDrawable*>(drawable)->getSurface();
    }

    EGLBoolean ok = _eglSurfaceAttrib(mEglDisplay, eglSurface, attibute, value);
    NativeWindowMutex.unlock();
    return ok == EGL_TRUE;
}

void GlwsEgl::Cleanup()
{
    if (mEglDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(mEglDisplay, NULL, NULL, NULL);
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

Drawable* GlwsEgl::CreateDrawable(int /*width*/, int /*height*/, int /*win*/)
{
    return 0;
}

void GlwsEgl::postInit()
{
}

void GlwsEgl::processStepEvent()
{
}

void GlwsEgl::releaseNativeDisplay(EGLNativeDisplayType /*display*/)
{
}

EGLNativeDisplayType GlwsEgl::getNativeDisplay()
{
    return EGL_DEFAULT_DISPLAY;
}

void GlwsEgl::setNativeWindow(EGLNativeWindowType window)
{
    mEglNativeWindow = window;
}

// Implement a better logic for selecting the most suitable EGL configuration
int PAFW_Choose_EGL_Config(const EGLint *preferred_config, EGLDisplay display, EGLConfig configs[], EGLint numConfigs)
{
    EGLint pref_red_size = -1;     // The initial value for preferred config is -1, which means "don't care" in EGL
    EGLint pref_green_size = -1;
    EGLint pref_blue_size = -1;
    EGLint pref_alpha_size = -1;
    EGLint pref_depth_size = -1;
    EGLint pref_stencil_size = -1;
    EGLint pref_sample_buffers = -1;
    EGLint pref_samples = -1;
    EGLint avail_red_size;
    EGLint avail_green_size;
    EGLint avail_blue_size;
    EGLint avail_alpha_size;
    EGLint avail_depth_size;
    EGLint avail_stencil_size;
    EGLint avail_sample_buffers;
    EGLint avail_samples;
    EGLint best_red_size;
    EGLint best_green_size;
    EGLint best_blue_size;
    EGLint best_alpha_size;
    EGLint best_depth_size;
    EGLint best_stencil_size;
    EGLint best_sample_buffers;
    EGLint best_samples;
    EGLint param;
    EGLint value;
    int i;
    int best_config;
    int old_best_config;
    int avail_config;

    if (numConfigs < 1)
        return -1;

    // First read the values from the preferred config
    i = 0;
    while (1) {
        param = preferred_config[i];
        if (param == EGL_NONE)
            break;
        value = preferred_config[i+1];
        i += 2;

        switch (param)
        {
        case EGL_RED_SIZE:
            pref_red_size = value;
            break;
        case EGL_GREEN_SIZE:
            pref_green_size = value;
            break;
        case EGL_BLUE_SIZE:
            pref_blue_size = value;
            break;
        case EGL_ALPHA_SIZE:
            pref_alpha_size = value;
            break;
        case EGL_DEPTH_SIZE:
            pref_depth_size = value;
            break;
        case EGL_STENCIL_SIZE:
            pref_stencil_size = value;
            break;
        case EGL_SAMPLE_BUFFERS:
            pref_sample_buffers = value;
            break;
        case EGL_SAMPLES:
            pref_samples = value;
            break;
        }
    }
    // Set the actual AA value to pref_samples. I guess this is how EGL works.
    if (pref_sample_buffers == -1 && pref_samples == -1)
        pref_samples = -1;
    else if (pref_sample_buffers < 1)
        pref_samples = 0;
    // else pref_samples = pref_samples;


    // Find the best configuration
    best_config = 0; // Initially the first config is the best
    old_best_config = -1;
    // Then we check if we can find a better config than the current best config
    for (avail_config = 1; avail_config < numConfigs; avail_config++)
    {
        eglGetConfigAttrib(display, configs[avail_config], EGL_RED_SIZE , &avail_red_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_GREEN_SIZE , &avail_green_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_BLUE_SIZE , &avail_blue_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_ALPHA_SIZE , &avail_alpha_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_DEPTH_SIZE , &avail_depth_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_STENCIL_SIZE , &avail_stencil_size);
        eglGetConfigAttrib(display, configs[avail_config], EGL_SAMPLE_BUFFERS , &avail_sample_buffers);
        eglGetConfigAttrib(display, configs[avail_config], EGL_SAMPLES, &avail_samples);

        if (best_config != old_best_config) {
            eglGetConfigAttrib(display, configs[best_config], EGL_RED_SIZE , &best_red_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_GREEN_SIZE , &best_green_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_BLUE_SIZE , &best_blue_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_ALPHA_SIZE , &best_alpha_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_DEPTH_SIZE , &best_depth_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_STENCIL_SIZE , &best_stencil_size);
            eglGetConfigAttrib(display, configs[best_config], EGL_SAMPLE_BUFFERS , &best_sample_buffers);
            eglGetConfigAttrib(display, configs[best_config], EGL_SAMPLES, &best_samples);
            old_best_config = best_config;
        }

        // P is the preferred configuration. A is a better configuration than B if:
        //  if      P.anti_aliasing = x        then         A.anti_aliasing < B.anti_aliasing
        //  if      P.color = x                then         A.color < B.color
        //  if      P.depth = x                then         A.depth < P.depth
        //  if      P.stencil = x              then         A.stencil < P-stencil

        if (pref_samples != -1) {
            if (avail_samples < best_samples) {
                best_config = avail_config;
                continue;
            } else if (avail_samples > best_samples) {
                continue;
            }
        }
        if (pref_red_size != -1 || pref_green_size != -1 || pref_blue_size != -1 || pref_alpha_size != -1) {
            int avail_color_buffer_size = avail_red_size + avail_green_size + avail_blue_size + avail_alpha_size;
            int best_color_buffer_size = best_red_size + best_green_size + best_blue_size + best_alpha_size;
            if (avail_color_buffer_size < best_color_buffer_size) {
                best_config = avail_config;
                continue;
            } else if (avail_color_buffer_size > best_color_buffer_size) {
                continue;
            }
        }
        if (pref_depth_size != -1) {
            if (avail_depth_size < best_depth_size) {
                best_config = avail_config;
                continue;
            } else if (avail_depth_size > best_depth_size) {
                continue;
            }
        }
        if (pref_stencil_size != -1) {
            if (avail_stencil_size < best_stencil_size) {
                best_config = avail_config;
                continue;
            } else if (avail_stencil_size > best_stencil_size) {
                continue;
            }
        }
    }
    return best_config;
}

}
