#pragma once
// Geometry collector: walks scene objects and extracts points/lines/polygons/splines

#include "c4d.h"
#include <vector>
#include <set>
#include <utility>

// All collected 3D geometry in world coordinates
struct CollectedGeometry
{
    // All 3D points (world coordinates)
    std::vector<Vector> points;

    // Lines: pairs of indices into points
    std::vector<std::pair<Int32, Int32>> lines;

    // Polygons: lists of indices into points (triangle or quad)
    std::vector<std::vector<Int32>> polygons;

    // Closed splines for fill (indices into points)
    std::vector<std::vector<Int32>> closed_splines;

    void Clear()
    {
        points.clear();
        lines.clear();
        polygons.clear();
        closed_splines.clear();
    }
};

// Collects geometry from an InExcludeData object list
class GeometryCollector
{
public:
    // Collect geometry from a list of objects.
    // objects         -- source BaseObject* list (from InExcludeData)
    // doc             -- active document
    // splineSubdiv    -- interpolation steps per spline segment
    // Change #6: hidden objects (MODE_OFF) are NOT skipped -- they always project
    void Collect(const std::vector<BaseObject*>& objects, BaseDocument* doc, Int32 splineSubdiv);

    const CollectedGeometry& GetGeometry() const { return m_geometry; }
    CollectedGeometry& GetGeometry() { return m_geometry; }

private:
    CollectedGeometry m_geometry;

    // Walk one object, find the best cache, collect geometry
    void CollectObject(BaseObject* obj, BaseDocument* doc, Int32 splineSubdiv);

    // Return the best cache for an object (deform cache > cache > nullptr)
    BaseObject* GetBestCache(BaseObject* obj);

    // Recursively collect from a cache object and its children
    void CollectFromCache(BaseObject* cacheObj, const Matrix& worldMg,
                          BaseDocument* doc, Int32 splineSubdiv);

    // Extract polygons from a PolygonObject
    void CollectPolygonObject(PolygonObject* polyObj, const Matrix& worldMg);

    // Extract points from a spline with interpolation
    void CollectSpline(SplineObject* splineObj, const Matrix& worldMg, Int32 splineSubdiv);
};
