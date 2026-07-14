#pragma once
// Rasterizer: converts ProjectedGeometry into a BaseBitmap

#include "c4d.h"
#include "geometry_projector.h"
#include "geometry_collector.h"
#include <vector>

// Forward declarations for UV-follow ray casting
class GeRayCollider;
class UVWTag;

class Rasterizer
{
public:
    // Rasterize projected geometry and return a new BaseBitmap (caller takes ownership).
    // Returns nullptr on allocation failure.
    BaseBitmap* Rasterize(const ProjectedGeometry& projected,
                           const CollectedGeometry& collected,
                           const ProjectionSettings& settings);

    // UV-follow two-stage pixel projection:
    //  Stage 1: orthographically render source geometry onto a plane
    //           perpendicular to source->target direction → temp bitmap
    //  Stage 2: for each filled pixel, ray-cast to target surface, sample UV,
    //           write color to the final bitmap at that UV position.
    // This produces a clean, undistorted silhouette that is seamless across
    // UV seams and doesn't jitter (no anchor UV dependency).
    // rayCollider must be pre-initialized with the target polygon object.
    BaseBitmap* RasterizeUVFollow(const CollectedGeometry& collected,
                                    const ProjectionSettings& settings,
                                    GeRayCollider* rayCollider,
                                    UVWTag* uvwTag,
                                    const Matrix& targetInvMg,
                                    const Vector& targetCenter);

private:
    Int32 m_width  = 0;
    Int32 m_height = 0;
    ProjectionSettings m_settings;

    // Foreground / background colors, 0-255 each channel
    Int32 m_fgR = 255, m_fgG = 255, m_fgB = 255;
    Int32 m_bgR = 0,   m_bgG = 0,   m_bgB = 0;

    // Pixel buffers (row-major)
    std::vector<uint8_t> m_rgb;    // width * height * 3
    std::vector<uint8_t> m_alpha;  // width * height

    // Fill buffers with background color
    void InitBuffers();

    // UV -> pixel coordinate conversion
    inline Int32 UtoX(Float u) const { return (Int32)(u * (Float)(m_width  - 1) + 0.5); }
    inline Int32 VtoY(Float v) const { return (Int32)(v * (Float)(m_height - 1) + 0.5); }

    // Write one pixel (with opacity applied via alpha channel)
    void SetPixel(Int32 x, Int32 y, Int32 r, Int32 g, Int32 b, Int32 a = 255);

    // Fill a horizontal span
    void HLine(Int32 y, Int32 x0, Int32 x1, Int32 r, Int32 g, Int32 b, Int32 a = 255);

    // Scanline polygon fill (arbitrary polygon, not necessarily convex)
    void ScanlineFill(const std::vector<std::pair<Int32,Int32>>& corners,
                       Int32 r, Int32 g, Int32 b, Int32 alpha);

    // 1-pixel Bresenham line
    void DrawLineBresenham(Int32 x0, Int32 y0, Int32 x1, Int32 y1,
                            Int32 r, Int32 g, Int32 b, Int32 alpha);

    // Thick line with optional end caps
    void DrawThickLine(Int32 x0, Int32 y0, Int32 x1, Int32 y1,
                        Int32 r, Int32 g, Int32 b, Int32 alpha,
                        Float thickness,
                        bool drawStartCap, bool drawEndCap);

    // Fill the convex quad that forms the body of a thick line
    void FillConvexQuad(Int32 ax, Int32 ay, Int32 bx, Int32 by,
                         Int32 cx, Int32 cy, Int32 dx, Int32 dy,
                         Int32 r, Int32 g, Int32 b, Int32 alpha);

    // Draw a line-end cap (dispatches by lineCapStyle)
    void DrawCap(Int32 cx, Int32 cy, Int32 radius,
                  Int32 r, Int32 g, Int32 b, Int32 alpha);

    void DrawFilledCircle(Int32 cx, Int32 cy, Int32 radius,
                           Int32 r, Int32 g, Int32 b, Int32 alpha);

    void DrawFilledSquare(Int32 cx, Int32 cy, Int32 half,
                           Int32 r, Int32 g, Int32 b, Int32 alpha);

    // Build the final BaseBitmap from pixel buffers
    BaseBitmap* Flush();

    inline static Int32 ClampI(Int32 v, Int32 lo, Int32 hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
};
