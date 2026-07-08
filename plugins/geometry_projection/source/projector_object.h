#pragma once
// GeometryProjectorObject -- ObjectData plugin (OBJECT_GENERATOR)
// Plugin ID: 1059890

#include "c4d.h"
#include "projection_cache.h"
#include "geometry_collector.h"
#include "geometry_projector.h"
#include "rasterizer.h"
#include <vector>

// Hidden parameter IDs (not declared in resource files)
static const Int32 PARAM_CACHE_ID   = 99999;  // Int64 cache ID stored in container

static const Int32 PLUGIN_ID_PROJECTOR_OBJECT = 1059890;

class GeometryProjectorObject : public ObjectData
{
    INSTANCEOF(GeometryProjectorObject, ObjectData)

public:
    static NodeData* Alloc();

    Bool Init(GeListNode* node) override;
    void Free(GeListNode* node) override;
    
    // ДОБАВЛЕНО: Объявление функции Read, на отсутствие которой ругался компилятор
    Bool Read(GeListNode* node, HyperFile* hf, Int32 level) override;

    BaseObject* GetVirtualObjects(BaseObject* op, HierarchyHelp* hh) override;

    // Forces C4D to re-evaluate this generator when InExclude source objects
    // (or the camera in camera mode) change. Without this, C4D does not know
    // the generator depends on those external objects and never calls
    // GetVirtualObjects() when they move.
    void CheckDirty(BaseObject* op, BaseDocument* doc) override;

    Bool Message(GeListNode* node, Int32 type, void* data) override;

    Bool GetDEnabling(GeListNode* node, const DescID& id, const GeData& t_data,
                       DESCFLAGS_ENABLE flags, const BaseContainer* itemdesc) override;

    DRAWRESULT Draw(BaseObject* op, DRAWPASS drawpass, BaseDraw* bd,
                     BaseDrawHelp* bh) override;

    Bool CopyTo(NodeData* dest, GeListNode* snode, GeListNode* dnode,
                 COPYFLAGS flags, AliasTrans* trn) override;

private:
    Int64 m_cacheId = 0;

    // Last combined dirty checksum of source objects + target + camera.
    // This is a CLASS FIELD (intentionally reset to 0 on viewport cloning):
    // the first CheckDirty on a fresh clone always sees sum != 0 and triggers
    // a rebuild, which is what makes the viewport update. Storing it in the
    // container (previous attempt) survived cloning and broke real-time.
    UInt32 m_lastDependencyDirty = 0;

    void DoUpdate(BaseObject* op, BaseDocument* doc);

    std::vector<BaseObject*> GetSourceObjects(BaseObject* op, BaseDocument* doc);

    static Int32 GetResolutionFromParam(Int32 paramValue);

    void UpdateMaterial(BaseObject* op, BaseDocument* doc);
    void CreateShader(BaseObject* op, BaseDocument* doc);
    void BakeToFile(BaseObject* op, BaseDocument* doc);

    ProjectionCache* GetCache(BaseObject* op);

    void DrawBounds(BaseObject* op, BaseDraw* bd, BaseDrawHelp* bh);
};

Bool RegisterGeometryProjectorObject();