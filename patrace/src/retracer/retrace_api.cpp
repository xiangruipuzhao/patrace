#include <retracer/retracer.hpp>

#include <retracer/retrace_api.hpp>

#include <common/gl_extension_supported.hpp>

#include <common/os.hpp>

#include <string.h>

#include "dma_buffer/dma_buffer.hpp"

namespace retracer {

void ignore(char* /*src*/) {

}
void unsupported(char* /*src*/) {
    DBG_LOG("Unsupported: \n");
}

}

using namespace common;
using namespace retracer;

// Copies content of CSB, addressed by <name>, into the mapped memory
// of the buffer currently bound to <target>.
void glCopyClientSideBuffer(GLenum target, unsigned int name)
{
    GLuint buffer = getBoundBuffer(target);
    if (buffer == 0)
    {
        const char* name = bufferName(target);
        gRetracer.reportAndAbort("Trying to copy client side buffer, but no %s buffer bound\n", name);
    }
    void* data = gRetracer.getCurrentContext()._bufferToData_map[buffer];
    ClientSideBufferObject* csb = gRetracer.mCSBuffers.get_object(gRetracer.getCurTid(), name);
    if (data == NULL)
    {
        const char* name = bufferName(target);
        gRetracer.reportAndAbort("Trying to copy client side buffer, but failed to fetch %s buffer\n", name);
    }
    if (csb == NULL)
    {
        gRetracer.reportAndAbort("Trying to copy client side buffer, but failed to fetch client side buffer %d\n", name);
    }
    memcpy(data, csb->base_address, csb->size);
}

// Patches content of CSB, addressed by <name>, into the mapped memory
// of the buffer currently bound to <target>.
void glPatchClientSideBuffer(GLenum target, int _size, const char* _data)
{
    GLuint buffer = getBoundBuffer(target);
    if (buffer == 0)
    {
        const char* name = bufferName(target);
        gRetracer.reportAndAbort("Trying to copy client side buffer, but no %s buffer bound!\n", name);
    }
    void* data = gRetracer.getCurrentContext()._bufferToData_map[buffer];
    if (data == NULL)
    {
        const char* name = bufferName(target);
        gRetracer.reportAndAbort("Trying to copy client side buffer, but failed to fetch %s buffer!\n", name);
    }

    // patch it
    const unsigned char* patch_list_ptr = reinterpret_cast<const unsigned char*>(_data);
    const CSBPatchList* ppl = (const CSBPatchList*)patch_list_ptr;

    patch_list_ptr += sizeof(CSBPatchList);
    for(unsigned int i = 0; i < ppl->count; i++)
    {
        const CSBPatch* ppatch = reinterpret_cast<const CSBPatch*>(patch_list_ptr);
        const unsigned char* pdata = patch_list_ptr + sizeof(CSBPatch);

        memcpy(static_cast<unsigned char*>(data) + ppatch->offset, pdata, ppatch->length);

        patch_list_ptr = pdata + ppatch->length;
    }
}

void glClientSideBufferData(unsigned int _name, int _size, const char* _data) {
#ifndef NDEBUG
    gRetracer.mClientSideMemoryDataSize += _size;
#endif
    gRetracer.mCSBuffers.object_data(gRetracer.getCurTid(), _name, _size, _data, true);
}

void glClientSideBufferSubData(unsigned int _name, int _offset, int _size, const char* _data) {
#ifndef NDEBUG
    gRetracer.mClientSideMemoryDataSize += _size;
#endif
    gRetracer.mCSBuffers.object_subdata(gRetracer.getCurTid(), _name, _offset, _size, _data);
}

void glCreateClientSideBuffer(unsigned int name) {
    gRetracer.mCSBuffers.create_object(gRetracer.getCurTid(), name);
}

void glDeleteClientSideBuffer(unsigned int _name) {
    gRetracer.mCSBuffers.delete_object(gRetracer.getCurTid(), _name);
}

unsigned int glGenGraphicBuffer_ARM(unsigned int _width, unsigned int _height, int _pix_format, unsigned int _usage) {
    Context& context = gRetracer.getCurrentContext();
#ifdef ANDROID
    GraphicBuffer *graphicBuffer = new GraphicBuffer(_width, _height, (PixelFormat)_pix_format, _usage);
    context.mGraphicBuffers.push_back(graphicBuffer);

    return context.mGraphicBuffers.size() - 1;
#else
    auto iter = context.mAndroidToLinuxPixelMap.find(static_cast<PixelFormat>(_pix_format));
    if (iter == context.mAndroidToLinuxPixelMap.end()) {
        gRetracer.reportAndAbort("Cannot find the corresponding PixelFormat of %x\n", _pix_format);
    }
    unsigned int linux_pix_format = iter->second;
    mali_tpi_egl_pixmap_format format = MALI_TPI_EGL_PIXMAP_FORMAT_INVALID;
    switch (linux_pix_format) {
    case DRM_FORMAT_NV12:
        format = MALI_TPI_EGL_PIXMAP_FORMAT_NV12_BT601_NARROW;
        break;
    case DRM_FORMAT_NV21:
        format = MALI_TPI_EGL_PIXMAP_FORMAT_NV21_BT601_NARROW;
        break;
    case DRM_FORMAT_YVU420:
        format = MALI_TPI_EGL_PIXMAP_FORMAT_YV12_BT601_NARROW;
        break;
    case DRM_FORMAT_RGB888:
        format = MALI_TPI_EGL_PIXMAP_FORMAT_B8G8R8;
        break;
    case DRM_FORMAT_P010:
        format = MALI_TPI_EGL_PIXMAP_FORMAT_P010;
        break;
    default:
        gRetracer.reportAndAbort("glGenGraphicBuffer_ARM doesn't support format=0x%x, aborting...\n", _pix_format);
        break;
    }
    egl_image_fixture *fix = new egl_image_fixture(format);
    fill_image_attributes(fix, format, linux_pix_format, _width, _height, fix->attrib_size, fix->attribs);
    context.mGraphicBuffers.push_back(fix);

    return context.mGraphicBuffers.size() - 1;
#endif
}

void glGraphicBufferData_ARM(unsigned int _name, int _size, const char * _data) {
    Context& context = gRetracer.getCurrentContext();
    int id = context.getGraphicBufferMap().RValue(_name);
#ifdef ANDROID
    GraphicBuffer *graphicBuffer = context.mGraphicBuffers[id];

    // Writing data
    void *writePtr = 0;
    graphicBuffer->lock(GraphicBuffer::USAGE_SW_WRITE_OFTEN, &writePtr);
    memcpy(writePtr, _data, _size);
    graphicBuffer->unlock();
#else
    egl_image_fixture *fix = context.mGraphicBuffers[id];
    refresh_dma_data(fix, _size, (const unsigned char *)_data);
#endif
}

void glDeleteGraphicBuffer_ARM(unsigned int _name) {
#ifndef ANDROID
    Context& context = gRetracer.getCurrentContext();
    int id = context.getGraphicBufferMap().RValue(_name);
    egl_image_fixture *fix = context.mGraphicBuffers[id];
    unmap_fixture_memory_bufs(fix);
    close_fixture_memory_bufs(fix);
    delete fix;
    context.mGraphicBuffers[id] = NULL;
#endif
}

void paMandatoryExtensions(int count, Array<const char*> string)
{
    for (int i=0; i<count; i++) {
        const char* ext = string[i];
        if (isGlesExtensionSupported(ext) == false) {
            gRetracer.reportAndAbort("Mandatory extension %s not supported!", ext);
        }
    }
}
