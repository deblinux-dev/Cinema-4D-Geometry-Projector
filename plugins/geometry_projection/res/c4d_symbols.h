#pragma once
enum
{
    // --- Ogeometryprojector ---
    GRP_SOURCE              = 1000,
    GRP_PROJECTION          = 1100,
    GRP_TRANSFORM           = 1150,
    GRP_DRAWING             = 1200,
    GRP_PREVIEW             = 1300,
    GRP_BAKE                = 1400,
    GRP_BUTTONS             = 1500,

    SOURCE_OBJECTS          = 1001,
    TARGET_MATERIAL         = 1002,
    TARGET_CHANNEL          = 1003,
    TARGET_OBJECT           = 1004,

    TCH_COLOR               = 0,
    TCH_LUMINANCE           = 1,
    TCH_ALPHA               = 2,
    TCH_BUMP                = 3,
    TCH_DIFFUSION           = 4,

    PROJ_MODE               = 1101,
    PROJ_DIRECTION          = 1102,
    PROJ_AUTO_FIT           = 1103,
    PROJ_INVERT_X           = 1104,
    PROJ_INVERT_Y           = 1105,
    PROJ_CAMERA_LINK        = 1106,

    PROJ_MODE_FLAT_X        = 0,
    PROJ_MODE_FLAT_Y        = 1,
    PROJ_MODE_FLAT_Z        = 2,
    PROJ_MODE_CAMERA        = 3,
    PROJ_MODE_CUSTOM        = 4,
    PROJ_MODE_FLAT_NEG_X    = 5,
    PROJ_MODE_FLAT_NEG_Y    = 6,
    PROJ_MODE_FLAT_NEG_Z    = 7,
    PROJ_MODE_UVFOLLOW      = 8,

    UV_OFFSET_X             = 1110,
    UV_OFFSET_Y             = 1111,
    UV_SCALE_X              = 1112,
    UV_SCALE_Y              = 1113,
    UV_ROTATION             = 1114,

    LINE_WIDTH              = 1201,
    DRAW_FILL               = 1202,
    DRAW_OUTLINE            = 1203,
    OUTLINE_WIDTH           = 1204,
    FG_COLOR                = 1210,
    BG_COLOR                = 1211,
    USE_ALPHA               = 1212,
    OPACITY                 = 1213,
    SPLINE_SUBDIVISION      = 1214,
    LINE_CAP_STYLE          = 1215,
    LINE_CAP_ROUND          = 0,
    LINE_CAP_SQUARE         = 1,

    PREVIEW_RESOLUTION      = 1301,
    PREVIEW_ENABLED         = 1302,
    SHOW_BOUNDS             = 1303,
    DEBUG_OUTPUT            = 1304,

    PREVIEW_RES_32          = 0,
    PREVIEW_RES_64          = 1,
    PREVIEW_RES_128         = 2,
    PREVIEW_RES_256         = 3,
    PREVIEW_RES_512         = 4,
    PREVIEW_RES_1024        = 5,
    PREVIEW_RES_2048        = 6,

    BAKE_WIDTH              = 1401,
    BAKE_HEIGHT             = 1402,
    BAKE_AA                 = 1403,
    BAKE_PATH               = 1404,
    BAKE_FORMAT             = 1405,

    BAKE_AA_NONE            = 0,
    BAKE_AA_2X              = 1,
    BAKE_AA_4X              = 2,

    FORMAT_PNG              = 0,
    FORMAT_TIFF             = 1,
    FORMAT_PSD              = 2,
    FORMAT_JPG              = 3,

    BTN_CREATE_SHADER       = 1501,
    BTN_BAKE                = 1502,
    BTN_REFRESH             = 1503,

    // --- Xprojectionshader ---
    SHADER_PROJECTOR_LINK   = 2001,
    SHADER_BLEND_MODE       = 2002,

    BLEND_REPLACE           = 0,
    BLEND_MULTIPLY          = 1,
    BLEND_ADD               = 2,
    BLEND_OVERLAY           = 3,

    _DUMMY_ELEMENT_
};