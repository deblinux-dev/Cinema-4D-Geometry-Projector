CONTAINER Xprojectionshader
{
    NAME Xprojectionshader;
    INCLUDE Xbase;

    GROUP ID_SHADERPROPERTIES
    {
        LINK SHADER_PROJECTOR_LINK { ACCEPT { Ogeometryprojector; } }
        LONG SHADER_BLEND_MODE
        {
            CYCLE { BLEND_REPLACE; BLEND_MULTIPLY; BLEND_ADD; BLEND_OVERLAY; }
        }
    }
}
