#ifndef _RETRACER_HPP_
#define _RETRACER_HPP_

#include "cmd_options.hpp"
#include "retracer/retrace_options.hpp"
#include "retracer/state.hpp"
#include "retracer/texture.hpp"
#include "helper/states.h"
#include "graphic_buffer/GraphicBuffer.hpp"
#include "dma_buffer/dma_buffer.hpp"

#include "common/file_format.hpp"
#include "common/in_file.hpp"
#include "common/os.hpp"
#include "common/os_thread.hpp"
#include "common/os_time.hpp"
#include "common/memory.hpp"
#ifndef _WIN32
#include "common/memoryinfo.hpp"
#endif

#include <string>
#include <vector>

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

class OffscreenManager;
class Quad;
class Collection;

namespace retracer {

class Retracer;
class WorkThread;
class Work;

class Work
{
public:
    enum WorkStatus
    {
        CREATED = 0,
        SUBMITED,
        EXECUTED,
        COMPLETED,
        UNKNOWN,
    };

    Work(int tid, unsigned frameId, unsigned callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL);
    virtual ~Work();
    int getThreadID();
    unsigned getFrameID();
    unsigned getCallID();
    const char* GetCallName()
    {
        return _callName;
    }
    virtual void run();
    void onFinished();
    void setStatus(WorkStatus s);
    WorkStatus getStatus() { return status; }
    void release() { int left = ref.fetch_sub(1); if (left == 0) delete this; }
    void retain() { ref++; }
    int  getRefCount() { return ref; }
    void dump();

    inline uint64_t GetExecutionTime()
    {
        return (end_time - start_time);
    }

    inline common::UnCompressedChunk* getChunkHandler()
    {
        return _chunk;
    }

    inline void SetMeasureTime(bool v)
    {
        isMeasureTime = v;
    }

private:
    int _tid;
    unsigned _frameId;
    unsigned _callId;
    void* _fptr;
    char* _src;
    const char* _callName;
    uint64_t    start_time;
    uint64_t    end_time;
    bool        isMeasureTime;
    std::atomic<WorkStatus> status;
    common::UnCompressedChunk * _chunk;
    std::atomic_int ref;
    os::Mutex workMutex;
};

class SnapshotWork : public Work
{
public:
    SnapshotWork(int tid, unsigned frameId, unsigned callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL)
        : Work(tid, frameId, callID, fptr, src, name, chunk) {}
    ~SnapshotWork() {}
    void run();
};

class DiscardFramebuffersWork : public Work
{
public:
    DiscardFramebuffersWork(int tid, unsigned frameId, unsigned callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL)
        : Work(tid, frameId, callID, fptr, src, name, chunk) {}
    ~DiscardFramebuffersWork() {}
    void run();
};

class PerfWork : public Work
{
public:
    PerfWork(int tid, unsigned frameId, unsigned callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL)
        : Work(tid, frameId, callID, fptr, src, name, chunk) {}
    ~PerfWork() {}
    void run();
};

class StepWork : public Work
{
public:
    StepWork(int tid, unsigned frameId, unsigned callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL)
        : Work(tid, frameId, callID, fptr, src, name, chunk) {}
    ~StepWork() {}
    void run();
};

class WorkThread : public os::Thread
{
public:
    enum WorkThreadStatus
    {
        IDLE = 0,
        RUNNING,
        TERMINATED,
        END,
        UNKNOWN,
    };

    enum { MAX_WORK_QUEUE_SIZE = 100, };
    WorkThread(common::InFile *file);
    ~WorkThread();
    void workQueuePush(Work *work);
    Work* workQueuePop() { return workQueue.pop(); }
    virtual void run();
    WorkThreadStatus getStatus() { return status; }
    void setStatus(WorkThreadStatus s) { status = s; }
    void terminate();
    void workQueueWakeup();
    void waitIdle();
    inline common::InFile* getFileHandler()
    {
        return file;
    }
    inline Work* GetCurWork()
    {
        return curWork;
    }

private:
    common::InFile *file;
    os::MTQueue<Work *> workQueue;
    os::Condition workThreadCond;
    std::atomic<WorkThreadStatus> status;
    Work* curWork;
};

typedef std::unordered_map<int, WorkThread*> WorkThreadPool_t;

class Retracer
{
public:
    Retracer();
    virtual ~Retracer();

    bool OpenTraceFile(const char* filename);
    bool overrideWithCmdOptions( const CmdOptions &cmdOptions );

    void CloseTraceFile();

    bool Retrace();

    // methods for debug purpose
    // Step forward specific number frames or drawcalls
    bool RetraceForward(unsigned int frameNum, unsigned int drawNum, bool dumpFrameBuffer = true);

    void CheckGlError();

    void reportAndAbort(const char *format, ...); // const NORETURN;
    void saveResult();
    bool addResultInformation();

    void OnFrameComplete();
    void OnNewFrame();
    void StartMeasuring();

    StateLogger& getStateLogger() { return mStateLogger; }

    Context& getCurrentContext();
    bool hasCurrentContext();

    common::HeaderVersion getFileFormatVersion() const { return mFileFormatVersion; }

    std::string changeAttributesToConstants(const std::string& source, const std::vector<VertexArrayInfo>& attributesToRemove);
    std::vector<Texture> getTexturesToDump();
    void TakeSnapshot(unsigned int callNo, unsigned int frameNo, const char *filename = NULL);
    void StepShot(unsigned int callNo, unsigned int frameNo, const char *filename = NULL);
    void dumpUniformBuffers(unsigned int callno);
    void createWorkThreadPool();
    void destroyWorkThreadPool();
    void waitWorkThreadPoolIdle();
    WorkThread* addWorkThread(unsigned tid);
    WorkThread* findWorkThread(unsigned tid);
    int getCurTid();
    unsigned GetCurCallId();
    void IncCurCallId();
    void ResetCurCallId();
    const char* GetCurCallName();
    unsigned GetCurDrawId();
    void IncCurDrawId();
    void ResetCurDrawId();
    unsigned GetCurFrameId();
    void IncCurFrameId();
    void ResetCurFrameId();
    Work*    GetCurWork(int tid);
    void wakeupAllWorkThreads();
    void DiscardFramebuffers();
    void PerfStart();
    void PerfEnd();
    void DispatchWork(Work* work);
    void DispatchWork(int tid, unsigned frameId, int callID, void* fptr, char* src, const char* name, common::UnCompressedChunk *chunk = NULL);
    inline void UpdateCallStats(const char* funcName, uint64_t time)
    {
        mCallStatsMutex.lock();
        mCallStats[funcName].count++;
        mCallStats[funcName].time += time;
        mCallStatsMutex.unlock();
    }

    common::InFile      mFile;
    RetraceOptions      mOptions;
    StateMgr            mState;

    common::BCall_vlen  mCurCall;
    bool                mOutOfMemory;
    bool                mFailedToLinkShaderProgram;
    bool volatile       mFinish;

    OffscreenManager*   mpOffscrMgr;
    common::ClientSideBufferObjectSet mCSBuffers;

    Quad*               mpQuad;

    unsigned int        mVBODataSize;
    unsigned int        mTextureDataSize;
    unsigned int        mCompressedTextureDataSize;
    unsigned int        mClientSideMemoryDataSize;
    std::unordered_map<std::string, int> mCallCounter;

    Json::Value         staticDump;
    Collection*         mCollectors;

    bool mMosaicNeedToBeFlushed = false;

private:
    bool loadRetraceOptionsByThreadId(int tid);
    void loadRetraceOptionsFromHeader();
    float getDuration(long long lastTime, long long* thisTime) const;
    float ticksToSeconds(long long t) const;
    void initializeCallCounter();

#ifndef _WIN32
    bool addMaliRegisterInformation();
#endif

    unsigned short mExIdEglSwapBuffers;
    unsigned short mExIdEglSwapBuffersWithDamage;

    long long           mEndFrameTime;
    long long           mTimerBeginTime;
    long long           mFinishSwapTime;

    StateLogger mStateLogger;
    common::HeaderVersion mFileFormatVersion;
    std::vector<std::string> mSnapshotPaths;

    struct CallStat
    {
        uint64_t count = 0;
        uint64_t time = 0;
    };
    std::unordered_map<std::string, CallStat> mCallStats;
    os::Mutex mCallStatsMutex;

    pid_t child = 0;

    WorkThreadPool_t workThreadPool;
    os::Mutex workThreadPoolMutex;
    WorkThread* preThread = nullptr;
    unsigned mCurCallNo = 0;
    unsigned mCurDrawNo = 0;
    unsigned mCurFrameNo = 0;
    unsigned mDispatchFrameNo = 0;
};

inline float Retracer::getDuration(long long lastTime, long long* thisTime) const
{
    long long now = os::getTime();
    float duration = ticksToSeconds(now - lastTime);
    *thisTime = now;
    return duration;
}

inline float Retracer::ticksToSeconds(long long t) const
{
    float oneOverFreq = 1.0f / os::timeFrequency;
    return t * oneOverFreq;
}

inline int Retracer::getCurTid()
{
    int map_tid = 0;

    if (!mOptions.mMultiThread)
    {
        map_tid = mCurCall.tid;
    }
    else
    {
        THREAD_HANDLE cur_tid = os::Thread::getCurrentThreadID();
        WorkThreadPool_t::const_iterator it;

        workThreadPoolMutex.lock();
        for (it = workThreadPool.begin(); it != workThreadPool.end(); it++)
        {
            WorkThread* wThread = it->second;
            if (wThread)
            {
                if (os::Thread::IsSameThread(cur_tid, wThread->getTid()))
                {
                    map_tid = it->first;
                    break;
                }
            }
        }
        if (it == workThreadPool.end())
        {
            map_tid = mCurCall.tid;
        }
        workThreadPoolMutex.unlock();
    }

    return map_tid;
}

inline unsigned Retracer::GetCurCallId()
{
    unsigned id = 0;

    if (!mOptions.mMultiThread)
    {
        id = mCurCallNo;
    }
    else
    {
        int tid = getCurTid();
        Work *work = GetCurWork(tid);
        if (work) // in case of a null pointer, there is no work in the queue at the beginning of retracing
        {
            id = work->getCallID();
        }
    }

    return id;
}

inline const char* Retracer::GetCurCallName()
{

    if (!mOptions.mMultiThread)
    {
        return mFile.ExIdToName(mCurCall.funcId);
    }
    else
    {
        int tid = getCurTid();
        Work *work = GetCurWork(tid);
        return work->GetCallName();
    }
}

inline void Retracer::IncCurCallId()
{
    mCurCallNo++;
}

inline void Retracer::ResetCurCallId()
{
    mCurCallNo = 0;
}

inline unsigned Retracer::GetCurDrawId()
{
    return mCurDrawNo;
}

inline void Retracer::IncCurDrawId()
{
    mCurDrawNo++;
}

inline void Retracer::ResetCurDrawId()
{
    mCurDrawNo = 0;
}

inline unsigned Retracer::GetCurFrameId()
{
    unsigned id = 0;

    if (!mOptions.mMultiThread)
    {
        id = mCurFrameNo;
    }
    else
    {
        int tid = getCurTid();
        Work *work = GetCurWork(tid);
        if (work)
        {
            id = work->getFrameID();
        }
    }

    return id;
}

inline void Retracer::IncCurFrameId()
{
    mCurFrameNo++;
}

inline void Retracer::ResetCurFrameId()
{
    mCurFrameNo = 0;
}

inline Work* Retracer::GetCurWork(int tid)
{
    Work *work = NULL;
    workThreadPoolMutex.lock();
    if (workThreadPool[tid])
    {
        work = workThreadPool[tid]->GetCurWork();
    }
    workThreadPoolMutex.unlock();
    return work;
}

inline Context& Retracer::getCurrentContext()
{
    int tid = getCurTid();
    Context* ctx = mState.mThreadArr[tid].getContext();
    if (!ctx)
    {
        reportAndAbort("No current context found for thread ID %d!", tid);
    }
    return *ctx;
}

inline bool Retracer::hasCurrentContext()
{
    int tid = getCurTid();
    return mState.mThreadArr[tid].getContext() != 0;
}

extern Retracer gRetracer;

void pre_glDraw();
void post_glLinkProgram(GLuint shader, GLuint originalShaderName, int status);
void post_glCompileShader(GLuint program, GLuint originalProgramName);
void hardcode_glBindFramebuffer(int target, unsigned int framebuffer);
void hardcode_glDeleteBuffers(int n, unsigned int* oldBuffers);
void hardcode_glDeleteFramebuffers(int n, unsigned int* oldBuffers);
void hardcode_glDeleteRenderbuffers(int n, unsigned int* oldBuffers);
void hardcode_glDeleteTextures(int n, unsigned int* oldTextures);
void hardcode_glDeleteTransformFeedbacks(int n, unsigned int* oldTransformFeedbacks);
void hardcode_glDeleteQueries(int n, unsigned int* oldQueries);
void hardcode_glDeleteSamplers(int n, unsigned int* oldSamplers);
void hardcode_glDeleteVertexArrays(int n, unsigned int* oldVertexArrays);
void forceRenderMosaicToScreen();

GLuint lookUpPolymorphic(GLuint name, GLenum target);

} /* namespace retracer */

#endif /* _RETRACE_HPP_ */
