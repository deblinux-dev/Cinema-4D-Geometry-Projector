// Rasterizer implementation for Geometry Projector

#include "rasterizer.h"
#include <algorithm>
#include <map>

// ==================== Entry point ====================

BaseBitmap* Rasterizer::Rasterize(const ProjectedGeometry& projected,
                                    const CollectedGeometry& collected,
                                    const ProjectionSettings& settings)
{
    m_width    = settings.previewResolution;
    m_height   = settings.previewResolution;
    m_settings = settings;

    // Convert colors to 0-255
    m_fgR = ClampI((Int32)(settings.fgColor.x * 255.0), 0, 255);
    m_fgG = ClampI((Int32)(settings.fgColor.y * 255.0), 0, 255);
    m_fgB = ClampI((Int32)(settings.fgColor.z * 255.0), 0, 255);
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

    // 1. Fill polygons
    if (settings.drawFill)
    {
        for (const auto& poly : projected.polygons)
        {
            if ((Int32)poly.size() < 3) continue;
            std::vector<std::pair<Int32,Int32>> corners;
            corners.reserve(poly.size());
            for (Int32 idx : poly)
            {
                if (idx >= 0 && idx < ptCount)
                    corners.push_back(pixelPts[idx]);
            }
            ScanlineFill(corners, m_fgR, m_fgG, m_fgB, alpha);
        }

        // Fill closed splines
        for (const auto& sp : projected.closedSplines)
        {
            if ((Int32)sp.size() < 3) continue;
            std::vector<std::pair<Int32,Int32>> corners;
            corners.reserve(sp.size());
            for (Int32 idx : sp)
            {
                if (idx >= 0 && idx < ptCount)
                    corners.push_back(pixelPts[idx]);
            }
            ScanlineFill(corners, m_fgR, m_fgG, m_fgB, alpha);
        }
    }

    // 2. Polygon outlines (no caps)
    if (settings.drawOutline)
    {
        Float outThick = settings.scaledOutlineWidth;
        for (const auto& poly : projected.polygons)
        {
            Int32 n = (Int32)poly.size();
            if (n < 2) continue;
            for (Int32 i = 0; i < n; i++)
            {
                Int32 ia = poly[i];
                Int32 ib = poly[(i + 1) % n];
                if (ia < 0 || ia >= ptCount || ib < 0 || ib >= ptCount) continue;
                DrawThickLine(pixelPts[ia].first, pixelPts[ia].second,
                               pixelPts[ib].first, pixelPts[ib].second,
                               m_fgR, m_fgG, m_fgB, alpha,
                               outThick, false, false);
            }
        }
    }

    // 3. Edge lines with caps.
    // A cap is drawn only at true endpoints (connection count == 1).
    std::map<Int32, Int32> pointConnections;
    for (const auto& line : projected.lines)
    {
        pointConnections[line.first]++;
        pointConnections[line.second]++;
    }

    Float lineThick = settings.scaledLineWidth;
    for (const auto& line : projected.lines)
    {
        Int32 ia = line.first;
        Int32 ib = line.second;
        if (ia < 0 || ia >= ptCount || ib < 0 || ib >= ptCount) continue;

        bool capA = (pointConnections[ia] == 1);
        bool capB = (pointConnections[ib] == 1);

        DrawThickLine(pixelPts[ia].first, pixelPts[ia].second,
                       pixelPts[ib].first, pixelPts[ib].second,
                       m_fgR, m_fgG, m_fgB, alpha,
                       lineThick, capA, capB);
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
        bm->SetPixelCnt(0, y, m_width, rowBuf.data(), COLORMODE::RGB, PIXELCNT::NONE);
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
                alphaBm->SetPixelCnt(0, y, m_width, alphaRow.data(),
                                      COLORMODE::GRAY, PIXELCNT::NONE);
            }
        }
    }

    return bm;
}
