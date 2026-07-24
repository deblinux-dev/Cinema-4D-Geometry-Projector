// Geometry collector implementation for Geometry Projector

#include "geometry_collector.h"
#include "projection_tag.h"
#include "description/Tprojectionsettings.h"
#include "customgui_inexclude.h"
#include <cmath>

void GeometryCollector::Collect(const std::vector<BaseObject*>& objects,
                                 BaseDocument* doc,
                                 HierarchyHelp* hh,
                                 Int32 splineSubdiv,
                                 Vector defaultColor,
                                 Float defaultThickness,
                                 Bool  defaultFill)
{
    m_geometry.Clear();
    m_defaultColor     = defaultColor;
    m_defaultThickness = defaultThickness;
    m_defaultFill      = defaultFill;
    m_hh               = hh;

    for (BaseObject* obj : objects)
    {
        if (!obj)
            continue;
        CollectObject(obj, doc, splineSubdiv);
    }
}

void GeometryCollector::CollectObject(BaseObject* obj, BaseDocument* doc, Int32 splineSubdiv)
{
    if (!obj)
        return;

    // Resolve per-object color, thickness and fill from the ProjectionSettingsTag.
    // If the tag is not present, or the override flags are off, fall back to
    // the global defaults passed from ProjectionSettings.
    m_currentColor     = m_defaultColor;
    m_currentThickness = m_defaultThickness;
    m_currentFillOverride = false;
    m_currentFill        = m_defaultFill;
    m_currentOwner       = obj;

    BaseTag* tag = obj->GetTag(PLUGIN_ID_PROJECTION_TAG);
    if (tag)
    {
        BaseContainer* td = tag->GetDataInstance();
        if (td)
        {
            if (td->GetBool(PROJTAG_OVERRIDE_COLOR))
                m_currentColor = td->GetVector(PROJTAG_COLOR, m_defaultColor);
            if (td->GetBool(PROJTAG_OVERRIDE_THICKNESS))
                m_currentThickness = td->GetFloat(PROJTAG_THICKNESS, m_defaultThickness);
            if (td->GetBool(PROJTAG_OVERRIDE_FILL))
            {
                m_currentFillOverride = true;
                m_currentFill        = td->GetBool(PROJTAG_FILL, m_defaultFill);
            }

            // Read the clip-source InExclude list. Each listed object's
            // projected silhouette will clip this object during the projection
            // pass. Nesting is automatic: a clip source can itself have a
            // PROJTAG_CLIP_SOURCES list, so its silhouette is already clipped.
            InExcludeData* clipList = (InExcludeData*)td->GetCustomDataType(PROJTAG_CLIP_SOURCES, CUSTOMDATATYPE_INEXCLUDE_LIST);
            if (clipList && clipList->GetObjectCount() > 0 && doc)
            {
                std::vector<BaseObject*> clips;
                Int32 cnt = clipList->GetObjectCount();
                clips.reserve(cnt);
                for (Int32 i = 0; i < cnt; i++)
                {
                    BaseObject* clipObj = static_cast<BaseObject*>(clipList->ObjectFromIndex(doc, i));
                    if (clipObj) clips.push_back(clipObj);
                }
                if (!clips.empty())
                    m_geometry.clipSources[obj] = std::move(clips);
            }
        }
    }

    // For spline objects (editable OR parametric like Circle/Rectangle/Arc):
    // Use GetVirtualLineObject which is the official API for getting a
    // LineObject/SplineObject from ANY spline -- editable or parametric
    // primitive. It requires HierarchyHelp from GetVirtualObjects.
    if (obj->IsInstanceOf(Ospline))
    {
        SplineObject* splineObj = static_cast<SplineObject*>(obj);

        // Editable spline with points: use the points directly.
        if (splineObj->GetPointCount() >= 2)
        {
            CollectSpline(splineObj, obj->GetMg(), splineSubdiv);
            return;
        }

        // Parametric spline: use GetVirtualLineObject to force interpolation.
        // keep_spline=true returns a SplineObject (not LineObject), so
        // IsInstanceOf(Ospline) succeeds and we can use CollectSpline.
        // recurse=true searches children/caches if the object itself has no line.
        if (m_hh)
        {
            Matrix mres;
            Bool dirty = false;
            BaseObject* lineObj = GetVirtualLineObject(obj, m_hh, obj->GetMl(), true, true, &mres, &dirty);
            if (lineObj && lineObj->IsInstanceOf(Ospline))
            {
                SplineObject* realSpline = static_cast<SplineObject*>(lineObj);
                if (realSpline->GetPointCount() >= 2)
                {
                    // GetVirtualLineObject returns points in the object's local
                    // space. Use the object's world matrix.
                    CollectSpline(realSpline, obj->GetMg(), splineSubdiv);
                    return;
                }
            }
            // Note: GetVirtualLineObject returns a borrowed reference (owned by
            // the cache), so we do NOT free it.
        }

        // Fallback: walk the cache.
        BaseObject* cache = GetBestCache(obj);
        if (cache)
        {
            CollectFromCache(cache, obj->GetMg(), doc, splineSubdiv);
            return;
        }
        return;
    }

    // For polygon objects and generators, use the cache (deform > generator)
    BaseObject* cache = GetBestCache(obj);
    if (cache)
    {
        CollectFromCache(cache, obj->GetMg(), doc, splineSubdiv);
    }
    else if (obj->IsInstanceOf(Opolygon))
    {
        // Editable polygon object with no cache: collect directly
        CollectFromCache(obj, obj->GetMg(), doc, splineSubdiv);
    }
    // else: generator whose cache hasn't been built yet -- nothing to collect
    // this frame; the next dirty tick will retry.
}

BaseObject* GeometryCollector::GetBestCache(BaseObject* obj)
{
    if (!obj)
        return nullptr;

    // Deform cache (deformed geometry) takes priority
    BaseObject* deformCache = obj->GetDeformCache();
    if (deformCache)
        return deformCache;

    // Generator cache (virtual objects)
    BaseObject* cache = obj->GetCache();
    if (cache)
        return cache;

    return nullptr;
}

void GeometryCollector::CollectFromCache(BaseObject* cacheObj, const Matrix& worldMg,
                                          BaseDocument* doc, Int32 splineSubdiv)
{
    if (!cacheObj)
        return;

    // Check for deform cache BEFORE dispatching by type
    BaseObject* deformCache = cacheObj->GetDeformCache();
    if (deformCache)
    {
        CollectFromCache(deformCache, worldMg, doc, splineSubdiv);
        return;
    }

    Int32 type = cacheObj->GetType();

    // Use IsInstanceOf for spline detection so parametric spline caches
    // (which may report OCIRCLE/ORECTANGLE from GetType) are still collected.
    if (type == Opolygon)
    {
        CollectPolygonObject(static_cast<PolygonObject*>(cacheObj), worldMg);
    }
    else if (cacheObj->IsInstanceOf(Ospline))
    {
        CollectSpline(static_cast<SplineObject*>(cacheObj), worldMg, splineSubdiv);
    }
    else if (type == Oline)
    {
        // LineObject: parametric spline primitives (Circle, Rectangle, Arc,
        // Text) generate a LineObject in their cache with interpolated points.
        // LineObject has segments (like SplineObject) that separate distinct
        // contours (e.g. individual letters in a text spline). Without
        // respecting segments, all points get connected, creating spurious
        // lines between letters.
        LineObject* lineObj = static_cast<LineObject*>(cacheObj);
        Int32 ptCount = lineObj->GetPointCount();
        Int32 segCount = lineObj->GetSegmentCount();
        if (ptCount >= 2 && segCount > 0)
        {
            const Vector* pts = lineObj->GetPointR();
            const Segment* segs = lineObj->GetSegmentR();
            if (pts && segs)
            {
                Int32 startPt = 0;
                for (Int32 s = 0; s < segCount; s++)
                {
                    Int32 segPts = segs[s].cnt;
                    Bool closed = segs[s].closed;
                    if (segPts < 2) { startPt += segPts; continue; }

                    Int32 baseIdx = (Int32)m_geometry.points.size();
                    for (Int32 i = 0; i < segPts; i++)
                        m_geometry.points.push_back(worldMg * pts[startPt + i]);

                    // Line segments
                    for (Int32 i = 0; i < segPts - 1; i++)
                        m_geometry.lines.push_back({ baseIdx + i, baseIdx + i + 1,
                                                     m_currentColor, m_currentThickness,
                                                     true, false, m_currentOwner });
                    if (closed)
                        m_geometry.lines.push_back({ baseIdx + segPts - 1, baseIdx,
                                                     m_currentColor, m_currentThickness,
                                                     true, false, m_currentOwner });

                    // Add as closed spline for fill rendering (even-odd rule)
                    if (closed && segPts >= 3)
                    {
                        std::vector<Int32> splinePoly;
                        splinePoly.reserve(segPts);
                        for (Int32 i = 0; i < segPts; i++)
                            splinePoly.push_back(baseIdx + i);
                        m_geometry.closed_splines.push_back({ std::move(splinePoly),
                                                              m_currentColor,
                                                              m_currentFillOverride,
                                                              m_currentFill,
                                                              m_currentOwner });
                    }
                    startPt += segPts;
                }
            }
        }
        else if (ptCount >= 2)
        {
            // Fallback: no segments, treat as single contour
            const Vector* pts = lineObj->GetPointR();
            if (pts)
            {
                Int32 baseIdx = (Int32)m_geometry.points.size();
                for (Int32 i = 0; i < ptCount; i++)
                    m_geometry.points.push_back(worldMg * pts[i]);
                for (Int32 i = 0; i < ptCount - 1; i++)
                    m_geometry.lines.push_back({ baseIdx + i, baseIdx + i + 1,
                                                 m_currentColor, m_currentThickness,
                                                 true, false, m_currentOwner });
            }
        }
    }

    // Recurse into generator cache children
    BaseObject* childCache = cacheObj->GetCache();
    if (childCache)
    {
        CollectFromCache(childCache, worldMg, doc, splineSubdiv);
    }

    // Recurse into direct children (each with its own local transform offset)
    BaseObject* child = cacheObj->GetDown();
    while (child)
    {
        Matrix childMg = worldMg * child->GetMl();
        CollectFromCache(child, childMg, doc, splineSubdiv);
        child = child->GetNext();
    }
}

void GeometryCollector::CollectPolygonObject(PolygonObject* polyObj, const Matrix& worldMg)
{
    if (!polyObj)
        return;

    Int32 pointCount = polyObj->GetPointCount();
    Int32 polyCount  = polyObj->GetPolygonCount();

    if (pointCount == 0 || polyCount == 0)
        return;

    const Vector*   points = polyObj->GetPointR();
    const CPolygon* polys  = polyObj->GetPolygonR();

    if (!points || !polys)
        return;

    Int32 baseIdx = (Int32)m_geometry.points.size();

    for (Int32 i = 0; i < pointCount; i++)
        m_geometry.points.push_back(worldMg * points[i]);

    std::set<std::pair<Int32,Int32>> edgeSet;

    for (Int32 i = 0; i < polyCount; i++)
    {
        const CPolygon& poly = polys[i];

        Int32 a = baseIdx + poly.a;
        Int32 b = baseIdx + poly.b;
        Int32 c = baseIdx + poly.c;
        Int32 d = baseIdx + poly.d;

        bool isTriangle = (poly.c == poly.d);

        if (isTriangle)
            m_geometry.polygons.push_back({ {a, b, c}, m_currentColor, m_currentFillOverride, m_currentFill, m_currentOwner });
        else
            m_geometry.polygons.push_back({ {a, b, c, d}, m_currentColor, m_currentFillOverride, m_currentFill, m_currentOwner });

        auto addEdge = [&](Int32 p0, Int32 p1)
        {
            std::pair<Int32,Int32> edge = (p0 < p1) ? std::make_pair(p0, p1)
                                                     : std::make_pair(p1, p0);
            if (edgeSet.insert(edge).second)
                m_geometry.lines.push_back({ p0, p1, m_currentColor, m_currentThickness, false, true, m_currentOwner });
        };

        addEdge(a, b);
        addEdge(b, c);
        if (!isTriangle)
        {
            addEdge(c, d);
            addEdge(d, a);
        }
        else
        {
            addEdge(c, a);
        }
    }
}

void GeometryCollector::CollectSpline(SplineObject* splineObj, const Matrix& worldMg,
                                       Int32 splineSubdiv)
{
    if (!splineObj)
        return;

    SplineObject* realSpline = splineObj->GetRealSpline();
    SplineObject* workSpline = realSpline ? realSpline : splineObj;

    Int32 segCount       = workSpline->GetSegmentCount();
    Int32 totalPointCount = workSpline->GetPointCount();

    if (totalPointCount < 2)
        return;

    const Vector* rawPoints = workSpline->GetPointR();
    if (!rawPoints)
        return;

    bool hasSegments = (segCount > 0);
    if (!hasSegments)
        segCount = 1;

    SPLINETYPE splineType = workSpline->GetInterpolationType();
    bool  needsInterpolation = (splineType != SPLINETYPE::LINEAR);

    Int32 startPt = 0;

    for (Int32 seg = 0; seg < segCount; seg++)
    {
        Int32 segPointCount;
        bool  closed = false;

        if (hasSegments)
        {
            const Segment* segInfo = workSpline->GetSegmentR();
            if (!segInfo) break;
            segPointCount = segInfo[seg].cnt;
            closed        = segInfo[seg].closed;
        }
        else
        {
            segPointCount = totalPointCount;
            closed        = workSpline->IsClosed();
        }

        if (segPointCount < 2)
        {
            startPt += segPointCount;
            continue;
        }

        Int32 baseIdx = (Int32)m_geometry.points.size();

        if (!needsInterpolation)
        {
            for (Int32 i = 0; i < segPointCount; i++)
                m_geometry.points.push_back(worldMg * rawPoints[startPt + i]);

            for (Int32 i = 0; i < segPointCount - 1; i++)
                m_geometry.lines.push_back({ baseIdx + i, baseIdx + i + 1,
                                             m_currentColor, m_currentThickness, true, false, m_currentOwner });

            if (closed)
                m_geometry.lines.push_back({ baseIdx + segPointCount - 1, baseIdx,
                                             m_currentColor, m_currentThickness, true, false, m_currentOwner });
        }
        else
        {
            Int32 samplesPerSeg = Max(1, splineSubdiv * 4);
            Int32 totalSamples  = (segPointCount - 1) * samplesPerSeg + 1;

            for (Int32 i = 0; i < totalSamples; i++)
            {
                Float t  = (totalSamples > 1) ? ((Float)i / (Float)(totalSamples - 1)) : 0.0;
                Vector pt = workSpline->GetSplinePoint(t, seg);
                m_geometry.points.push_back(worldMg * pt);
            }

            for (Int32 i = 0; i < totalSamples - 1; i++)
                m_geometry.lines.push_back({ baseIdx + i, baseIdx + i + 1,
                                             m_currentColor, m_currentThickness, true, false, m_currentOwner });

            if (closed)
                m_geometry.lines.push_back({ baseIdx + totalSamples - 1, baseIdx,
                                             m_currentColor, m_currentThickness, true, false, m_currentOwner });
        }

        if (closed)
        {
            Int32 addedCount = (Int32)m_geometry.points.size() - baseIdx;
            std::vector<Int32> splinePoly;
            splinePoly.reserve(addedCount);
            for (Int32 i = 0; i < addedCount; i++)
                splinePoly.push_back(baseIdx + i);
            m_geometry.closed_splines.push_back({ std::move(splinePoly), m_currentColor, m_currentFillOverride, m_currentFill, m_currentOwner });
        }

        startPt += segPointCount;
    }
}
