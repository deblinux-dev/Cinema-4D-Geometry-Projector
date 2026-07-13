// Geometry projector implementation for Geometry Projector

#include "geometry_projector.h"
#include "description/Ogeometryprojector.h"
#include "c4d_basetag.h"
#include "c4d_libs/lib_collider.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <set>

// ==================== ProjectionSettings ====================

ProjectionSettings ProjectionSettings::FromContainer(BaseContainer* bc, Int32 resolution,
                                                      BaseObject* targetObj,
                                                      BaseDocument* doc,
                                                      const std::vector<BaseObject*>& sourceObjs)
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
    s.targetSurfaceObj = targetObj;

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

    // autoFit = true  -> project geometry onto the target object's WORLD-space
    //                    bounding box, preserving real proportions (a 1x1 cube
    //                    on a 10x10 plane occupies 0.1x0.1 of UV).
    // autoFit = false -> stretch the geometry's own bounds to fill [0..1] UV
    //                    (user-driven layout via UV Offset/Scale).
    // Note: GetMp()/GetRad() return LOCAL-space bounds, so they must be
    // transformed through the target's world matrix (position + scale + rotation).
    s.hasTargetBounds = false;
    if (s.autoFit)
    {
        if (targetObj)
        {
            // Explicit target object: use its world-space bounding box.
            Matrix mg = targetObj->GetMg();
            s.targetBoundsCenter = mg * targetObj->GetMp();
            Vector rad = targetObj->GetRad();
            s.targetBoundsRad = Vector(
                rad.x * mg.sqmat.v1.GetLength(),
                rad.y * mg.sqmat.v2.GetLength(),
                rad.z * mg.sqmat.v3.GetLength()
            );
            s.hasTargetBounds = true;
        }
        else if (!sourceObjs.empty())
        {
            // No target object: compute the combined world-space bounding box
            // of ALL source objects so that every object fits inside [0..1] UV.
            // This is the "flat projection plane sized to fit all geometry" mode.
            Vector bmin( std::numeric_limits<Float>::max());
            Vector bmax(-std::numeric_limits<Float>::max());
            for (BaseObject* obj : sourceObjs)
            {
                if (!obj) continue;
                Matrix mg = obj->GetMg();
                Vector mp = obj->GetMp();
                Vector rad = obj->GetRad();
                // 8 corners of the local AABB, transformed to world space
                for (Int32 ix = -1; ix <= 1; ix += 2)
                for (Int32 iy = -1; iy <= 1; iy += 2)
                for (Int32 iz = -1; iz <= 1; iz += 2)
                {
                    Vector corner = mp + Vector(rad.x * ix, rad.y * iy, rad.z * iz);
                    Vector w = mg * corner;
                    if (w.x < bmin.x) bmin.x = w.x;
                    if (w.y < bmin.y) bmin.y = w.y;
                    if (w.z < bmin.z) bmin.z = w.z;
                    if (w.x > bmax.x) bmax.x = w.x;
                    if (w.y > bmax.y) bmax.y = w.y;
                    if (w.z > bmax.z) bmax.z = w.z;
                }
            }
            s.targetBoundsCenter = (bmin + bmax) * 0.5;
            s.targetBoundsRad    = (bmax - bmin) * 0.5;
            s.hasTargetBounds = true;
        }
    }

    return s;
}

// ==================== GeometryProjector ====================

void GeometryProjector::Project(const CollectedGeometry& geometry,
                                  const ProjectionSettings& settings,
                                  ProjectedGeometry& outProjected)
{
    outProjected.Clear();
    m_geometry = &geometry;

    if (geometry.points.empty())
        return;

    Int32 ptCount = (Int32)geometry.points.size();

    std::vector<std::pair<Float,Float>> pts2d;
    pts2d.reserve(ptCount);

    // UV-follow mode: ray-cast the SOURCE CENTER to the target to find the
    // anchor UV, then flat-project all points (preserving the source's shape)
    // and place them at the anchor UV. This produces a clean, undistorted
    // silhouette on the target surface -- a square stays a square, a circle
    // stays a circle -- just positioned at the right UV spot.
    if (settings.projMode == PROJ_MODE_UVFOLLOW)
    {
        // Compute source center (centroid of all source points).
        Vector sourceCenter(0);
        for (Int32 i = 0; i < ptCount; i++)
            sourceCenter += geometry.points[i];
        if (ptCount > 0) sourceCenter /= (Float)ptCount;

        if (!m_uvFollowReady)
        {
            m_uvFollowReady = InitUVFollow(settings, sourceCenter, geometry.points);
            if (!m_uvFollowReady)
                return;
        }

        for (Int32 i = 0; i < ptCount; i++)
            pts2d.push_back(ProjectUVFollow(geometry.points[i], settings));

        // Skip normalize/autofit/transform -- UV is already in [0..1].
        outProjected.boundsMinX = 0.0; outProjected.boundsMinY = 0.0;
        outProjected.boundsMaxX = 1.0; outProjected.boundsMaxY = 1.0;
        outProjected.points2d    = std::move(pts2d);
        outProjected.lines       = geometry.lines;
        outProjected.polygons    = geometry.polygons;
        outProjected.closedSplines = geometry.closed_splines;
        ApplyClipping(outProjected);

        // Free the ray collider (allocated per Project() call). The UVWTag
        // pointer is not owned (it belongs to the target object).
        if (m_rayCollider)
        {
            GeRayCollider* rc = static_cast<GeRayCollider*>(m_rayCollider);
            GeRayCollider::Free(rc);
            m_rayCollider = nullptr;
        }
        m_uvwTag = nullptr;
        m_uvFollowReady = false;
        return;
    }

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

    // ----- Clipping pass -----
    // For each object that has clip sources (read from the ProjectionSettingsTag),
    // build the combined projected silhouette of its clip sources and clip the
    // object's own polygons + closed splines + lines against it.
    // Silhouettes are built from the (already projected) polygons/closed_splines
    // of the clip source objects, so nesting works automatically: a clip source
    // that itself has a clip source already had its silhouette clipped above
    // (we process owners in an order that resolves dependencies first).
    ApplyClipping(outProjected);
}

// Build the combined 2D silhouette (list of UV polygons) for a set of owner
// objects, using their already-projected polygons and closed splines.
static void BuildOwnerSilhouette(const ProjectedGeometry& proj,
                                  const std::vector<BaseObject*>& owners,
                                  std::vector<std::vector<std::pair<Float,Float>>>& outPolys)
{
    for (BaseObject* owner : owners)
    {
        if (!owner) continue;
        for (const auto& poly : proj.polygons)
        {
            if (poly.ownerObj != owner) continue;
            std::vector<std::pair<Float,Float>> uv;
            uv.reserve(poly.indices.size());
            for (Int32 idx : poly.indices)
            {
                if (idx >= 0 && idx < (Int32)proj.points2d.size())
                    uv.push_back(proj.points2d[idx]);
            }
            if (uv.size() >= 3) outPolys.push_back(std::move(uv));
        }
        for (const auto& sp : proj.closedSplines)
        {
            if (sp.ownerObj != owner) continue;
            std::vector<std::pair<Float,Float>> uv;
            uv.reserve(sp.indices.size());
            for (Int32 idx : sp.indices)
            {
                if (idx >= 0 && idx < (Int32)proj.points2d.size())
                    uv.push_back(proj.points2d[idx]);
            }
            if (uv.size() >= 3) outPolys.push_back(std::move(uv));
        }
    }
}

// Sutherland-Hodgman clip of a polygon against a single convex clip polygon.
// (Clip sources are assumed convex; for non-convex boundaries the result may
// be approximate but still usable as a mask.)
static std::vector<std::pair<Float,Float>> SH_ClipPolyVsPoly(
    const std::vector<std::pair<Float,Float>>& subject,
    const std::vector<std::pair<Float,Float>>& clip)
{
    if (clip.size() < 3) return subject;
    std::vector<std::pair<Float,Float>> out = subject;
    Int32 cn = (Int32)clip.size();
    for (Int32 i = 0; i < cn; i++)
    {
        if (out.empty()) break;
        Int32 j = (i + 1) % cn;
        Float ax = clip[i].first,  ay = clip[i].second;
        Float bx = clip[j].first,  by = clip[j].second;
        // edge direction
        Float ex = bx - ax, ey = by - ay;
        std::vector<std::pair<Float,Float>> next;
        Int32 n = (Int32)out.size();
        for (Int32 k = 0; k < n; k++)
        {
            auto cur = out[k];
            auto prev = out[(k - 1 + n) % n];
            // inside = left side of edge a->b (cross > 0)
            auto cross = [&](const std::pair<Float,Float>& p) -> Float {
                return (ex) * (p.second - ay) - (ey) * (p.first - ax);
            };
            bool curIn  = cross(cur)  >= 0.0;
            bool prevIn = cross(prev) >= 0.0;
            if (curIn)
            {
                if (!prevIn)
                {
                    Float t = cross(prev) / (cross(prev) - cross(cur));
                    next.push_back({ prev.first + t * (cur.first - prev.first),
                                     prev.second + t * (cur.second - prev.second) });
                }
                next.push_back(cur);
            }
            else if (prevIn)
            {
                Float t = cross(prev) / (cross(prev) - cross(cur));
                next.push_back({ prev.first + t * (cur.first - prev.first),
                                 prev.second + t * (cur.second - prev.second) });
            }
        }
        out = std::move(next);
    }
    return out;
}

// Check if a 2D point is inside any of the clip polygons (point-in-polygon).
static bool PointInAnyPoly(Float x, Float y,
                            const std::vector<std::vector<std::pair<Float,Float>>>& polys)
{
    for (const auto& poly : polys)
    {
        Int32 n = (Int32)poly.size();
        if (n < 3) continue;
        bool inside = false;
        for (Int32 i = 0, j = n - 1; i < n; j = i++)
        {
            Float xi = poly[i].first,  yi = poly[i].second;
            Float xj = poly[j].first,  yj = poly[j].second;
            if (((yi > y) != (yj > y)) &&
                (x < (xj - xi) * (y - yi) / ((yj - yi) != 0.0 ? (yj - yi) : 1e-10) + xi))
                inside = !inside;
        }
        if (inside) return true;
    }
    return false;
}

void GeometryProjector::ApplyClipping(ProjectedGeometry& proj)
{
    // Nothing to do if no clip sources were recorded.
    if (!m_geometry) return;
    if (m_geometry->clipSources.empty()) return;
    if (proj.polygons.empty() && proj.lines.empty() && proj.closedSplines.empty()) return;

    // Process owners in dependency order: an owner whose clip sources include
    // another owner that ALSO has clip sources must be processed after that
    // other owner's silhouette is finalized. We do a simple iterative
    // topological resolution: repeat until no changes.
    std::vector<BaseObject*> ownersWithClips;
    for (const auto& kv : m_geometry->clipSources)
        ownersWithClips.push_back(kv.first);

    // For each owner, build its clip silhouette and clip its own geometry.
    // Because silhouettes are read from proj.polygons/closedSplines (which we
    // mutate in place), processing order matters. We iterate owners and for
    // each, only clip once its clip sources have no pending clips themselves
    // (or have already been processed). A few passes suffice for typical depth.
    std::set<BaseObject*> done;
    for (Int32 pass = 0; pass < 16 && (Int32)done.size() < (Int32)ownersWithClips.size(); pass++)
    {
        for (BaseObject* owner : ownersWithClips)
        {
            if (done.count(owner)) continue;
            auto it = m_geometry->clipSources.find(owner);
            if (it == m_geometry->clipSources.end()) { done.insert(owner); continue; }
            const std::vector<BaseObject*>& clips = it->second;
            // Check all clip sources are either clip-free or already done.
            bool ready = true;
            for (BaseObject* c : clips)
                if (m_geometry->clipSources.count(c) && !done.count(c)) { ready = false; break; }
            if (!ready) continue;

            // Build combined silhouette of clip sources (from current proj state,
            // which may already be clipped for nested sources).
            std::vector<std::vector<std::pair<Float,Float>>> sil;
            BuildOwnerSilhouette(proj, clips, sil);
            if (sil.empty()) { done.insert(owner); continue; }

            // 1. Clip this owner's polygons (fill) against the silhouette.
            std::vector<CollectedPolygon> newPolys;
            for (auto& poly : proj.polygons)
            {
                if (poly.ownerObj != owner) { newPolys.push_back(poly); continue; }
                std::vector<std::pair<Float,Float>> uv;
                uv.reserve(poly.indices.size());
                for (Int32 idx : poly.indices)
                    if (idx >= 0 && idx < (Int32)proj.points2d.size())
                        uv.push_back(proj.points2d[idx]);
                // Intersect with union of silhouette polys (for convex polys,
                // intersecting each keeps the intersection; for a union we'd
                // need to collect all results. Here we clip against each poly
                // and keep the result of the first that yields a non-empty
                // polygon, which is a reasonable approximation for typical
                // single-boundary use cases like eye/mouth clipping.)
                std::vector<std::pair<Float,Float>> best = uv;
                bool kept = false;
                for (const auto& cp : sil)
                {
                    auto clipped = SH_ClipPolyVsPoly(uv, cp);
                    if (clipped.size() >= 3)
                    {
                        // Add as a NEW polygon (split) so multi-region coverage works.
                        CollectedPolygon np = poly;
                        // Need new point indices. We append the clipped UV as
                        // new points and reference them.
                        Int32 base = (Int32)proj.points2d.size();
                        np.indices.clear();
                        for (const auto& p : clipped)
                        {
                            proj.points2d.push_back(p);
                            np.indices.push_back(base++);
                        }
                        newPolys.push_back(np);
                        kept = true;
                    }
                }
                if (!kept)
                {
                    // Fully outside all clip polys -> discard (don't push).
                }
            }
            proj.polygons = std::move(newPolys);

            // 2. Clip closed splines the same way (treat as fill polygons).
            std::vector<CollectedPolygon> newSp;
            for (auto& sp : proj.closedSplines)
            {
                if (sp.ownerObj != owner) { newSp.push_back(sp); continue; }
                std::vector<std::pair<Float,Float>> uv;
                uv.reserve(sp.indices.size());
                for (Int32 idx : sp.indices)
                    if (idx >= 0 && idx < (Int32)proj.points2d.size())
                        uv.push_back(proj.points2d[idx]);
                bool kept = false;
                for (const auto& cp : sil)
                {
                    auto clipped = SH_ClipPolyVsPoly(uv, cp);
                    if (clipped.size() >= 3)
                    {
                        CollectedPolygon np = sp;
                        Int32 base = (Int32)proj.points2d.size();
                        np.indices.clear();
                        for (const auto& p : clipped)
                        {
                            proj.points2d.push_back(p);
                            np.indices.push_back(base++);
                        }
                        newSp.push_back(np);
                        kept = true;
                    }
                }
            }
            proj.closedSplines = std::move(newSp);

            // 3. Clip lines: keep a segment only if at least one endpoint is
            // inside the silhouette. (Full segment clipping would be more
            // accurate but this is simple and visually adequate for outlines.)
            std::vector<CollectedLine> newLines;
            newLines.reserve(proj.lines.size());
            for (const auto& line : proj.lines)
            {
                if (line.ownerObj != owner) { newLines.push_back(line); continue; }
                if (line.v0 < 0 || line.v0 >= (Int32)proj.points2d.size() ||
                    line.v1 < 0 || line.v1 >= (Int32)proj.points2d.size())
                    continue;
                const auto& p0 = proj.points2d[line.v0];
                const auto& p1 = proj.points2d[line.v1];
                bool in0 = PointInAnyPoly(p0.first, p0.second, sil);
                bool in1 = PointInAnyPoly(p1.first, p1.second, sil);
                if (in0 || in1)
                    newLines.push_back(line);
            }
            proj.lines = std::move(newLines);

            done.insert(owner);
        }
    }
}

// ==================== UV Follow ====================

Bool GeometryProjector::InitUVFollow(const ProjectionSettings& settings,
                                      const Vector& sourceCenter,
                                      const std::vector<Vector>& sourcePoints)
{
    // Clean up any previous state (in case Project() is called twice).
    if (m_rayCollider)
    {
        GeRayCollider* rc = static_cast<GeRayCollider*>(m_rayCollider);
        GeRayCollider::Free(rc);
        m_rayCollider = nullptr;
    }
    m_uvwTag      = nullptr;
    m_uvFollowReady = false;

    if (!settings.targetSurfaceObj) return false;

    // UV-follow needs a polygon object with a UVW tag. If the target is a
    // generator (e.g. a parametric Sphere), walk its cache to find the
    // generated PolygonObject.
    BaseObject* surfObj = settings.targetSurfaceObj;
    if (!surfObj->IsInstanceOf(Opolygon))
    {
        BaseObject* cache = surfObj->GetCache();
        while (cache && !cache->IsInstanceOf(Opolygon))
            cache = cache->GetCache();
        if (!cache)
        {
            cache = surfObj->GetDown();
            while (cache && !cache->IsInstanceOf(Opolygon))
                cache = cache->GetNext();
        }
        if (cache) surfObj = cache;
        else return false;
    }
    if (!surfObj->IsInstanceOf(Opolygon)) return false;

    BaseTag* tag = surfObj->GetTag(Tuvw);
    if (!tag) return false;
    m_uvwTag = tag;

    GeRayCollider* rc = GeRayCollider::Alloc();
    if (!rc) return false;
    if (!rc->Init(surfObj, true))
    {
        GeRayCollider::Free(rc);
        return false;
    }
    m_rayCollider = rc;

    Matrix mg = surfObj->GetMg();
    m_targetInvMg = ~mg;
    m_targetCenter = mg * surfObj->GetMp();

    // ---- Compute flat projection basis ----
    Vector fwd = m_targetCenter - sourceCenter;
    Float fwdLen = fwd.GetLength();
    if (fwdLen < 1e-7) fwd = Vector(0, 0, 1);
    else fwd = fwd / fwdLen;

    m_flatRight = Cross(Vector(0, 1, 0), fwd);
    if (m_flatRight.GetLength() < 1e-6)
        m_flatRight = Cross(Vector(1, 0, 0), fwd);
    m_flatRight.Normalize();
    m_flatUp = Cross(fwd, m_flatRight);
    m_flatUp.Normalize();
    m_flatOrigin = sourceCenter;

    // Size the flat silhouette to the source's own extent (so it keeps its
    // real proportions) rather than the target diameter.
    Float srcMinX =  std::numeric_limits<Float>::max();
    Float srcMinY =  std::numeric_limits<Float>::max();
    Float srcMaxX = -std::numeric_limits<Float>::max();
    Float srcMaxY = -std::numeric_limits<Float>::max();
    for (const Vector& p : sourcePoints)
    {
        Vector d = p - m_flatOrigin;
        Float fx = Dot(d, m_flatRight);
        Float fy = Dot(d, m_flatUp);
        if (fx < srcMinX) srcMinX = fx;
        if (fx > srcMaxX) srcMaxX = fx;
        if (fy < srcMinY) srcMinY = fy;
        if (fy > srcMaxY) srcMaxY = fy;
    }
    m_flatRangeX = (srcMaxX - srcMinX > 1e-6) ? (srcMaxX - srcMinX) : 1.0;
    m_flatRangeY = (srcMaxY - srcMinY > 1e-6) ? (srcMaxY - srcMinY) : 1.0;
    m_flatCentroidX = (srcMinX + srcMaxX) * 0.5;
    m_flatCentroidY = (srcMinY + srcMaxY) * 0.5;

    // ---- Compute anchor UV as the UV centroid of ray-hit points ----
    // Ray-cast ALL source points (not just the center) and average their hit
    // UVs. This is far more stable than ray-casting a single point: when the
    // source crosses a polygon boundary, only ONE point's UV jumps, while the
    // centroid of dozens of hits stays smooth. To avoid the averaging being
    // corrupted by UV-seam splits (hits on u=0.99 vs u=0.01), we wrap each
    // hit's UV delta relative to the first hit to the [-0.5..0.5] range
    // before accumulating.
    m_anchorU = 0.5; m_anchorV = 0.5;
    {
        Vector localSrc = m_targetInvMg * sourceCenter;
        Vector localTgt = m_targetInvMg * m_targetCenter;
        Vector dir = localTgt - localSrc;
        Float len = dir.GetLength();
        if (len >= 1e-7)
        {
            dir = dir / len;
            Float rayLen = len * 2.0 + 1000.0;

            Float sumU = 0.0, sumV = 0.0;
            Int32 hitCount = 0;
            Float refU = 0.0, refV = 0.0;
            bool haveRef = false;

            for (const Vector& p : sourcePoints)
            {
                Vector localP = m_targetInvMg * p;
                if (!rc->Intersect(localP, dir, rayLen, false)) continue;
                GeRayColResult res;
                if (!rc->GetNearestIntersection(&res)) continue;
                if (res.face_id < 0) continue;

                UVWStruct us = static_cast<UVWTag*>(m_uvwTag)->GetSlow(res.face_id);
                Float u = res.barrycoords.x;
                Float v = res.barrycoords.y;
                Bool secondHalf = (res.tri_face_id < 0);
                Vector uvw;
                if (!secondHalf)
                    uvw = us.a + (us.b - us.a) * u + (us.c - us.a) * v;
                else
                    uvw = us.a + (us.c - us.a) * u + (us.d - us.a) * v;

                if (!haveRef)
                {
                    refU = uvw.x; refV = uvw.y;
                    sumU = uvw.x; sumV = uvw.y;
                    haveRef = true;
                }
                else
                {
                    // Wrap delta to [-0.5..0.5] so seam-crossing hits don't
                    // corrupt the centroid.
                    Float du = uvw.x - refU;
                    Float dv = uvw.y - refV;
                    du = du - std::floor(du + 0.5);
                    dv = dv - std::floor(dv + 0.5);
                    sumU += refU + du;
                    sumV += refV + dv;
                }
                hitCount++;
            }

            if (hitCount > 0)
            {
                m_anchorU = sumU / (Float)hitCount;
                m_anchorV = sumV / (Float)hitCount;
                // Wrap to [0..1]
                m_anchorU = m_anchorU - std::floor(m_anchorU);
                m_anchorV = m_anchorV - std::floor(m_anchorV);
            }
        }
    }

    m_uvFollowReady = true;
    return true;
}

std::pair<Float,Float> GeometryProjector::ProjectUVFollow(const Vector& pt,
                                                            const ProjectionSettings& settings)
{
    if (!m_uvFollowReady) return {m_anchorU, m_anchorV};

    // Flat-project the point (preserves source shape).
    Vector d = pt - m_flatOrigin;
    Float flatX = Dot(d, m_flatRight);
    Float flatY = Dot(d, m_flatUp);

    // Normalize relative to the source's own centroid and extent, so the
    // silhouette spans roughly [-0.5..0.5] in both axes.
    Float normU = (flatX - m_flatCentroidX) / m_flatRangeX;
    Float normV = (flatY - m_flatCentroidY) / m_flatRangeY;

    Float u = m_anchorU + normU;
    Float v = m_anchorV + normV;

    // Wrap to [0..1].
    u = u - std::floor(u);
    v = v - std::floor(v);

    return { u, v };
}

std::pair<Float,Float> GeometryProjector::ProjectPoint(const Vector& pt,
                                                         const ProjectionSettings& settings)
{
    switch (settings.projMode)
    {
        case PROJ_MODE_FLAT_X:     return {pt.z,  pt.y};
        case PROJ_MODE_FLAT_Y:     return {pt.x,  pt.z};
        case PROJ_MODE_FLAT_Z:     return {pt.x,  pt.y};
        // Negative axes mirror the coordinate so the projection comes from the
        // opposite side. Useful when an object sits behind/below the target.
        case PROJ_MODE_FLAT_NEG_X: return {-pt.z, pt.y};
        case PROJ_MODE_FLAT_NEG_Y: return {pt.x,  -pt.z};
        case PROJ_MODE_FLAT_NEG_Z: return {-pt.x, pt.y};
        case PROJ_MODE_CAMERA:     return ProjectCamera(pt, settings);
        case PROJ_MODE_CUSTOM:     return ProjectCustom(pt, settings);
        // UVFOLLOW is handled in Project() before reaching ProjectPoint;
        // this is a safe fallback.
        case PROJ_MODE_UVFOLLOW:   return {pt.x, pt.y};
        default:                   return {pt.x, pt.y};
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
    if (Abs(Dot(dir, up)) > 0.999) up = Vector(1, 0, 0);

    Vector right = Cross(dir, up);
    right.Normalize();
    up = Cross(right, dir);
    up.Normalize();

    return { Dot(pt, right), Dot(pt, up) };
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
