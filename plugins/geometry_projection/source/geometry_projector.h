#pragma once
// Geometry projector: converts 3D points to 2D UV coordinates

#include "c4d.h"
#include "geometry_collector.h"
#include <vector>
#include <utility>

// All projection and drawing settings, populated from the object's BaseContainer
struct ProjectionSettings
{
    // Projection mode
    Int32  projMode;           // 0=FlatX, 1=FlatY, 2=FlatZ, 3=Camera, 4=Custom

    // Custom direction mode
    Vector projDirection;

    // Camera mode
    Matrix cameraMatrix;       // Camera world matrix
    Float  cameraFov;          // Field of view in radians

    // Normalization
    Bool   autoFit;
    Vector targetBoundsCenter; // Target object bounding box center
    Vector targetBoundsRad;    // Target object bounding box half-extents
    Bool   hasTargetBounds;    // Whether to use target bounds for normalization

    // UV transform
    Float  uvOffsetX;
    Float  uvOffsetY;
    Float  uvScaleX;
    Float  uvScaleY;
    Float  uvRotation;         // Degrees

    // Invert flags
    Bool   invertX;
    Bool   invertY;

    // Drawing parameters
    Float  lineWidth;          // In world units (will be scaled to pixels)
    Bool   drawFill;
    Bool   drawOutline;
    Float  outlineWidth;
    Vector fgColor;            // RGB [0..1]
    Vector bgColor;            // RGB [0..1]
    Bool   useAlpha;
    Float  opacity;            // [0..1]
    Int32  splineSubdivision;
    Int32  lineCapStyle;       // 0=Round, 1=Square

    // Preview
    Int32  previewResolution;  // Pixels
    Bool   previewEnabled;
    Bool   showBounds;
    Bool   debugOutput;

    // Bake
    Int32    bakeWidth;
    Int32    bakeHeight;
    Int32    bakeAA;
    Int32    bakeFormat;
    Filename bakePath;

    // Computed fields
    Float  scaledLineWidth;    // Line width scaled to current resolution (pixels)
    Float  scaledOutlineWidth; // Outline width scaled to current resolution (pixels)

    static const Int32 REFERENCE_RESOLUTION = 1024;

    // Build a ProjectionSettings from a BaseContainer.
    // resolution  -- current render/preview resolution in pixels
    // targetObj   -- used for bounds-based normalization (may be nullptr)
    // doc         -- active document (needed for link resolution)
    // sourceObjs  -- if targetObj is null and autoFit is on, the combined
    //                world-space bounding box of these objects is used as the
    //                projection plane (so all source geometry fits in UV [0..1]).
    static ProjectionSettings FromContainer(BaseContainer* bc, Int32 resolution,
                                             BaseObject* targetObj,
                                             BaseDocument* doc,
                                             const std::vector<BaseObject*>& sourceObjs = {});

    // Scale a world-unit line width to pixels for the given resolution
    Float ScaleLineWidth(Float width, Int32 resolution) const
    {
        return width * ((Float)resolution / (Float)REFERENCE_RESOLUTION);
    }
};

// Projected 2D geometry in UV space [0..1]
struct ProjectedGeometry
{
    // 2D points (u, v) in [0..1]
    std::vector<std::pair<Float,Float>> points2d;

    // Lines with per-object color and thickness (copied from CollectedGeometry)
    std::vector<CollectedLine> lines;

    // Polygons with per-object color (copied from CollectedGeometry)
    std::vector<CollectedPolygon> polygons;

    // Closed splines with per-object color (copied from CollectedGeometry)
    std::vector<CollectedPolygon> closedSplines;

    // Raw 2D bounds before normalization
    Float boundsMinX, boundsMinY;
    Float boundsMaxX, boundsMaxY;

    void Clear()
    {
        points2d.clear();
        lines.clear();
        polygons.clear();
        closedSplines.clear();
    }
};

// Projects CollectedGeometry -> ProjectedGeometry
class GeometryProjector
{
public:
    void Project(const CollectedGeometry& geometry, const ProjectionSettings& settings,
                  ProjectedGeometry& outProjected);

private:
    // Snapshot of the geometry being projected (kept so ApplyClipping can read
    // the clipSources map). Set at the start of Project().
    const CollectedGeometry* m_geometry = nullptr;

    std::pair<Float,Float> ProjectPoint(const Vector& pt, const ProjectionSettings& settings);
    std::pair<Float,Float> ProjectCamera(const Vector& pt, const ProjectionSettings& settings);
    std::pair<Float,Float> ProjectCustom(const Vector& pt, const ProjectionSettings& settings);

    void GetTargetUVBounds(const ProjectionSettings& settings,
                            Float& outMinX, Float& outMinY,
                            Float& outRangeX, Float& outRangeY);

    void NormalizeAutoFit(std::vector<std::pair<Float,Float>>& pts,
                           Float& outMinX, Float& outMinY,
                           Float& outRangeX, Float& outRangeY);

    std::pair<Float,Float> ApplyTransform(Float u, Float v,
                                           const ProjectionSettings& settings);

    // Clip each owner object's projected geometry against its clip sources'
    // silhouettes. Resolves nested clip dependencies in topological order.
    void ApplyClipping(ProjectedGeometry& proj);
};
