// Geometry projector implementation for Geometry Projector

#include "geometry_projector.h"
#include "description/Ogeometryprojector.h"
#include <cmath>
#include <algorithm>
#include <limits>

// ==================== ProjectionSettings ====================

ProjectionSettings ProjectionSettings::FromContainer(BaseContainer* bc, Int32 resolution,
                                                      BaseObject* targetObj,
                                                      BaseDocument* doc)
{
    ProjectionSettings s;

    s.projMode        = bc->GetInt32(PROJ_MODE, PROJ_MODE_FLAT_Z);
    s.projDirection   = bc->GetVector(PROJ_DIRECTION, Vector(0,0,1));
    s.autoFit         = bc->GetBool(PROJ_AUTO_FIT, true);
    s.invertX         = bc->GetBool(PROJ_INVERT_X, false);
    s.invertY         = bc->GetBool(PROJ_INVERT_Y, true);

    s.uvOffsetX       = bc->GetFloat(UV_OFFSET_X, 0.0);
    s.uvOffsetY       = bc->GetFloat(UV_OFFSET_Y, 0.0);
    s.uvScaleX        = bc->GetFloat(UV_SCALE_X, 1.0);
    s.uvScaleY        = bc->GetFloat(UV_SCALE_Y, 1.0);
    s.uvRotation      = bc->GetFloat(UV_ROTATION, 0.0);

    s.lineWidth       = bc->GetFloat(LINE_WIDTH, 2.0);
    s.drawFill        = bc->GetBool(DRAW_FILL, false);
    s.drawOutline     = bc->GetBool(DRAW_OUTLINE, true);
    s.outlineWidth    = bc->GetFloat(OUTLINE_WIDTH, 1.0);
    s.fgColor         = bc->GetVector(FG_COLOR, Vector(1,1,1));
    s.bgColor         = bc->GetVector(BG_COLOR, Vector(0,0,0));
    s.useAlpha        = bc->GetBool(USE_ALPHA, false);
    s.opacity         = bc->GetFloat(OPACITY, 100.0) / 100.0;
    s.splineSubdivision = bc->GetInt32(SPLINE_SUBDIVISION, 4);
    s.lineCapStyle    = bc->GetInt32(LINE_CAP_STYLE, LINE_CAP_ROUND);

    s.previewEnabled  = bc->GetBool(PREVIEW_ENABLED, true);
    s.showBounds      = bc->GetBool(SHOW_BOUNDS, false);
    s.debugOutput     = bc->GetBool(DEBUG_OUTPUT, false);

    s.bakeWidth       = bc->GetInt32(BAKE_WIDTH, 1024);
    s.bakeHeight      = bc->GetInt32(BAKE_HEIGHT, 1024);
    s.bakeAA          = bc->GetInt32(BAKE_AA, BAKE_AA_NONE);
    s.bakeFormat      = bc->GetInt32(BAKE_FORMAT, FORMAT_PNG);
    s.bakePath        = bc->GetFilename(BAKE_PATH);

    s.previewResolution = resolution;

    s.scaledLineWidth    = s.ScaleLineWidth(s.lineWidth,    resolution);
    s.scaledOutlineWidth = s.ScaleLineWidth(s.outlineWidth, resolution);

    s.cameraMatrix.SetIdentity();
    s.cameraFov = 1.0;

    if (s.projMode == PROJ_MODE_CAMERA && doc)
    {
        BaseObject* cameraObj = nullptr;

        cameraObj = static_cast<BaseObject*>(bc->GetLink(PROJ_CAMERA_LINK, doc, Ocamera));

        if (!cameraObj)
        {
            BaseDraw* bd = doc->GetActiveBaseDraw();
            if (bd)
            {
                cameraObj = bd->GetSceneCamera(doc);
                if (!cameraObj)
                    cameraObj = bd->GetEditorCamera();
            }
        }

        if (cameraObj)
        {
            s.cameraMatrix = cameraObj->GetMg();
            BaseContainer* camBc = cameraObj->GetDataInstance();
            if (camBc)
                s.cameraFov = camBc->GetFloat(CAMERAOBJECT_FOV, 1.0);
        }
    }

    s.hasTargetBounds = false;
    if (targetObj && !s.autoFit)
    {
        s.targetBoundsCenter = targetObj->GetMp();
        s.targetBoundsRad    = targetObj->GetRad();
        s.hasTargetBounds    = true;
    }

    return s;
}

// ==================== GeometryProjector ====================

void GeometryProjector::Project(const CollectedGeometry& geometry,
                                  const ProjectionSettings& settings,
                                  ProjectedGeometry& outProjected)
{
    outProjected.Clear();

    if (geometry.points.empty())
        return;

    Int32 ptCount = (Int32)geometry.points.size();

    std::vector<std::pair<Float,Float>> pts2d;
    pts2d.reserve(ptCount);

    for (Int32 i = 0; i < ptCount; i++)
        pts2d.push_back(ProjectPoint(geometry.points[i], settings));

    Float minX = std::numeric_limits<Float>::max();
    Float minY = std::numeric_limits<Float>::max();
    Float maxX = -std::numeric_limits<Float>::max();
    Float maxY = -std::numeric_limits<Float>::max();

    for (const auto& pt : pts2d)
    {
        if (pt.first  < minX) minX = pt.first;
        if (pt.first  > maxX) maxX = pt.first;
        if (pt.second < minY) minY = pt.second;
        if (pt.second > maxY) maxY = pt.second;
    }

    outProjected.boundsMinX = minX;
    outProjected.boundsMinY = minY;
    outProjected.boundsMaxX = maxX;
    outProjected.boundsMaxY = maxY;

    Float rangeX = 0.0, rangeY = 0.0;

    if (settings.hasTargetBounds)
    {
        Float tMinX, tMinY, tRangeX, tRangeY;
        GetTargetUVBounds(settings, tMinX, tMinY, tRangeX, tRangeY);

        Float invRX = (tRangeX > 1e-10) ? (1.0 / tRangeX) : 1.0;
        Float invRY = (tRangeY > 1e-10) ? (1.0 / tRangeY) : 1.0;

        for (auto& pt : pts2d)
        {
            pt.first  = (pt.first  - tMinX) * invRX;
            pt.second = (pt.second - tMinY) * invRY;
        }
    }
    else
    {
        NormalizeAutoFit(pts2d, minX, minY, rangeX, rangeY);
    }

    for (auto& pt : pts2d)
    {
        if (settings.invertX) pt.first  = 1.0 - pt.first;
        if (settings.invertY) pt.second = 1.0 - pt.second;
    }

    for (auto& pt : pts2d)
    {
        auto t = ApplyTransform(pt.first, pt.second, settings);
        pt.first  = t.first;
        pt.second = t.second;
    }

    outProjected.points2d    = std::move(pts2d);
    outProjected.lines       = geometry.lines;
    outProjected.polygons    = geometry.polygons;
    outProjected.closedSplines = geometry.closed_splines;
}

std::pair<Float,Float> GeometryProjector::ProjectPoint(const Vector& pt,
                                                         const ProjectionSettings& settings)
{
    switch (settings.projMode)
    {
        case PROJ_MODE_FLAT_X: return {pt.z, pt.y};
        case PROJ_MODE_FLAT_Y: return {pt.x, pt.z};
        case PROJ_MODE_FLAT_Z: return {pt.x, pt.y};
        case PROJ_MODE_CAMERA: return ProjectCamera(pt, settings);
        case PROJ_MODE_CUSTOM: return ProjectCustom(pt, settings);
        default:               return {pt.x, pt.y};
    }
}

std::pair<Float,Float> GeometryProjector::ProjectCamera(const Vector& pt,
                                                          const ProjectionSettings& settings)
{
    Matrix invCam = ~settings.cameraMatrix;
    Vector lp = invCam * pt;

    // Оптимизация: Камера C4D направлена по +Z. Деление на положительный Z исправляет выворачивание пространства.
    Float lz = lp.z;
    if (Abs(lz) < 1e-10) lz = 1e-10;

    Float tanHalfFov = Tan(settings.cameraFov * 0.5);
    if (Abs(tanHalfFov) < 1e-10) tanHalfFov = 1e-10;

    return { lp.x / (lz * tanHalfFov), lp.y / (lz * tanHalfFov) };
}

std::pair<Float,Float> GeometryProjector::ProjectCustom(const Vector& pt,
                                                          const ProjectionSettings& settings)
{
    Vector dir = settings.projDirection;
    Float len = dir.GetLength();
    if (len < 1e-10) dir = Vector(0, 0, 1);
    else             dir = dir / len;

    Vector up(0, 1, 0);
    if (Abs(dir % up) > 0.999) up = Vector(1, 0, 0);

    Vector right = dir % up;
    right.Normalize();
    up = right % dir;
    up.Normalize();

    return { pt * right, pt * up };
}

void GeometryProjector::GetTargetUVBounds(const ProjectionSettings& settings,
                                            Float& outMinX, Float& outMinY,
                                            Float& outRangeX, Float& outRangeY)
{
    Vector center = settings.targetBoundsCenter;
    Vector rad    = settings.targetBoundsRad;

    Float minX =  std::numeric_limits<Float>::max();
    Float minY =  std::numeric_limits<Float>::max();
    Float maxX = -std::numeric_limits<Float>::max();
    Float maxY = -std::numeric_limits<Float>::max();

    for (Int32 ix = -1; ix <= 1; ix += 2)
    for (Int32 iy = -1; iy <= 1; iy += 2)
    for (Int32 iz = -1; iz <= 1; iz += 2)
    {
        Vector corner(center.x + ix * rad.x,
                      center.y + iy * rad.y,
                      center.z + iz * rad.z);
        auto pt = ProjectPoint(corner, settings);
        if (pt.first  < minX) minX = pt.first;
        if (pt.first  > maxX) maxX = pt.first;
        if (pt.second < minY) minY = pt.second;
        if (pt.second > maxY) maxY = pt.second;
    }

    outMinX   = minX;
    outMinY   = minY;
    outRangeX = maxX - minX;
    outRangeY = maxY - minY;

    if (outRangeX < 1e-10) outRangeX = 1.0;
    if (outRangeY < 1e-10) outRangeY = 1.0;
}

void GeometryProjector::NormalizeAutoFit(std::vector<std::pair<Float,Float>>& pts,
                                           Float& outMinX, Float& outMinY,
                                           Float& outRangeX, Float& outRangeY)
{
    if (pts.empty()) return;

    Float minX = std::numeric_limits<Float>::max();
    Float minY = std::numeric_limits<Float>::max();
    Float maxX = -std::numeric_limits<Float>::max();
    Float maxY = -std::numeric_limits<Float>::max();

    for (const auto& pt : pts)
    {
        if (pt.first  < minX) minX = pt.first;
        if (pt.first  > maxX) maxX = pt.first;
        if (pt.second < minY) minY = pt.second;
        if (pt.second > maxY) maxY = pt.second;
    }

    Float rangeX = maxX - minX;
    Float rangeY = maxY - minY;

    Float padX = rangeX * 0.02;
    Float padY = rangeY * 0.02;
    minX -= padX; maxX += padX;
    minY -= padY; maxY += padY;
    rangeX = maxX - minX;
    rangeY = maxY - minY;

    if (rangeX < 1e-10) rangeX = 1.0;
    if (rangeY < 1e-10) rangeY = 1.0;

    Float maxRange = (rangeX > rangeY) ? rangeX : rangeY;
    Float centerX  = (minX + maxX) * 0.5;
    Float centerY  = (minY + maxY) * 0.5;
    Float invMax   = 1.0 / maxRange;

    for (auto& pt : pts)
    {
        pt.first  = (pt.first  - centerX) * invMax + 0.5;
        pt.second = (pt.second - centerY) * invMax + 0.5;
    }

    outMinX   = minX;
    outMinY   = minY;
    outRangeX = rangeX;
    outRangeY = rangeY;
}

std::pair<Float,Float> GeometryProjector::ApplyTransform(Float u, Float v,
                                                           const ProjectionSettings& settings)
{
    Float su = settings.uvScaleX;
    Float sv = settings.uvScaleY;
    if (Abs(su) < 1e-10) su = 1e-10;
    if (Abs(sv) < 1e-10) sv = 1e-10;

    Float cu = (u - 0.5) / su;
    Float cv = (v - 0.5) / sv;

    Float rad  = -settings.uvRotation * PI / 180.0;
    Float cosA = Cos(rad);
    Float sinA = Sin(rad);
    Float ru   = cu * cosA - cv * sinA;
    Float rv   = cu * sinA + cv * cosA;

    return { ru + 0.5 + settings.uvOffsetX, rv + 0.5 + settings.uvOffsetY };
}
