// Geometry collector implementation for Geometry Projector

#include "geometry_collector.h"
#include <cmath>

void GeometryCollector::Collect(const std::vector<BaseObject*>& objects,
                                 BaseDocument* doc,
                                 Int32 splineSubdiv)
{
    m_geometry.Clear();

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

    // Change #6: do NOT skip hidden (MODE_OFF) objects.
    // Visibility is controlled by per-entry checkboxes in InExcludeData, not editor mode.

    // Try the best cache first
    BaseObject* cache = GetBestCache(obj);
    if (cache)
    {
        CollectFromCache(cache, obj->GetMg(), doc, splineSubdiv);
    }
    else
    {
        // No cache -- treat the object itself as the source
        CollectFromCache(obj, obj->GetMg(), doc, splineSubdiv);
    }
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

    // Fix for BUG 12: check for deform cache BEFORE dispatching by type,
    // so we never collect the raw object AND its deform cache both.
    BaseObject* deformCache = cacheObj->GetDeformCache();
    if (deformCache)
    {
        // Deform cache completely replaces this object -- recurse only into it
        CollectFromCache(deformCache, worldMg, doc, splineSubdiv);
        return;
    }

    Int32 type = cacheObj->GetType();

    if (type == Opolygon)
    {
        CollectPolygonObject(static_cast<PolygonObject*>(cacheObj), worldMg);
    }
    else if (type == Ospline)
    {
        CollectSpline(static_cast<SplineObject*>(cacheObj), worldMg, splineSubdiv);
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

    // Base index for all points of this object
    Int32 baseIdx = (Int32)m_geometry.points.size();

    // Add points in world coordinates
    for (Int32 i = 0; i < pointCount; i++)
    {
        m_geometry.points.push_back(worldMg * points[i]);
    }

    // Deduplicated edges
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
            m_geometry.polygons.push_back({a, b, c});
        else
            m_geometry.polygons.push_back({a, b, c, d});

        auto addEdge = [&](Int32 p0, Int32 p1)
        {
            std::pair<Int32,Int32> edge = (p0 < p1) ? std::make_pair(p0, p1)
                                                     : std::make_pair(p1, p0);
            if (edgeSet.insert(edge).second)
                m_geometry.lines.push_back(edge);
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

    // Prefer interpolated (real) spline
    SplineObject* realSpline = splineObj->GetRealSpline();
    SplineObject* workSpline = realSpline ? realSpline : splineObj;

    Int32 segCount       = workSpline->GetSegmentCount();
    Int32 totalPointCount = workSpline->GetPointCount();

    if (totalPointCount < 2)
        return;

    const Vector* rawPoints = workSpline->GetPointR();
    if (!rawPoints)
        return;

    // If no segments, treat the whole spline as one segment
    bool hasSegments = (segCount > 0);
    if (!hasSegments)
        segCount = 1;

    Int32 splineType       = workSpline->GetSplineType();
    bool  needsInterpolation = (splineType != SPLINETYPE_LINEAR);

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
                m_geometry.lines.push_back({baseIdx + i, baseIdx + i + 1});

            if (closed)
                m_geometry.lines.push_back({baseIdx + segPointCount - 1, baseIdx});
        }
        else
        {
            // Use GetSplinePoint for interpolated splines.
            // t in [0..1] is relative to the given segment.
            Int32 samplesPerSeg = Max(1, splineSubdiv * 4);
            Int32 totalSamples  = (segPointCount - 1) * samplesPerSeg + 1;

            for (Int32 i = 0; i < totalSamples; i++)
            {
                Float t  = (totalSamples > 1) ? ((Float)i / (Float)(totalSamples - 1)) : 0.0;
                Vector pt = workSpline->GetSplinePoint(t, seg);
                m_geometry.points.push_back(worldMg * pt);
            }

            for (Int32 i = 0; i < totalSamples - 1; i++)
                m_geometry.lines.push_back({baseIdx + i, baseIdx + i + 1});

            if (closed)
                m_geometry.lines.push_back({baseIdx + totalSamples - 1, baseIdx});
        }

        // Closed splines are also stored as polygons for fill
        if (closed)
        {
            Int32 addedCount = (Int32)m_geometry.points.size() - baseIdx;
            std::vector<Int32> splinePoly;
            splinePoly.reserve(addedCount);
            for (Int32 i = 0; i < addedCount; i++)
                splinePoly.push_back(baseIdx + i);
            m_geometry.closed_splines.push_back(std::move(splinePoly));
        }

        startPt += segPointCount;
    }
}
