// Rasterizer implementation for Geometry Projector

#include "rasterizer.h"
#include <algorithm>
#include <map>
#include <tuple>

// ==================== UV line clipping (Cohen-Sutherland) ====================
// Clips a line segment to the [0..1] UV box so geometry outside the target
// object's bounds does not draw a strip at the bitmap edge.

static const int CS_INSIDE = 0, CS_LEFT = 1, CS_RIGHT = 2, CS_BOTTOM = 4, CS_TOP = 8;

static int CS_OutCode(Float u, Float v)
{
    int code = CS_INSIDE;
    if (u < 0.0) code |= CS_LEFT;
    else if (u > 1.0) code |= CS_RIGHT;
    if (v < 0.0) code |= CS_BOTTOM;
    else if (v > 1.0) code |= CS_TOP;
    return code;
}

static bool CS_ClipLine(Float& u0, Float& v0, Float& u1, Float& v1)
{
    int code0 = CS_OutCode(u0, v0);
    int code1 = CS_OutCode(u1, v1);

    while (true)
    {
        if (!(code0 | code1)) return true;   // Both inside
        if (code0 & code1)   return false;   // Both outside same region

        Float u, v;
        int codeOut = code0 ? code0 : code1;

        if (codeOut & CS_TOP)
        {
            u = u0 + (u1 - u0) * (1.0 - v0) / (v1 - v0);
            v = 1.0;
        }
        else if (codeOut & CS_BOTTOM)
        {
            u = u0 + (u1 - u0) * (0.0 - v0) / (v1 - v0);
            v = 0.0;
        }
        else if (codeOut & CS_RIGHT)
        {
            v = v0 + (v1 - v0) * (1.0 - u0) / (u1 - u0);
            u = 1.0;
        }
        else // CS_LEFT
        {
            v = v0 + (v1 - v0) * (0.0 - u0) / (u1 - u0);
            u = 0.0;
        }

        if (codeOut == code0) { u0 = u; v0 = v; code0 = CS_OutCode(u0, v0); }
        else                  { u1 = u; v1 = v; code1 = CS_OutCode(u1, v1); }
    }
}

// ==================== UV polygon clipping (Sutherland-Hodgman) ====================
// Clips a polygon to the [0..1] UV box. Without this, a polygon entirely
// outside the target bounds (e.g. a cube moved past the plane edge) still
// produced a 1-pixel strip at the bitmap border, because ScanlineFill's HLine
// clamped negative x coordinates to 0 and drew a single pixel there.

using UVPoint = std::pair<Float, Float>;

static void SH_ClipAgainstEdge(std::vector<UVPoint>& poly, Float u, Float v,
                                int edge, Float bound)
{
    std::vector<UVPoint> out;
    if (poly.empty()) return;
    Int32 n = (Int32)poly.size();
    for (Int32 i = 0; i < n; i++)
    {
        UVPoint cur  = poly[i];
        UVPoint prev = poly[(i - 1 + n) % n];

        // 'inside' test depends on which edge we clip against
        auto isInside = [&](const UVPoint& p) -> bool {
            switch (edge)
            {
                case 0: return p.first  >= bound; // left (u >= 0)
                case 1: return p.first  <= bound; // right (u <= 1)
                case 2: return p.second >= bound; // bottom (v >= 0)
                case 3: return p.second <= bound; // top (v <= 1)
            }
            return true;
        };

        bool curIn  = isInside(cur);
        bool prevIn = isInside(prev);

        if (curIn)
        {
            if (!prevIn)
            {
                // Entering: compute intersection
                Float du = cur.first - prev.first;
                Float dv = cur.second - prev.second;
                Float t;
                if (edge <= 1) t = (bound - prev.first)  / (du != 0.0 ? du : 1e-10);
                else           t = (bound - prev.second) / (dv != 0.0 ? dv : 1e-10);
                out.push_back({ prev.first  + t * du, prev.second + t * dv });
            }
            out.push_back(cur);
        }
        else if (prevIn)
        {
            // Leaving: compute intersection
            Float du = cur.first - prev.first;
            Float dv = cur.second - prev.second;
            Float t;
            if (edge <= 1) t = (bound - prev.first)  / (du != 0.0 ? du : 1e-10);
            else           t = (bound - prev.second) / (dv != 0.0 ? dv : 1e-10);
            out.push_back({ prev.first  + t * du, prev.second + t * dv });
        }
    }
    poly = std::move(out);
}

// Clip a UV polygon to [0..1] x [0..1]. Returns the clipped polygon (may be
// empty if the input was entirely outside).
static std::vector<UVPoint> SH_ClipPolygon(std::vector<UVPoint> poly)
{
    SH_ClipAgainstEdge(poly, 0, 0, 0, 0.0); // left:   u >= 0
    SH_ClipAgainstEdge(poly, 0, 0, 1, 1.0); // right:  u <= 1
    SH_ClipAgainstEdge(poly, 0, 0, 2, 0.0); // bottom: v >= 0
    SH_ClipAgainstEdge(poly, 0, 0, 3, 1.0); // top:    v <= 1
    return poly;
}

// ==================== Entry point ====================

BaseBitmap* Rasterizer::Rasterize(const ProjectedGeometry& projected,
                                    const CollectedGeometry& collected,
                                    const ProjectionSettings& settings)
{
    m_width    = settings.previewResolution;
    m_height   = settings.previewResolution;
    m_settings = settings;

    // Convert global colors to 0-255 (used for background, and as fallback
    // for polygon outlines which don't have per-object colors)
    m_bgR = ClampI((Int32)(settings.bgColor.x * 255.0), 0, 255);
    m_bgG = ClampI((Int32)(settings.bgColor.y * 255.0), 0, 255);
    m_bgB = ClampI((Int32)(settings.bgColor.z * 255.0), 0, 255);

    Int32 alpha = ClampI((Int32)(settings.opacity * 255.0), 0, 255);

    InitBuffers();

    if (projected.points2d.empty())
        return Flush();

    Int32 ptCount = (Int32)projected.points2d.size();

    // Convert UV points to pixel coordinates.
    // Y is flipped: UV v=0 is at the bottom, bitmap y=0 is at the top.
    std::vector<std::pair<Int32,Int32>> pixelPts;
    pixelPts.reserve(ptCount);
    for (Int32 i = 0; i < ptCount; i++)
    {
        Float u = projected.points2d[i].first;
        Float v = projected.points2d[i].second;
        pixelPts.push_back({ UtoX(u), VtoY(1.0 - v) });
    }

    // Helper: convert a Vector color to 0-255 RGB
    auto to255 = [](const Vector& c) -> std::tuple<Int32,Int32,Int32> {
        return std::make_tuple(
            ClampI((Int32)(c.x * 255.0), 0, 255),
            ClampI((Int32)(c.y * 255.0), 0, 255),
            ClampI((Int32)(c.z * 255.0), 0, 255)
        );
    };

    // 1. Fill polygons. An object can override the global Draw Fill setting
    // via its ProjectionSettingsTag (poly.fillOverride). When overridden and
    // poly.fill is true, fill even if global drawFill is false; when overridden
    // and false, skip even if global drawFill is true.
    auto shouldFill = [&](const CollectedPolygon& p) -> bool {
        if (p.fillOverride) return p.fill;
        return settings.drawFill;
    };

    for (const auto& poly : projected.polygons)
    {
        if ((Int32)poly.indices.size() < 3) continue;
        if (!shouldFill(poly)) continue;
        auto [r, g, b] = to255(poly.color);

        // Build UV polygon and clip to [0..1] so fills outside the target
        // bounds don't leave a 1-pixel strip at the bitmap edge.
        std::vector<UVPoint> uvPoly;
        uvPoly.reserve(poly.indices.size());
        for (Int32 idx : poly.indices)
        {
            if (idx >= 0 && idx < ptCount)
                uvPoly.push_back(projected.points2d[idx]);
        }
        uvPoly = SH_ClipPolygon(std::move(uvPoly));
        if (uvPoly.size() < 3) continue;

        std::vector<std::pair<Int32,Int32>> corners;
        corners.reserve(uvPoly.size());
        for (const auto& uv : uvPoly)
            corners.push_back({ UtoX(uv.first), VtoY(1.0 - uv.second) });
        ScanlineFill(corners, r, g, b, alpha);
    }

    // Fill closed splines (per-object override applies too)
    for (const auto& sp : projected.closedSplines)
    {
        if ((Int32)sp.indices.size() < 3) continue;
        if (!shouldFill(sp)) continue;
        auto [r, g, b] = to255(sp.color);

        std::vector<UVPoint> uvPoly;
        uvPoly.reserve(sp.indices.size());
        for (Int32 idx : sp.indices)
        {
            if (idx >= 0 && idx < ptCount)
                uvPoly.push_back(projected.points2d[idx]);
        }
        uvPoly = SH_ClipPolygon(std::move(uvPoly));
        if (uvPoly.size() < 3) continue;

        std::vector<std::pair<Int32,Int32>> corners;
        corners.reserve(uvPoly.size());
        for (const auto& uv : uvPoly)
            corners.push_back({ UtoX(uv.first), VtoY(1.0 - uv.second) });
        ScanlineFill(corners, r, g, b, alpha);
    }

    // 2. Draw edges. Two modes:
    //   - drawOutline = false (silhouette mode): draw only BOUNDARY edges
    //     (polygon edges belonging to exactly 1 polygon) + spline segments.
    //     This gives a clean outline without internal mesh lines.
    //   - drawOutline = true (all-edges mode): draw ALL polygon edges +
    //     spline segments. Useful for wireframe-style effects.
    // Boundary edges are computed by counting how many polygons share each
    // edge (sorted vertex pair). Edges with count == 1 are boundary.
    {
        // Build edge -> polygon count map from projected polygons.
        std::map<std::pair<Int32,Int32>, Int32> edgePolyCount;
        for (const auto& poly : projected.polygons)
        {
            Int32 n = (Int32)poly.indices.size();
            for (Int32 i = 0; i < n; i++)
            {
                Int32 a = poly.indices[i];
                Int32 b = poly.indices[(i + 1) % n];
                auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
                edgePolyCount[key]++;
            }
        }

        // Count connections per point (for cap decision).
        std::map<Int32, Int32> pointConnections;
        for (const auto& line : projected.lines)
        {
            pointConnections[line.v0]++;
            pointConnections[line.v1]++;
        }

        for (const auto& line : projected.lines)
        {
            Int32 ia = line.v0;
            Int32 ib = line.v1;
            if (ia < 0 || ia >= ptCount || ib < 0 || ib >= ptCount) continue;

            // Determine if this line should be drawn in the current mode.
            bool drawThisLine = false;
            if (line.isSpline)
            {
                // Spline segments are always drawn (they ARE the silhouette).
                drawThisLine = true;
            }
            else if (line.isPolygonEdge)
            {
                // Polygon edge: check if it's a boundary edge.
                auto key = (ia < ib) ? std::make_pair(ia, ib) : std::make_pair(ib, ia);
                Int32 cnt = edgePolyCount[key];
                if (settings.drawOutline)
                {
                    // All-edges mode: draw all polygon edges.
                    drawThisLine = true;
                }
                else
                {
                    // Silhouette mode: draw only boundary edges (cnt == 1).
                    drawThisLine = (cnt <= 1);
                }
            }
            else
            {
                // Non-polygon, non-spline line: always draw (shouldn't happen).
                drawThisLine = true;
            }

            if (!drawThisLine) continue;

            // UV-clip the line to [0..1] to avoid edge strips
            Float u0 = projected.points2d[ia].first;
            Float v0 = projected.points2d[ia].second;
            Float u1 = projected.points2d[ib].first;
            Float v1 = projected.points2d[ib].second;
            if (!CS_ClipLine(u0, v0, u1, v1)) continue;

            auto [r, g, b] = to255(line.color);
            Float thick = settings.ScaleLineWidth(line.thickness, m_width);

            // Caps: round caps at endpoints (connection count == 1).
            // For spline segments, caps at every vertex to fill corner gaps.
            bool capA = (pointConnections[ia] == 1) || line.isSpline;
            bool capB = (pointConnections[ib] == 1) || line.isSpline;

            DrawThickLine(UtoX(u0), VtoY(1.0 - v0),
                           UtoX(u1), VtoY(1.0 - v1),
                           r, g, b, alpha, thick, capA, capB);
        }
    }

    return Flush();
}


// ==================== Buffers ====================

void Rasterizer::InitBuffers()
{
    Int32 size = m_width * m_height;
    m_rgb.resize(size * 3);
    m_alpha.resize(size, 0);

    for (Int32 i = 0; i < size; i++)
    {
        m_rgb[i * 3 + 0] = (uint8_t)m_bgR;
        m_rgb[i * 3 + 1] = (uint8_t)m_bgG;
        m_rgb[i * 3 + 2] = (uint8_t)m_bgB;
    }
    // m_alpha starts at 0 (transparent background); foreground pixels set it to 'alpha'
}

void Rasterizer::SetPixel(Int32 x, Int32 y, Int32 r, Int32 g, Int32 b, Int32 a)
{
    if (x < 0 || x >= m_width || y < 0 || y >= m_height)
        return;
    Int32 idx = y * m_width + x;
    m_rgb[idx * 3 + 0] = (uint8_t)r;
    m_rgb[idx * 3 + 1] = (uint8_t)g;
    m_rgb[idx * 3 + 2] = (uint8_t)b;
    m_alpha[idx]        = (uint8_t)a;
}

void Rasterizer::HLine(Int32 y, Int32 x0, Int32 x1, Int32 r, Int32 g, Int32 b, Int32 a)
{
    if (y < 0 || y >= m_height) return;
    if (x0 > x1) { Int32 tmp = x0; x0 = x1; x1 = tmp; }
    x0 = ClampI(x0, 0, m_width - 1);
    x1 = ClampI(x1, 0, m_width - 1);

    Int32 base = y * m_width;
    for (Int32 x = x0; x <= x1; x++)
    {
        m_rgb[(base + x) * 3 + 0] = (uint8_t)r;
        m_rgb[(base + x) * 3 + 1] = (uint8_t)g;
        m_rgb[(base + x) * 3 + 2] = (uint8_t)b;
        m_alpha[base + x]          = (uint8_t)a;
    }
}

// ==================== Scanline fill ====================

void Rasterizer::ScanlineFill(const std::vector<std::pair<Int32,Int32>>& corners,
                                Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    Int32 n = (Int32)corners.size();
    if (n < 3) return;

    Int32 minY = corners[0].second;
    Int32 maxY = corners[0].second;
    for (Int32 i = 1; i < n; i++)
    {
        if (corners[i].second < minY) minY = corners[i].second;
        if (corners[i].second > maxY) maxY = corners[i].second;
    }
    minY = ClampI(minY, 0, m_height - 1);
    maxY = ClampI(maxY, 0, m_height - 1);

    for (Int32 y = minY; y <= maxY; y++)
    {
        std::vector<Int32> xs;

        for (Int32 i = 0; i < n; i++)
        {
            Int32 j  = (i + 1) % n;
            Int32 ay = corners[i].second;
            Int32 by = corners[j].second;
            Int32 ax = corners[i].first;
            Int32 bx = corners[j].first;

            if ((ay <= y && by > y) || (by <= y && ay > y))
            {
                Int32 x = ax + (Int32)((Float)(y - ay) * (Float)(bx - ax) / (Float)(by - ay));
                xs.push_back(x);
            }
        }

        std::sort(xs.begin(), xs.end());
        for (Int32 k = 0; k + 1 < (Int32)xs.size(); k += 2)
            HLine(y, xs[k], xs[k + 1], r, g, b, alpha);
    }
}

// ==================== Lines ====================

void Rasterizer::DrawLineBresenham(Int32 x0, Int32 y0, Int32 x1, Int32 y1,
                                     Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    Int32 dx = Abs(x1 - x0);
    Int32 dy = Abs(y1 - y0);
    Int32 sx = (x0 < x1) ? 1 : -1;
    Int32 sy = (y0 < y1) ? 1 : -1;
    Int32 err = dx - dy;

    while (true)
    {
        SetPixel(x0, y0, r, g, b, alpha);
        if (x0 == x1 && y0 == y1) break;
        Int32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void Rasterizer::DrawThickLine(Int32 x0, Int32 y0, Int32 x1, Int32 y1,
                                 Int32 r, Int32 g, Int32 b, Int32 alpha,
                                 Float thickness,
                                 bool drawStartCap, bool drawEndCap)
{
    if (thickness <= 1.0)
    {
        DrawLineBresenham(x0, y0, x1, y1, r, g, b, alpha);
        return;
    }

    Float dx  = (Float)(x1 - x0);
    Float dy  = (Float)(y1 - y0);
    Float len = Sqrt(dx * dx + dy * dy);

    if (len < 1e-6)
    {
        // Degenerate (zero-length) line -- draw a single cap
        DrawCap(x0, y0, (Int32)(thickness * 0.5 + 0.5), r, g, b, alpha);
        return;
    }

    Float half = thickness * 0.5;
    // Unit perpendicular to the line
    Float nx = -dy / len;
    Float ny =  dx / len;

    // Four corners of the line body
    Int32 ax = x0 + (Int32)(nx * half + 0.5);
    Int32 ay = y0 + (Int32)(ny * half + 0.5);
    Int32 bx = x0 - (Int32)(nx * half + 0.5);
    Int32 by = y0 - (Int32)(ny * half + 0.5);
    Int32 cx = x1 - (Int32)(nx * half + 0.5);
    Int32 cy = y1 - (Int32)(ny * half + 0.5);
    Int32 dx2= x1 + (Int32)(nx * half + 0.5);
    Int32 dy2= y1 + (Int32)(ny * half + 0.5);

    FillConvexQuad(ax, ay, bx, by, cx, cy, dx2, dy2, r, g, b, alpha);

    Int32 capRadius = (Int32)(half + 0.5);
    if (drawStartCap) DrawCap(x0, y0, capRadius, r, g, b, alpha);
    if (drawEndCap)   DrawCap(x1, y1, capRadius, r, g, b, alpha);
}

void Rasterizer::FillConvexQuad(Int32 ax, Int32 ay, Int32 bx, Int32 by,
                                  Int32 cx, Int32 cy, Int32 dx, Int32 dy,
                                  Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    std::vector<std::pair<Int32,Int32>> quad = {{ax,ay},{bx,by},{cx,cy},{dx,dy}};
    ScanlineFill(quad, r, g, b, alpha);
}

// ==================== Caps ====================

void Rasterizer::DrawCap(Int32 cx, Int32 cy, Int32 radius,
                           Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    if (m_settings.lineCapStyle == 1)  // LINE_CAP_SQUARE
        DrawFilledSquare(cx, cy, radius, r, g, b, alpha);
    else
        DrawFilledCircle(cx, cy, radius, r, g, b, alpha);
}

void Rasterizer::DrawFilledCircle(Int32 cx, Int32 cy, Int32 radius,
                                    Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    if (radius <= 0)
    {
        SetPixel(cx, cy, r, g, b, alpha);
        return;
    }

    // Midpoint circle algorithm filling horizontal spans
    Int32 x = radius;
    Int32 y = 0;
    Int32 err = 0;

    while (x >= y)
    {
        HLine(cy + y, cx - x, cx + x, r, g, b, alpha);
        HLine(cy - y, cx - x, cx + x, r, g, b, alpha);
        HLine(cy + x, cx - y, cx + y, r, g, b, alpha);
        HLine(cy - x, cx - y, cx + y, r, g, b, alpha);

        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0)
        {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void Rasterizer::DrawFilledSquare(Int32 cx, Int32 cy, Int32 half,
                                    Int32 r, Int32 g, Int32 b, Int32 alpha)
{
    for (Int32 y = cy - half; y <= cy + half; y++)
        HLine(y, cx - half, cx + half, r, g, b, alpha);
}

// ==================== Flush ====================

BaseBitmap* Rasterizer::Flush()
{
    BaseBitmap* bm = BaseBitmap::Alloc();
    if (!bm) return nullptr;

    // Always create a 24-bit bitmap.
    // For alpha support we add a separate channel rather than relying on
    // bm->Init(w, h, 32) producing COLORMODE::ARGB (not guaranteed on all platforms).
    IMAGERESULT ir = bm->Init(m_width, m_height, 24);
    if (ir != IMAGERESULT::OK)
    {
        BaseBitmap::Free(bm);
        return nullptr;
    }

    // Write RGB rows via SetPixelCnt (fast path)
    std::vector<uint8_t> rowBuf(m_width * 3);
    for (Int32 y = 0; y < m_height; y++)
    {
        Int32 base = y * m_width;
        for (Int32 x = 0; x < m_width; x++)
        {
            rowBuf[x * 3 + 0] = m_rgb[(base + x) * 3 + 0];
            rowBuf[x * 3 + 1] = m_rgb[(base + x) * 3 + 1];
            rowBuf[x * 3 + 2] = m_rgb[(base + x) * 3 + 2];
        }
        bm->SetPixelCnt(0, y, m_width, rowBuf.data(), 3, COLORMODE::RGB, PIXELCNT::NONE);
    }

    // Write alpha channel if requested
    if (m_settings.useAlpha)
    {
        // AddChannel(isAlpha=true, isPremultiplied=true) -- matches SDK convention
        BaseBitmap* alphaBm = bm->AddChannel(true, true);
        if (alphaBm)
        {
            std::vector<uint8_t> alphaRow(m_width);
            for (Int32 y = 0; y < m_height; y++)
            {
                Int32 base = y * m_width;
                for (Int32 x = 0; x < m_width; x++)
                    alphaRow[x] = m_alpha[base + x];
                alphaBm->SetPixelCnt(0, y, m_width, alphaRow.data(), 1, COLORMODE::GRAY, PIXELCNT::NONE);

            }
        }
    }

    return bm;
}
