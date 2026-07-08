// Geometry Projector plugin for Cinema 4D R20
// Entry point: registers GeometryProjectorObject and ProjectionShader

#include "c4d.h"
#include "projection_cache.h"

extern Bool RegisterGeometryProjectorObject();
extern Bool RegisterProjectionShader();
extern Bool RegisterProjectionSettingsTag();

Bool PluginStart()
{
    if (!RegisterProjectionShader())
    {
        GePrint("GeometryProjector: RegisterProjectionShader FAILED!"_s);
        return false;
    }

    if (!RegisterGeometryProjectorObject())
    {
        GePrint("GeometryProjector: RegisterGeometryProjectorObject FAILED!"_s);
        return false;
    }

    if (!RegisterProjectionSettingsTag())
    {
        GePrint("GeometryProjector: RegisterProjectionSettingsTag FAILED!"_s);
        return false;
    }

    GePrint("GeometryProjector: Plugins registered successfully"_s);
    return true;
}

Bool PluginMessage(Int32 id, void* data)
{
    switch (id)
    {
        case C4DPL_INIT_SYS:
        {
            if (!g_resource.Init())
            {
                GePrint("GeometryProjector: g_resource.Init() FAILED!"_s);
                return false;
            }
            return true;
        }
    }
    return false;
}

void PluginEnd()
{
    // Clear the cache registry to avoid calling BaseBitmap::Free()
    // after C4D has already cleaned up its allocator
    ClearCacheRegistry();
}
