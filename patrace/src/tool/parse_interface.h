#ifndef PARSE_INTERFACE_H
#define PARSE_INTERFACE_H

#include <deque>
#include <vector>
#include <list>
#include <map> // do not use unordered, since we want reproducible output
#include <tuple>
#include <unordered_set>
#include <set>
#include <utility>
#include <EGL/egl.h>
#include <stdint.h>

#include "dispatch/eglimports.hpp"
#include "common/analysis_utility.hpp"
#include "common/in_file.hpp"
#include "common/file_format.hpp"
#include "common/out_file.hpp"
#include "common/api_info.hpp"
#include "common/parse_api.hpp"
#include "common/trace_model.hpp"
#include "common/trace_model_utility.hpp"
#include "common/os.hpp"
#include "eglstate/context.hpp"
#include "tool/config.hpp"
#include "base/base.hpp"

/// Large negative index number to encourage crashing if used improperly.
const int UNBOUND = -1000;

struct DrawParams
{
    GLenum mode = GL_NONE;

    int vertices = 0;
    int instances = 1;
    int primitives = 0;

    // the below are only for indexed drawing
    int unique_vertices = 0; // this may be different than vertices for indexed drawing, counting vertex reuse
    int max_sparseness = 0; // size of largest unused hole in the used segment of the vertex array
    double avg_sparseness = 0.0; // average size of holes
    int max_value = 0; // highest index
    int min_value = 0; // lowest index
    GLenum value_type = GL_NONE; // type of index values
    // locality metrics are 0.0->1.0
    double vec4_locality = 0.0;
    double temporal_locality = 0.0;
    double spatial_locality = 0.0;
};

static inline const std::string drawEnum(unsigned int enumToFind)
{
    const char *retval = EnumString(enumToFind, "glDraw");
    return retval ? retval : "Unknown";
}

static inline const std::string blendEnum(unsigned int enumToFind)
{
    const char *retval = EnumString(enumToFind, "glBlendFunc");
    return retval ? retval : "Unknown";
}

static inline const std::string texEnum(unsigned int enumToFind)
{
    const char *retval = EnumString(enumToFind, "glTexParameter");
    return retval ? retval : "Unknown";
}

enum SurfaceType
{
    SURFACE_NATIVE,
    SURFACE_PBUFFER,
    SURFACE_PIXMAP,
    SURFACE_COUNT
};

namespace StateTracker
{

struct Buffer
{
    int id;
    int index;
    unsigned call_created;
    unsigned call_destroyed = 0;
    unsigned frame_created;
    unsigned frame_destroyed = 0;
    // A buffer can be viewed in several different ways, if interleaved.
    std::set<std::tuple<GLenum, GLint, GLsizei>> types; // type, components, stride
    std::unordered_set<GLenum> usages;
    Buffer(int _id, int _index, unsigned _call_created, unsigned _frame_created) : id(_id), index(_index),
        call_created(_call_created), frame_created(_frame_created) {}
};

struct SamplerState
{
    GLenum swizzle[4];
    GLenum min_filter;
    GLenum mag_filter;
    GLenum texture_wrap_s;
    GLenum texture_wrap_t;
    GLenum texture_wrap_r;
    GLenum texture_compare_func;
    GLenum texture_compare_mode;
    GLint min_lod;
    GLint max_lod;
    GLint base_level;
    GLint max_level;
    GLenum depth_stencil_mode;
    GLfloat anisotropy = 1.0;
    GLenum srgb_decode = GL_DECODE_EXT;
    SamplerState()
    {
        // Set to OpenGL defaults
        swizzle[0] = GL_RED;
        swizzle[1] = GL_GREEN;
        swizzle[2] = GL_BLUE;
        swizzle[3] = GL_ALPHA;
        min_filter = GL_NEAREST_MIPMAP_LINEAR;
        mag_filter = GL_LINEAR;
        texture_wrap_s = GL_REPEAT;
        texture_wrap_t = GL_REPEAT;
        texture_wrap_r = GL_REPEAT;
        max_level = 1000;
        base_level = 0;
        min_lod = -1000;
        max_lod = 1000;
        texture_compare_mode = GL_NONE;
        texture_compare_func = GL_LEQUAL;
        depth_stencil_mode = GL_DEPTH_COMPONENT;
    }
};

struct Resource
{
    GLuint id;
    int index;
    unsigned call_created;
    int call_destroyed = -1;
    unsigned frame_created;
    int frame_destroyed = -1;
    std::string label;
    Resource(GLuint _id, int _index, unsigned _call_created, unsigned _frame_created) : id(_id), index(_index),
             call_created(_call_created), frame_created(_frame_created) {}
};

template<typename T>
struct ResourceStorage
{
    ResourceStorage(ResourceStorage* _share = nullptr, int zeroval = UNBOUND) : share(_share)
    {
        remapping[0] = zeroval; // make sure zero remapping does not crash
        if (!share)
        {
            share = this;
        }
        assert(store.size() == 0);
        assert(share->store.size() != 9999999); // test memory access
	assert(share->remapping.at(0) == zeroval);
    }

    /// Make sure we do not accidentially copy this resource around somehow, because that would
    /// invalidate all our pointers!
    ResourceStorage(const ResourceStorage& r) = delete;

    inline T& at(int idx) { return share->store.at(idx); }
    inline const T& at(int idx) const { return share->store.at(idx); }
    inline T& operator[](int idx) { return share->store.at(idx); }
    inline T& id(GLuint id) { int idx; idx = share->remapping.at(id); return at(idx); }
    inline const T& id(GLuint id) const { int idx; idx = share->remapping.at(id); return at(idx); }
    inline int remap(GLuint id) const { return share->remapping.at(id); }
    inline bool contains(GLuint id) const { return share->remapping.count(id) > 0; }
    const std::vector<T>& all() const { return share->store; }
    size_t size() const { return share->store.size(); }
    ssize_t ssize() const { return static_cast<ssize_t>(share->store.size()); }

    inline T & add(GLuint _id, unsigned _call_created, unsigned _frame_created)
    {
        int idx = share->store.size();
        share->remapping[_id] = idx;
        share->store.emplace_back(_id, idx, _call_created, _frame_created);
        return share->store.back();
    }

    inline void remove(GLuint _id, unsigned _call_destroyed, unsigned _frame_destroyed)
    {
        int idx = share->remapping.at(_id);
        share->store.at(idx).call_destroyed = _call_destroyed;
        share->store.at(idx).frame_destroyed = _frame_destroyed;
    }

private:
    ResourceStorage* share = nullptr;
    std::map<int, int> remapping;
    std::vector<T> store;
};

struct Sampler : public Resource
{
    SamplerState state;
    Sampler(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

struct Query : public Resource
{
    Query(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

struct TransformFeedback : public Resource
{
    GLenum primitiveMode;
    TransformFeedback(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created),
                      primitiveMode(GL_NONE) {}
};

struct Texture : public Resource
{
    bool immutable = false;
    int levels = 0;
    int width = 1;
    int height = 1;
    int depth = 1;
    GLenum internal_format;
    GLenum binding_point;
    SamplerState state;
    Texture(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created),
            internal_format(GL_NONE), binding_point(GL_NONE) {}

    // the below are not set by the parse_interface, because we need dynamic runtime info to fill them out
    std::unordered_set<GLenum> used_min_filters;
    std::unordered_set<GLenum> used_mag_filters;
};

struct Renderbuffer : public Resource
{
    GLsizei samples;
    GLenum internalformat;
    GLsizei width;
    GLsizei height;
    Renderbuffer(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created),
                 samples(0), internalformat(GL_NONE), width(0), height(0) {}
};

struct FillState
{
    struct
    {
        GLint x = 0;
        GLint y = 0;
        GLsizei width = -1;
        GLsizei height = -1;
    } scissor;
    GLboolean depthmask = GL_TRUE;
    GLenum depthfunc = GL_LESS;
    GLboolean colormask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLfloat clearcolor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::unordered_map<GLenum, GLuint> stencilmask = { { GL_BACK, UINT32_MAX }, { GL_FRONT, UINT32_MAX } };
    GLint clearstencil = 0;
    GLfloat cleardepth = 1.0f;
    unsigned call_stored = 0; // for storing clears, record when it was recorded
};

struct Attachment
{
    Attachment(GLuint _id, GLenum _type, int _index) : id(_id), type(_type), index(_index) {}
    Attachment() {}

    GLuint id = 0; // texture or renderpass by ID
    GLenum type = GL_TEXTURE_2D; // texture type (binding point)
    int index = UNBOUND; // texture or renderpass by index
    std::vector<FillState> clears; // track clears done to find duplicated clear calls
};

struct Framebuffer : public Resource
{
    std::map<GLenum, Attachment> attachments; // buffer type : Attachment
    Framebuffer(int _id, int _index, unsigned _call_created, unsigned _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
    int used = 0; // considered 'used' if anything drawn to it
    int last_used = 0; // which frame it was last drawn to
    int attachment_calls = 0; // how many times we have changed attachments to it
    std::unordered_map<int, int> duplicate_clears; // by frame
    std::unordered_map<int, int> total_clears; // by frame
};

enum GLSLPrecision
{
    HIGHP, MEDIUMP, LOWP
};

struct GLSLSampler
{
    GLSLPrecision precision = HIGHP;
    GLuint value = 0;
    GLenum type = GL_NONE;
    GLSLSampler(GLSLPrecision p, GLuint v, GLenum t) : precision(p), value(v), type(t) {}
};

struct Shader : public Resource
{
    std::string source_code;
    std::string source_compressed;
    bool contains_invariants = false;
    bool contains_optimize_off_pragma = false;
    bool contains_debug_on_pragma = false;
    bool contains_invariant_all_pragma = false;
    std::unordered_map<std::string, GLSLSampler> samplers;
    int varying_locations_used = 0;
    int lowp_varyings = 0;
    int mediump_varyings = 0;
    int highp_varyings = 0;
    GLenum shader_type = GL_NONE;
    unsigned call = 0; // call where shader source was uploaded
    bool compile_status = false;
    std::vector<std::string> extensions;

    Shader(int _id, int _index, int _call_created, int _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

struct base_stats
{
    int dispatches = 0; // number of compute dispatches
    int drawcalls = 0;
    int64_t vertices = 0;
    int64_t primitives = 0;
};

struct ProgramPipeline : public Resource
{
    base_stats stats;
    std::unordered_map<GLenum, int> program_stages; // enum to index
    ProgramPipeline(int _id, int _index, int _call_created, int _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

struct Program : public Resource
{
    base_stats stats;
    std::unordered_map<int, base_stats> stats_per_frame;
    std::map<GLenum, int> shaders; // attached shaders, by index
    bool link_status = false;
    std::string md5sum; // md5sum of the combined source of all shaders, for matching with shaders in dumps

    std::map<int, std::string> uniformNames;
    std::map<std::string, int> uniformLocations;
    std::map<int, std::vector<int>> uniformValues; // only those we need for samplers stored, we don't know the array size of each, hence vectors
    std::map<int, int> uniformLastChanged; // call number when last set

    Program(int _id, int _index, int _call_created, int _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

// A "render pass" binds together draw calls that operate on the same framebuffer configuration, in the same frame.
struct RenderPass
{
    int index; // 0...N for each frame
    int frame;
    bool active;
    int first_call; // first draw call in render pass
    int last_call; // last draw call in render pass
    unsigned drawframebuffer; // currently bound, stored by ID
    std::unordered_set<int> used_renderbuffers; // usage statistics, by index
    std::unordered_set<int> used_texture_targets; // usage statistics, by index, only counted if used as render target
    std::unordered_set<int> used_programs; // usage statistics, by index
    std::map<GLenum, std::pair<GLenum, int>> render_targets; // detailed info, buffer type : renderbuffer/texture : index
    int draw_calls;
    int64_t vertices;
    int64_t primitives;
    std::string snapshot_filename;
    RenderPass(int _index, int _call, int _frame) : index(_index), frame(_frame), active(true), first_call(_call), last_call(-1),
               drawframebuffer(0), draw_calls(0), vertices(0), primitives(0) {}
};

struct VertexArrayObject : public Resource
{
    VertexArrayObject(int _id, int _index, int _call_created, int _frame_created) : Resource(_id, _index, _call_created, _frame_created) {}
};

struct Context : public Resource
{
    int display;
    int share_context = 0; // by id
    Context* share = nullptr;

    /// Global clear fill states
    FillState fillstate;

    std::map<int, int> draw_calls_per_frame;
    std::map<int, int> renderpasses_per_frame;
    std::map<int, int> flush_calls_per_frame;
    std::map<int, int> finish_calls_per_frame;

    std::vector<RenderPass> render_passes;

    struct
    {
        GLint x = 0;
        GLint y = 0;
        GLsizei width = -1;
        GLsizei height = -1;
    } viewport;

    unsigned drawframebuffer = 0; // currently bound, stored by ID
    unsigned readframebuffer = 0; // currently bound, stored by ID
    unsigned prev_drawframebuffer = 0; // previously bound, stored by ID
    unsigned prev_readframebuffer = 0; // previously bound, stored by ID

    ResourceStorage<Framebuffer> framebuffers;
    ResourceStorage<Texture> textures;
    ResourceStorage<Renderbuffer> renderbuffers;
    ResourceStorage<Sampler> samplers;
    ResourceStorage<Query> queries;
    ResourceStorage<TransformFeedback> transform_feedbacks;
    ResourceStorage<Buffer> buffers;
    ResourceStorage<Program> programs;
    ResourceStorage<Shader> shaders;
    ResourceStorage<VertexArrayObject> vaos;
    ResourceStorage<ProgramPipeline> program_pipelines;

    int renderbuffer_index = UNBOUND;
    std::vector<GLenum> draw_buffers; // as set by glDrawBuffers()

    struct MipmapGeneration
    {
        unsigned call; // call number of glGenerateMipmap
        int frame;
        int texture_index;
    };
    std::vector<MipmapGeneration> mipmaps;
    std::map<GLuint, std::map<GLenum, GLuint>> textureUnits; // texture unit : target : texture id
    GLuint activeTextureUnit = 0;

    std::map<GLuint, int> sampler_binding; // texture unit : sampler id

    std::map<GLenum, int> query_binding; // target : query id

    GLuint transform_feedback_binding = 0;

    struct BufferBinding
    {
        GLuint buffer; // id
        GLintptr offset;
        GLsizeiptr size;
    };
    std::map<GLenum, std::map<GLuint, BufferBinding>> boundBufferIds; // target : GL buffer index : buffer info

    int program_index = UNBOUND;

    // support for separate program pipelines
    int program_pipeline_index = UNBOUND;

    int vao_index = UNBOUND;
    std::unordered_set<GLuint> vao_enabled; // currently enabled VAOs

    int patchSize = 3;
    std::map<GLenum, bool> enabled;

    Context(int _id, int _display, int _index, int _share_context, unsigned _call_created, unsigned _frame_created, Context* _share)
            : Resource(_id, _index, _call_created, _frame_created), display(_display), share_context(_share_context), share(_share),
              framebuffers(&_share->framebuffers, 0),
              textures(&_share->textures),
              renderbuffers(&_share->renderbuffers),
              samplers(&_share->samplers),
              queries(&_share->queries),
              transform_feedbacks(&_share->transform_feedbacks),
              buffers(&_share->buffers),
              programs(&_share->programs),
              shaders(&_share->shaders),
              vaos(&_share->vaos),
              program_pipelines(&_share->program_pipelines)
    {
        init(_call_created, _frame_created);
    }

    Context(int _id, int _display, int _index, unsigned _call_created, unsigned _frame_created)
            : Resource(_id, _index, _call_created, _frame_created), display(_display), framebuffers(nullptr, 0)
    {
        init(_call_created, _frame_created);
    }

private:
    void init(unsigned _call_created, unsigned _frame_created)
    {
        Framebuffer& f = framebuffers.add(0, _call_created, _frame_created); // create default framebuffer
        f.attachments[GL_STENCIL_ATTACHMENT] = Attachment();
        f.attachments[GL_DEPTH_ATTACHMENT] = Attachment();
        f.attachments[GL_COLOR_ATTACHMENT0] = Attachment();
        draw_buffers.resize(1);
        draw_buffers[0] = GL_BACK;
        enabled[GL_DITHER] = true; // the only one that starts enabled
    }
};

struct Surface : public Resource
{
    int display;
    SurfaceType type;
    std::map<GLenum, GLint> attribs;
    int width;
    int height;
    std::vector<int> swap_calls; // by call numbers
    Surface(GLuint _id, int _display, int _index, unsigned _call_created, unsigned _frame_created, SurfaceType _type, std::map<GLenum, GLint> _attribs, int _width, int _height)
            : Resource(_id, _index, _call_created, _frame_created), display(_display), type(_type), attribs(_attribs), width(_width), height(_height) {}
};

}; // end interface

class ParseInterfaceBase
{
public:
    ParseInterfaceBase()
    {
        context_remapping[0] = UNBOUND;
    }
    virtual bool open(const std::string& input, const std::string& output = std::string()) = 0;
    virtual void close() = 0;
    virtual common::CallTM* next_call() = 0;
    virtual DrawParams getDrawCallCount(common::CallTM *call);

    void dumpFrameBuffers(bool value) { mDumpFramebuffers = value; }
    void setQuickMode(bool value) { mQuickMode = value; }
    void setDisplayMode(bool value) { mDisplayMode = value; }
    void setScreenshots(bool value) { mScreenshots = value; }
    void interpret_call(common::CallTM *call);

    std::deque<StateTracker::Context> contexts; // using deque to avoid moving contents around in memory, invalidating pointers
    std::vector<StateTracker::Surface> surfaces;
    std::map<int, int> context_remapping; // from id to index in the original file
    std::map<int, int> surface_remapping; // from id to index in the original file
    std::map<int, int> current_context; // map threads to contexts
    std::map<int, int> current_surface; // map threads to surfaces

    std::string filename;
    unsigned numThreads = 0;
    unsigned defaultTid = 0;
    Json::Value threadArray;
    Json::Value header;
    int frames = 0; // number of frames processed so far
    int surface_index = UNBOUND;
    int context_index = UNBOUND;

    int highest_gles_version = 0; // multiplied by 10 to avoid float
    std::unordered_set<std::string> used_extensions;

private:
    bool find_duplicate_clears(const StateTracker::FillState& f, const StateTracker::Attachment& at, GLenum type, StateTracker::Framebuffer& fbo, const std::string& call);

protected:
    bool mDumpFramebuffers = false;
    bool mQuickMode = true;
    bool mDisplayMode = false;
    bool mScreenshots = true;
};

class ParseInterface : public ParseInterfaceBase
{
public:
    ParseInterface(bool _only_default = false) : ParseInterfaceBase(), _curFrame(NULL),
          _curFrameIndex(0), _curCallIndexInFrame(0), only_default(_only_default) {}

    virtual bool open(const std::string& input, const std::string& output = std::string()) override;
    virtual void close() override;
    virtual common::CallTM* next_call() override;

    virtual void writeout(common::OutFile &outputFile, common::CallTM *call);

    common::TraceFileTM inputFile;
    common::OutFile outputFile;

private:
    common::FrameTM* _curFrame;
    unsigned _curFrameIndex;
    unsigned _curCallIndexInFrame;
    bool only_default; // only parse default tid calls
};

#endif
