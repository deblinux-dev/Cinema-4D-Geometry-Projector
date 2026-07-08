CONTAINER Ogeometryprojector
{
    NAME Ogeometryprojector;
    INCLUDE Obase;

    GROUP GRP_SOURCE
    {
        DEFAULT 1;
        IN_EXCLUDE SOURCE_OBJECTS { }
        LINK TARGET_MATERIAL { ACCEPT { Mmaterial; } }
        LINK TARGET_OBJECT { ACCEPT { Obase; } }
        LONG TARGET_CHANNEL
        {
            CYCLE
            {
                TCH_COLOR; TCH_LUMINANCE; TCH_ALPHA;
                TCH_BUMP; TCH_DIFFUSION;
            }
        }
    }

    GROUP GRP_PROJECTION
    {
        DEFAULT 1;
        LONG PROJ_MODE
        {
            CYCLE
            {
                PROJ_MODE_FLAT_X; PROJ_MODE_FLAT_Y; PROJ_MODE_FLAT_Z;
                PROJ_MODE_CAMERA; PROJ_MODE_CUSTOM;
            }
        }
        LINK PROJ_CAMERA_LINK { ACCEPT { Ocamera; } }
        VECTOR PROJ_DIRECTION { }
        BOOL PROJ_AUTO_FIT { }
        BOOL PROJ_INVERT_X { }
        BOOL PROJ_INVERT_Y { }
    }

    GROUP GRP_TRANSFORM
    {
        DEFAULT 1;
        REAL UV_OFFSET_X { UNIT METER; STEP 0.01; }
        REAL UV_OFFSET_Y { UNIT METER; STEP 0.01; }
        REAL UV_SCALE_X { UNIT METER; MIN 0.01; STEP 0.01; }
        REAL UV_SCALE_Y { UNIT METER; MIN 0.01; STEP 0.01; }
        REAL UV_ROTATION { UNIT DEGREE; MIN -360; MAX 360; STEP 1; }
    }

    GROUP GRP_DRAWING
    {
        DEFAULT 1;
        REAL LINE_WIDTH { UNIT METER; MIN 0.1; MAX 100; STEP 0.5; }
        BOOL DRAW_FILL { }
        BOOL DRAW_OUTLINE { }
        REAL OUTLINE_WIDTH { UNIT METER; MIN 0.1; MAX 50; STEP 0.5; }
        COLOR FG_COLOR { }
        COLOR BG_COLOR { }
        BOOL USE_ALPHA { }
        REAL OPACITY { UNIT PERCENT; MIN 0; MAX 100; STEP 1; }
        LONG SPLINE_SUBDIVISION { MIN 1; MAX 10; }
        LONG LINE_CAP_STYLE
        {
            CYCLE { LINE_CAP_ROUND; LINE_CAP_SQUARE; }
        }
    }

    GROUP GRP_PREVIEW
    {
        DEFAULT 1;
        BOOL PREVIEW_ENABLED { }
        LONG PREVIEW_RESOLUTION
        {
            CYCLE
            {
                PREVIEW_RES_32; PREVIEW_RES_64;
                PREVIEW_RES_128; PREVIEW_RES_256;
                PREVIEW_RES_512; PREVIEW_RES_1024;
                PREVIEW_RES_2048;
            }
        }
        BOOL SHOW_BOUNDS { }
        BOOL DEBUG_OUTPUT { }
    }

    GROUP GRP_BAKE
    {
        DEFAULT 1;
        LONG BAKE_WIDTH { MIN 64; MAX 16384; }
        LONG BAKE_HEIGHT { MIN 64; MAX 16384; }
        LONG BAKE_AA { CYCLE { BAKE_AA_NONE; BAKE_AA_2X; BAKE_AA_4X; } }
        LONG BAKE_FORMAT { CYCLE { FORMAT_PNG; FORMAT_TIFF; FORMAT_PSD; FORMAT_JPG; } }
        FILENAME BAKE_PATH { }
    }

    GROUP GRP_BUTTONS
    {
        DEFAULT 1;
        BUTTON BTN_CREATE_SHADER { }
        BUTTON BTN_BAKE { }
        BUTTON BTN_REFRESH { }
    }
}
