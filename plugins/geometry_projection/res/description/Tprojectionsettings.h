#ifndef TPROJECTIONSETTINGS_H__
#define TPROJECTIONSETTINGS_H__

enum
{
    PROJTAG_OVERRIDE_THICKNESS = 3001,
    PROJTAG_THICKNESS          = 3002,
    PROJTAG_OVERRIDE_COLOR     = 3003,
    PROJTAG_COLOR              = 3004,
    PROJTAG_OVERRIDE_FILL      = 3005,
    PROJTAG_FILL               = 3006,
    // InExclude list of objects whose projected silhouette clips this object.
    PROJTAG_CLIP_SOURCES       = 3007,
    // Per-object projection direction source (UV Follow mode only).
    // If set, this object is projected along the direction from this linked
    // object toward the target center (orthographic parallel rays).
    // If not set, the global Geometry Projector direction is used.
    // This allows each source object to have its own projection direction
    // (e.g. a cube on the right projects horizontally, a sphere on top
    // projects vertically) — like individual decals from different sides.
    PROJTAG_DIR_SOURCE         = 3008
};

#endif
