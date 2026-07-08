#pragma once
// Geometry collector: walks scene objects and extracts points/lines/polygons/splines
// Per-object color and thickness are read from the ProjectionSettingsTag (if present)
// and stored alongside each line/polygon so the rasterizer can draw different
// objects with different visual styles.

#include "c4d.h"
#include <vector>
#include <set>
#include <utility>

// A line segment with per-object color and thickness
struct CollectedLine
{
    Int32  v0, v1;
    Vector color;
    Float  thickness;
};

// A polygon (triangle or quad) with per-object color
struct CollectedPolygon
{
    std::vector<Int32> indices;
    Vector             color;
};

// All collected 3D geometry in world coordinates
struct CollectedGeometry
{
    std::vector<Vector>         points;
    std::vector<CollectedLine>  lines;
    std::vector<CollectedPolygon> polygons;
    std::vector<CollectedPolygon> closed_splines;

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
    // defaultColor    -- color to use when an object has no ProjectionSettingsTag
    //                     with color override
    // defaultThickness -- line thickness when no tag override
    void Collect(const std::vector<BaseObject*>& objects, BaseDocument* doc,
                 Int32 splineSubdiv, Vector defaultColor, Float defaultThickness);

    const CollectedGeometry& GetGeometry() const { return m_geometry; }
    CollectedGeometry& GetGeometry() { return m_geometry; }

private:
    CollectedGeometry m_geometry;

    // Per-object color/thickness, resolved in CollectObject and used by the
    // Collect* helpers when creating lines/polygons.
    Vector m_currentColor     = Vector(1, 1, 1);
    Float  m_currentThickness = 2.0;
    Vector m_defaultColor     = Vector(1, 1, 1);
    Float  m_defaultThickness = 2.0;

    // Walk one object, resolve tag settings, find the best cache, collect geometry
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
