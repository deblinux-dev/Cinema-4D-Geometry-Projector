#pragma once
// ProjectionShader -- ShaderData plugin
// Plugin ID: 1059891
//
// Output() is called from render threads and must be thread-safe.
// Thread safety strategy: in InitRender() we take a snapshot of the current bitmap
// by copying all pixel data into a private buffer. Output() then reads only from
// this private buffer (no shared mutable state during rendering).

#include "c4d.h"
#include "projection_cache.h"
#include <vector>

static const Int32 PLUGIN_ID_PROJECTION_SHADER = 1059891;

class ProjectionShader : public ShaderData
{
    INSTANCEOF(ProjectionShader, ShaderData)

public:
    static NodeData* Alloc();

    Bool Init(GeListNode* node) override;

    INITRENDERRESULT InitRender(BaseShader* sh, const InitRenderStruct& irs) override;
    void FreeRender(BaseShader* sh) override;

    // Called from render threads -- accesses only m_pixelData (immutable after InitRender)
    Vector Output(BaseShader* sh, ChannelData* cd) override;

    SHADERINFO GetRenderInfo(BaseShader* sh) override;

private:
    // Pixel data copied from the bitmap in InitRender (owned, immutable during render)
    // Layout: row-major, 3 bytes per pixel (R, G, B), values 0-255
    std::vector<uint8_t> m_pixelData;
    Int32 m_bitmapWidth  = 0;
    Int32 m_bitmapHeight = 0;
    Int32 m_blendMode    = 0;

    // Version of the cache when InitRender ran -- allows detecting stale data
    Int32 m_snapshotVersion = -1;

    // Sample the private pixel buffer at UV coordinates
    Vector SamplePixelData(Float u, Float v) const;

    // Blend sample onto base color according to m_blendMode
    Vector ApplyBlend(const Vector& base, const Vector& sample) const;

    // Gray checkerboard pattern shown when no bitmap is available
    static Vector CheckerPattern(Float u, Float v);
};

Bool RegisterProjectionShader();
