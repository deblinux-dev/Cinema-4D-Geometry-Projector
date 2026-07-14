#pragma once
// Geometry collector: walks scene objects and extracts points/lines/polygons/splines
// Per-object color and thickness are read from the ProjectionSettingsTag (if present)
// and stored alongside each line/polygon so the rasterizer can draw different
// objects with different visual styles.

#include "c4d.h"
#include <vector>
#include <set>
#include <map>
#include <utility>

// A line segment with per-object color and thickness.
// isSpline marks segments that belong to a spline (vs polygon edges). Spline
// segments get round caps at EVERY vertex to avoid gaps at corners.
// isPolygonEdge marks edges that come from polygon topology (vs spline).
// The rasterizer uses this to distinguish 'silhouette' (boundary edges) from
// 'all edges' for the 3 draw modes.
// ownerObj identifies which source object this line came from (used for the
// clipping pass: lines of an object are clipped against its clip source's
// projected silhouette).
struct CollectedLine
{
    Int32  v0, v1;
    Vector color;
    Float  thickness;
    Bool   isSpline       = false;
    Bool   isPolygonEdge  = false;
    BaseObject* ownerObj  = nullptr;
};

// A polygon (triangle or quad) with per-object color and fill override.
// fillOverride = true means use 'fill' instead of the global Draw Fill setting.
// ownerObj identifies the source object (for the clipping pass).
struct CollectedPolygon
{
    std::vector<Int32> indices;
    Vector             color;
    Bool               fillOverride = false;
    Bool               fill         = false;
    BaseObject*        ownerObj     = nullptr;
};

// All collected 3D geometry in world coordinates
struct CollectedGeometry
{
    std::vector<Vector>         points;
    std::vector<CollectedLine>  lines;
    std::vector<CollectedPolygon> polygons;
    std::vector<CollectedPolygon> closed_splines;

    // For each source object, the list of objects whose projected silhouette
    // should clip it. Populated by the collector from the
    // ProjectionSettingsTag's PROJTAG_CLIP_SOURCES InExclude list.
    // The clipping pass (in GeometryProjector::Project) uses this to clip the
    // object's polygons/lines against the clip source's projected silhouette.
    std::map<BaseObject*, std::vector<BaseObject*>> clipSources;

    void Clear()
    {
        points.clear();
        lines.clear();
        polygons.clear();
        closed_splines.clear();
        clipSources.clear();
    }
};

// Collects geometry from an InExcludeData object list
class GeometryCollector
{
public:
    // Collect geometry from a list of objects.
    // objects         -- source BaseObject* list (from InExcludeData)
    // doc             -- active document
    // hh              -- HierarchyHelp from GetVirtualObjects (needed for
    //                    GetVirtualLineObject on parametric spline primitives)
    // splineSubdiv    -- interpolation steps per spline segment
    // defaultColor    -- color to use when an object has no ProjectionSettingsTag
    //                     with color override
    // defaultThickness -- line thickness when no tag override
    // defaultFill     -- global Draw Fill setting (used when no tag override)
    void Collect(const std::vector<BaseObject*>& objects, BaseDocument* doc,
                 HierarchyHelp* hh,
                 Int32 splineSubdiv, Vector defaultColor, Float defaultThickness,
                 Bool defaultFill);

    const CollectedGeometry& GetGeometry() const { return m_geometry; }
    CollectedGeometry& GetGeometry() { return m_geometry; }

private:
    CollectedGeometry m_geometry;

    // Per-object color/thickness/fill/owner, resolved in CollectObject and used
    // by the Collect* helpers when creating lines/polygons.
    Vector m_currentColor     = Vector(1, 1, 1);
    Float  m_currentThickness = 2.0;
    Bool   m_currentFillOverride = false;
    Bool   m_currentFill        = false;
    BaseObject* m_currentOwner  = nullptr;
    Vector m_defaultColor     = Vector(1, 1, 1);
    Float  m_defaultThickness = 2.0;
    Bool   m_defaultFill      = false;

    HierarchyHelp* m_hh = nullptr;  // from GetVirtualObjects, for GetVirtualLineObject

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
