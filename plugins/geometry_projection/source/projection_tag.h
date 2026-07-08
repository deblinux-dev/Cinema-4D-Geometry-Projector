#pragma once
// ProjectionSettingsTag -- TagData plugin
// Plugin ID: 1059892
//
// Applied to any object that is in the projector's Source Objects list.
// Allows overriding the color and line thickness for that specific object,
// so different objects can project with different visual styles.

#include "c4d.h"

static const Int32 PLUGIN_ID_PROJECTION_TAG = 1059892;

class ProjectionSettingsTag : public TagData
{
    INSTANCEOF(ProjectionSettingsTag, TagData)

public:
    static NodeData* Alloc();
    Bool Init(GeListNode* node) override;
};

Bool RegisterProjectionSettingsTag();
