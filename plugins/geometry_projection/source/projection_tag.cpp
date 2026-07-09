#include "projection_tag.h"
#include "description/Tprojectionsettings.h"

// ==================== Alloc / Init ====================

NodeData* ProjectionSettingsTag::Alloc()
{
    return NewObjClear(ProjectionSettingsTag);
}

Bool ProjectionSettingsTag::Init(GeListNode* node)
{
    BaseTag*       tag  = static_cast<BaseTag*>(node);
    BaseContainer* data = tag->GetDataInstance();
    if (!data) return false;

    data->SetBool(PROJTAG_OVERRIDE_COLOR, false);
    data->SetVector(PROJTAG_COLOR, Vector(1, 1, 1));
    data->SetBool(PROJTAG_OVERRIDE_THICKNESS, false);
    data->SetFloat(PROJTAG_THICKNESS, 2.0);
    data->SetBool(PROJTAG_OVERRIDE_FILL, false);
    data->SetBool(PROJTAG_FILL, false);

    return true;
}

// ==================== Registration ====================

Bool RegisterProjectionSettingsTag()
{
    // Try to load the tag icon from res/icons/. Non-fatal: registration
    // proceeds without a custom icon if the file is missing or unreadable.
    BaseBitmap* icon = BaseBitmap::Alloc();
    if (icon)
    {
        if (icon->Init(GeGetPluginResourcePath() + "icons/tprojectionsettings.png"_s) != IMAGERESULT::OK)
        {
            BaseBitmap::Free(icon);
            icon = nullptr;
        }
    }

    if (!RegisterTagPlugin(PLUGIN_ID_PROJECTION_TAG, "Projection Object Settings"_s,
            TAG_VISIBLE | TAG_MULTIPLE, ProjectionSettingsTag::Alloc,
            "Tprojectionsettings"_s, icon, 0))
        return false;

    return true;
}
