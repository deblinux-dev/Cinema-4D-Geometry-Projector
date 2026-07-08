#pragma once
// Projection cache: 3-level cache (geometry -> projection -> raster)
// and global registry linking shader <-> projector object

#include "c4d.h"
#include "geometry_collector.h"
#include "geometry_projector.h"
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstring>
#include <cstdint>

// Cache update level required when settings change
enum class CacheUpdateLevel
{
    NONE        = 0,  // Everything up to date
    RASTER      = 1,  // Re-rasterize (drawing params changed)
    PROJECTION  = 2,  // Re-project (projection mode/settings changed)
    GEOMETRY    = 3   // Re-collect geometry (source objects changed)
};

// Dirty state snapshot for one source object
struct ObjectDirtyState
{
    Int32 matrixDirty = 0;
    Int32 dataDirty   = 0;
    Int32 cacheDirty  = 0;

    bool operator==(const ObjectDirtyState& o) const
    {
        return matrixDirty == o.matrixDirty &&
               dataDirty   == o.dataDirty   &&
               cacheDirty  == o.cacheDirty;
    }
    bool operator!=(const ObjectDirtyState& o) const { return !(*this == o); }
};

// 3-level projection cache
struct ProjectionCache
{
    // Level 1: collected geometry
    CollectedGeometry  geometry;
    bool               geometryValid = false;
    // Key: (Int64)(intptr_t)obj -- avoids 32-bit pointer truncation on 64-bit targets
    std::map<Int64, ObjectDirtyState> objectDirtyStates;

    // Level 2: projected 2D geometry
    ProjectedGeometry  projected;
    bool               projectionValid = false;
    UInt32             projectionHash  = 0;

    // Level 3: rasterized bitmap
    // bitmap is replaced by the projector thread (DoUpdate) and read by shader
    // render threads (Output -> RefreshSnapshot). The mutex guards all access
    // to the bitmap pointer so a reader never sees a half-freed bitmap.
    BaseBitmap*        bitmap          = nullptr;
    Int32              bitmapWidth     = 0;
    Int32              bitmapHeight    = 0;
    bool               rasterValid     = false;
    UInt32             rasterHash      = 0;
    std::mutex         bitmapMutex;

    // Incremented each time the bitmap is replaced; read by shader in InitRender
    std::atomic<Int32> version{0};

    ProjectionCache() {}
    ~ProjectionCache() { FreeBitmap(); }

    ProjectionCache(const ProjectionCache&) = delete;
    ProjectionCache& operator=(const ProjectionCache&) = delete;

    void FreeBitmap()
    {
        std::lock_guard<std::mutex> lock(bitmapMutex);
        if (bitmap)
        {
            BaseBitmap::Free(bitmap);
            bitmap       = nullptr;
            bitmapWidth  = 0;
            bitmapHeight = 0;
        }
    }

    void SetBitmap(BaseBitmap* bm)
    {
        // Free the old bitmap OUTSIDE the lock to avoid holding the mutex while
        // C4D's allocator runs (BaseBitmap::Free can be slow). We swap the
        // pointer atomically under the lock, then free the old one.
        BaseBitmap* oldBm = nullptr;
        {
            std::lock_guard<std::mutex> lock(bitmapMutex);
            oldBm  = bitmap;
            bitmap = bm;
            if (bm)
            {
                bitmapWidth  = bm->GetBw();
                bitmapHeight = bm->GetBh();
            }
            else
            {
                bitmapWidth  = 0;
                bitmapHeight = 0;
            }
        }
        if (oldBm) BaseBitmap::Free(oldBm);
    }

    // Thread-safe snapshot of the current bitmap's pixel data.
    // Called from shader render threads. Copies RGB pixels into 'outBuf'
    // (sized bw*bh*3). Returns false if no valid bitmap is available.
    // The caller is responsible for allocating outBuf with the returned size.
    bool SnapshotPixels(std::vector<uint8_t>& outBuf, Int32& outW, Int32& outH)
    {
        std::lock_guard<std::mutex> lock(bitmapMutex);
        if (!bitmap || !rasterValid) return false;
        Int32 bw = bitmap->GetBw();
        Int32 bh = bitmap->GetBh();
        if (bw <= 0 || bh <= 0) return false;

        outW = bw;
        outH = bh;
        outBuf.resize((size_t)bw * bh * 3);

        std::vector<uint8_t> rowBuf((size_t)bw * 3);
        for (Int32 y = 0; y < bh; y++)
        {
            bitmap->GetPixelCnt(0, y, bw, rowBuf.data(), 3, COLORMODE::RGB, PIXELCNT::NONE);
            memcpy(outBuf.data() + (size_t)y * bw * 3, rowBuf.data(), (size_t)bw * 3);
        }
        return true;
    }

    void InvalidateAll()
    {
        geometryValid   = false;
        projectionValid = false;
        rasterValid     = false;
    }

    // Determine what needs to be recomputed
    CacheUpdateLevel GetUpdateLevel(
        const std::map<Int64, ObjectDirtyState>& currentStates,
        UInt32 newProjectionHash,
        UInt32 newRasterHash,
        Int32  newResolution) const
    {
        if (!geometryValid || currentStates != objectDirtyStates)
            return CacheUpdateLevel::GEOMETRY;

        if (!projectionValid || newProjectionHash != projectionHash)
            return CacheUpdateLevel::PROJECTION;

        if (!rasterValid || newRasterHash != rasterHash || newResolution != bitmapWidth)
            return CacheUpdateLevel::RASTER;

        return CacheUpdateLevel::NONE;
    }
};

// Global registry: projector object ID -> cache
// Allows the shader to find the bitmap by the projector's cache ID
class CacheRegistry
{
public:
    static CacheRegistry& Instance()
    {
        static CacheRegistry instance;
        return instance;
    }

    // Return existing cache or create a new one for this ID
    ProjectionCache* GetOrCreate(Int64 objectId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_caches.find(objectId);
        if (it != m_caches.end())
            return it->second.get();

        auto cache = std::make_unique<ProjectionCache>();
        ProjectionCache* ptr = cache.get();
        m_caches[objectId] = std::move(cache);
        return ptr;
    }

    // Find cache by ID without creating (may return nullptr)
    ProjectionCache* Get(Int64 objectId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_caches.find(objectId);
        return (it != m_caches.end()) ? it->second.get() : nullptr;
    }

    // Remove cache when a projector object is deleted
    void Remove(Int64 objectId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_caches.erase(objectId);
    }

    // Call from PluginEnd() before C4D tears down its allocator
    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_caches.clear();
    }

private:
    CacheRegistry() {}
    std::mutex m_mutex;
    std::map<Int64, std::unique_ptr<ProjectionCache>> m_caches;
};

// Called from PluginEnd() -- defined in main.cpp
inline void ClearCacheRegistry()
{
    CacheRegistry::Instance().Clear();
}

// Generate session-wide unique IDs to link original nodes and render-clones
inline Int64 GenerateUniqueCacheID()
{
    static std::atomic<Int64> s_uniqueCounter{1};
    return s_uniqueCounter.fetch_add(1);
}

// ---- Dirty state helpers ----

inline ObjectDirtyState GetObjectDirtyState(BaseObject* obj)
{
    ObjectDirtyState s;
    if (obj)
    {
        s.matrixDirty = obj->GetDirty(DIRTYFLAGS::MATRIX);
        s.dataDirty   = obj->GetDirty(DIRTYFLAGS::DATA);
        s.cacheDirty  = obj->GetDirty(DIRTYFLAGS::CACHE);
    }
    return s;
}

// ---- Hash helpers ----

// Use memcpy for type-punning to avoid undefined behavior.
inline UInt32 HashFloat(Float f, UInt32 seed = 0)
{
    uint64_t bits = 0;
    static_assert(sizeof(f) == sizeof(bits), "Float must be 64-bit");
    memcpy(&bits, &f, sizeof(bits));
    UInt32 lo = (UInt32)(bits & 0xFFFFFFFF);
    UInt32 hi = (UInt32)(bits >> 32);
    UInt32 v  = lo ^ hi;
    return seed ^ (v + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

inline UInt32 HashInt(Int32 i, UInt32 seed = 0)
{
    return seed ^ ((UInt32)i + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

// Hash all fields that affect the projection step
inline UInt32 ComputeProjectionHash(const ProjectionSettings& s)
{
    UInt32 h = 0;
    h = HashInt(s.projMode,           h);
    h = HashFloat(s.projDirection.x,  h);
    h = HashFloat(s.projDirection.y,  h);
    h = HashFloat(s.projDirection.z,  h);
    h = HashInt(s.autoFit  ? 1 : 0,   h);
    h = HashInt(s.invertX  ? 1 : 0,   h);
    h = HashInt(s.invertY  ? 1 : 0,   h);
    h = HashFloat(s.uvOffsetX,        h);
    h = HashFloat(s.uvOffsetY,        h);
    h = HashFloat(s.uvScaleX,         h);
    h = HashFloat(s.uvScaleY,         h);
    h = HashFloat(s.uvRotation,       h);
    h = HashFloat(s.cameraMatrix.sqmat.v1.x, h);
    h = HashFloat(s.cameraMatrix.sqmat.v1.y, h);
    h = HashFloat(s.cameraMatrix.sqmat.v1.z, h);
    h = HashFloat(s.cameraMatrix.sqmat.v2.x, h);
    h = HashFloat(s.cameraMatrix.sqmat.v2.y, h);
    h = HashFloat(s.cameraMatrix.sqmat.v2.z, h);
    h = HashFloat(s.cameraMatrix.sqmat.v3.x, h);
    h = HashFloat(s.cameraMatrix.sqmat.v3.y, h);
    h = HashFloat(s.cameraMatrix.sqmat.v3.z, h);
    h = HashFloat(s.cameraMatrix.off.x, h);
    h = HashFloat(s.cameraMatrix.off.y, h);
    h = HashFloat(s.cameraMatrix.off.z, h);
    h = HashFloat(s.cameraFov,        h);
    return h;
}

// Hash all fields that affect the rasterization step
inline UInt32 ComputeRasterHash(const ProjectionSettings& s)
{
    UInt32 h = 0;
    h = HashFloat(s.lineWidth,         h);
    h = HashInt(s.drawFill    ? 1 : 0, h);
    h = HashInt(s.drawOutline ? 1 : 0, h);
    h = HashFloat(s.outlineWidth,      h);
    h = HashFloat(s.fgColor.x,        h);
    h = HashFloat(s.fgColor.y,        h);
    h = HashFloat(s.fgColor.z,        h);
    h = HashFloat(s.bgColor.x,        h);
    h = HashFloat(s.bgColor.y,        h);
    h = HashFloat(s.bgColor.z,        h);
    h = HashInt(s.useAlpha    ? 1 : 0, h);
    h = HashFloat(s.opacity,           h);
    h = HashInt(s.lineCapStyle,        h);
    h = HashInt(s.previewResolution,   h);
    return h;
}