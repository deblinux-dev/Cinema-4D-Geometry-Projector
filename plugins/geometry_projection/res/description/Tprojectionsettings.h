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
    // Anything drawn outside the clip source's silhouette is discarded.
    // Nesting works: a clip source can itself have its own clip source, so
    // its effective silhouette is already clipped before clipping this object.
    PROJTAG_CLIP_SOURCES       = 3007
};

#endif
