#pragma once
// ProjectionShader -- ShaderData plugin
// Plugin ID: 1059891
//
// Output() is called from render threads and must be thread-safe.
// Real-time strategy: Output() lazily refreshes a private pixel snapshot from
// the projector's cache whenever the cache version changes. This makes the
// shader reflect geometry/camera edits immediately in the viewport, not just
// when C4D happens to call InitRender() (which only runs at render start).

#include "c4d.h"
#include "projection_cache.h"
#include <vector>
#include <mutex>

static const Int32 PLUGIN_ID_PROJECTION_SHADER = 1059891;

class ProjectionShader : public ShaderData
{
    INSTANCEOF(ProjectionShader, ShaderData)

public:
    static NodeData* Alloc();

    Bool Init(GeListNode* node) override;

    INITRENDERRESULT InitRender(BaseShader* sh, const InitRenderStruct& irs) override;
    void FreeRender(BaseShader* sh) override;

    // Called from render threads. Lazily refreshes the pixel snapshot from the
    // projector's cache (if its version changed), then samples it. All shared
    // state access is guarded by m_refreshMutex.
    Vector Output(BaseShader* sh, ChannelData* cd) override;

    SHADERINFO GetRenderInfo(BaseShader* sh) override;

private:
    // Pixel snapshot copied from the projector's cache bitmap (owned, immutable
    // between refreshes). Layout: row-major, 3 bytes/pixel (R,G,B), 0-255.
    std::vector<uint8_t> m_pixelData;
    Int32 m_bitmapWidth  = 0;
    Int32 m_bitmapHeight = 0;
    Int32 m_blendMode    = 0;

    // Version of the cache when the snapshot was last taken
    Int32 m_snapshotVersion = -1;

    // Cache ID resolved in InitRender (or lazily in Output). Identical for the
    // original projector and its render clones, so the shader always finds the
    // right cache via the global CacheRegistry.
    Int64 m_cacheId = 0;

    // Guards m_pixelData / m_bitmapWidth / m_bitmapHeight / m_snapshotVersion
    // against concurrent refresh from multiple render threads.
    std::mutex m_refreshMutex;

    // If the projector's cache bitmap version changed, re-copy its pixels into
    // m_pixelData. Called from Output() before sampling. Safe for concurrent
    // render threads (double-checked locking on m_snapshotVersion).
    void RefreshSnapshot(BaseShader* sh);

    // Sample the private pixel buffer at UV coordinates
    Vector SamplePixelData(Float u, Float v) const;

    // Blend sample onto base color according to m_blendMode
    Vector ApplyBlend(const Vector& base, const Vector& sample) const;

    // Gray checkerboard pattern shown when no bitmap is available
    static Vector CheckerPattern(Float u, Float v);
};

Bool RegisterProjectionShader();
