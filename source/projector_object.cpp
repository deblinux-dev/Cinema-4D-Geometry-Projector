// GeometryProjectorObject implementation

#include "projector_object.h"
#include "description/Ogeometryprojector.h"
#include "description/Xprojectionshader.h"

// ---- Material channel ID lookup ----

static Int32 GetMaterialChannelId(Int32 channelParam)
{
    switch (channelParam)
    {
        case CHANNEL_COLOR:     return MATERIAL_COLOR_SHADER;
        case CHANNEL_LUMINANCE: return MATERIAL_LUMINANCE_SHADER;
        case CHANNEL_ALPHA:     return MATERIAL_ALPHA_SHADER;
        case CHANNEL_BUMP:      return MATERIAL_BUMP_SHADER;
        case CHANNEL_DIFFUSION: return MATERIAL_DIFFUSION_SHADER;
        default:                return MATERIAL_COLOR_SHADER;
    }
}

// ---- Channel enable flag lookup ----

static Int32 GetChannelUseFlag(Int32 channelParam)
{
    switch (channelParam)
    {
        case CHANNEL_COLOR:     return MATERIAL_USE_COLOR;
        case CHANNEL_LUMINANCE: return MATERIAL_USE_LUMINANCE;
        case CHANNEL_ALPHA:     return MATERIAL_USE_ALPHA;
        case CHANNEL_BUMP:      return MATERIAL_USE_BUMP;
        case CHANNEL_DIFFUSION: return MATERIAL_USE_DIFFUSION;
        default:                return MATERIAL_USE_COLOR;
    }
}

// ==================== Alloc / Init / Free ====================

NodeData* GeometryProjectorObject::Alloc()
{
    return NewObjClear(GeometryProjectorObject);
}

Bool GeometryProjectorObject::Init(GeListNode* node)
{
    BaseObject*    op   = static_cast<BaseObject*>(node);
    BaseContainer* data = op->GetDataInstance();
    if (!data) return false;

    // Инициализация уникального безопасного ID кэша
    m_cacheId = GenerateUniqueCacheID();
    data->SetInt64(PARAM_CACHE_ID, m_cacheId);

    // Default values
    data->SetInt32(TARGET_CHANNEL,      CHANNEL_COLOR);
    data->SetInt32(PROJ_MODE,           PROJ_MODE_FLAT_Z);
    data->SetVector(PROJ_DIRECTION,     Vector(0, 0, 1));
    data->SetBool(PROJ_AUTO_FIT,        true);
    data->SetBool(PROJ_INVERT_X,        false);
    data->SetBool(PROJ_INVERT_Y,        true);

    data->SetFloat(UV_OFFSET_X,         0.0);
    data->SetFloat(UV_OFFSET_Y,         0.0);
    data->SetFloat(UV_SCALE_X,          1.0);
    data->SetFloat(UV_SCALE_Y,          1.0);
    data->SetFloat(UV_ROTATION,         0.0);

    data->SetFloat(LINE_WIDTH,          2.0);
    data->SetBool(DRAW_FILL,            false);
    data->SetBool(DRAW_OUTLINE,         true);
    data->SetFloat(OUTLINE_WIDTH,       1.0);
    data->SetVector(FG_COLOR,           Vector(1, 1, 1));
    data->SetVector(BG_COLOR,           Vector(0, 0, 0));
    data->SetBool(USE_ALPHA,            false);
    data->SetFloat(OPACITY,             100.0);
    data->SetInt32(SPLINE_SUBDIVISION,  4);
    data->SetInt32(LINE_CAP_STYLE,      LINE_CAP_ROUND);

    data->SetBool(PREVIEW_ENABLED,      true);
    data->SetInt32(PREVIEW_RESOLUTION,  PREVIEW_RES_256);
    data->SetBool(SHOW_BOUNDS,          false);
    data->SetBool(DEBUG_OUTPUT,         false);

    data->SetInt32(BAKE_WIDTH,          1024);
    data->SetInt32(BAKE_HEIGHT,         1024);
    data->SetInt32(BAKE_AA,             BAKE_AA_NONE);
    data->SetInt32(BAKE_FORMAT,         FORMAT_PNG);

    return true;
}

void GeometryProjectorObject::Free(GeListNode* node)
{
    if (m_cacheId != 0)
    {
        CacheRegistry::Instance().Remove(m_cacheId);
        m_cacheId = 0;
    }
}

Bool GeometryProjectorObject::Read(GeListNode* node, HyperFile* hf, Int32 level)
{
    if (!SUPER::Read(node, hf, level))
        return false;

    // Генерируем новый ID при загрузке файла для исключения коллизий с другими открытыми сценами
    m_cacheId = GenerateUniqueCacheID();
    BaseContainer* data = static_cast<BaseObject*>(node)->GetDataInstance();
    if (data)
    {
        data->SetInt64(PARAM_CACHE_ID, m_cacheId);
    }
    return true;
}

Bool GeometryProjectorObject::CopyTo(NodeData* dest, GeListNode* snode, GeListNode* dnode,
                                       COPYFLAGS flags, AliasTrans* trn)
{
    GeometryProjectorObject* destObj = static_cast<GeometryProjectorObject*>(dest);
    if (destObj)
    {
        // Оптимизация: рендер-клоны наследуют оригинальный кэш, пользовательские копии получают новый ID
        if (flags & COPYFLAGS::DOCUMENT)
        {
            destObj->m_cacheId = m_cacheId;
        }
        else
        {
            destObj->m_cacheId = GenerateUniqueCacheID();
        }

        BaseContainer* destData = dnode->GetDataInstance();
        if (destData)
        {
            destData->SetInt64(PARAM_CACHE_ID, destObj->m_cacheId);
        }
    }
    return SUPER::CopyTo(dest, snode, dnode, flags, trn);
}

// ==================== GetVirtualObjects ====================

BaseObject* GeometryProjectorObject::GetVirtualObjects(BaseObject* op, HierarchyHelp* hh)
{
    BaseDocument* doc = op->GetDocument();
    if (!doc) return BaseObject::Alloc(Onull);

    BaseContainer* data = op->GetDataInstance();
    if (!data) return BaseObject::Alloc(Onull);

    Bool dirty = op->IsDirty(DIRTYFLAGS_DATA | DIRTYFLAGS_MATRIX | DIRTYFLAGS_CACHE);

    if (!dirty)
    {
        InExcludeData* srcList = static_cast<InExcludeData*>(
            data->GetCustomDataType(SOURCE_OBJECTS, CUSTOMDATATYPE_INEXCLUDE_LIST));
        if (srcList)
        {
            Int32 cnt = srcList->GetObjectCount();
            for (Int32 i = 0; i < cnt && !dirty; i++)
            {
                BaseObject* srcObj = static_cast<BaseObject*>(srcList->ObjectFromIndex(doc, i));
                if (srcObj && srcObj->IsDirty(DIRTYFLAGS_MATRIX | DIRTYFLAGS_DATA | DIRTYFLAGS_CACHE))
                    dirty = true;
            }
        }
    }

    ProjectionCache* cache = GetCache();

    if (dirty || !cache || !cache->rasterValid)
        DoUpdate(op, doc);

    return BaseObject::Alloc(Onull);
}

// ==================== DoUpdate ====================

void GeometryProjectorObject::DoUpdate(BaseObject* op, BaseDocument* doc)
{
    BaseContainer* data = op->GetDataInstance();
    if (!data) return;

    if (!data->GetBool(PREVIEW_ENABLED, true)) return;

    std::vector<BaseObject*> srcObjs = GetSourceObjects(op, doc);
    if (srcObjs.empty()) return;

    Int32 previewRes = GetResolutionFromParam(data->GetInt32(PREVIEW_RESOLUTION, PREVIEW_RES_256));

    BaseObject* targetObj = srcObjs[0];
    ProjectionSettings settings = ProjectionSettings::FromContainer(data, previewRes, targetObj, doc);

    ProjectionCache* cache = GetCache();
    if (!cache) return;

    std::map<Int64, ObjectDirtyState> currentStates;
    for (BaseObject* obj : srcObjs)
    {
        if (obj)
            currentStates[(Int64)(intptr_t)obj] = GetObjectDirtyState(obj);
    }

    UInt32 projHash   = ComputeProjectionHash(settings);
    UInt32 rasterHash = ComputeRasterHash(settings);

    CacheUpdateLevel level = cache->GetUpdateLevel(currentStates, projHash, rasterHash, previewRes);

    if (level == CacheUpdateLevel::NONE) return;

    if (level >= CacheUpdateLevel::GEOMETRY)
    {
        GeometryCollector collector;
        collector.Collect(srcObjs, doc, settings.splineSubdivision);
        cache->geometry           = collector.GetGeometry();
        cache->objectDirtyStates  = currentStates;
        cache->geometryValid      = true;
        cache->projectionValid    = false;
        cache->rasterValid        = false;
    }

    if (level >= CacheUpdateLevel::PROJECTION)
    {
        GeometryProjector projector;
        projector.Project(cache->geometry, settings, cache->projected);
        cache->projectionHash  = projHash;
        cache->projectionValid = true;
        cache->rasterValid     = false;
    }

    if (level >= CacheUpdateLevel::RASTER)
    {
        Rasterizer rasterizer;
        BaseBitmap* newBitmap = rasterizer.Rasterize(cache->projected, cache->geometry, settings);

        cache->SetBitmap(newBitmap);
        cache->rasterHash  = rasterHash;
        cache->rasterValid = true;
        cache->version.fetch_add(1);
    }

    if (cache->bitmap)
        UpdateMaterial(op, doc);
}

// ==================== Helpers ====================

std::vector<BaseObject*> GeometryProjectorObject::GetSourceObjects(BaseObject* op,
                                                                      BaseDocument* doc)
{
    std::vector<BaseObject*> result;
    BaseContainer* data = op->GetDataInstance();
    if (!data) return result;

    InExcludeData* srcList = static_cast<InExcludeData*>(
        data->GetCustomDataType(SOURCE_OBJECTS, CUSTOMDATATYPE_INEXCLUDE_LIST));
    if (!srcList) return result;

    Int32 cnt = srcList->GetObjectCount();
    for (Int32 i = 0; i < cnt; i++)
    {
        BaseObject* obj = static_cast<BaseObject*>(srcList->ObjectFromIndex(doc, i));
        if (obj) result.push_back(obj);
    }
    return result;
}

Int32 GeometryProjectorObject::GetResolutionFromParam(Int32 paramValue)
{
    switch (paramValue)
    {
        case PREVIEW_RES_32:   return 32;
        case PREVIEW_RES_64:   return 64;
        case PREVIEW_RES_128:  return 128;
        case PREVIEW_RES_256:  return 256;
        case PREVIEW_RES_512:  return 512;
        case PREVIEW_RES_1024: return 1024;
        case PREVIEW_RES_2048: return 2048;
        default:               return 256;
    }
}

ProjectionCache* GeometryProjectorObject::GetCache()
{
    if (m_cacheId == 0)
    {
        m_cacheId = GenerateUniqueCacheID();
        BaseContainer* data = GetDataInstance();
        if (data)
            data->SetInt64(PARAM_CACHE_ID, m_cacheId);
    }
    return CacheRegistry::Instance().GetOrCreate(m_cacheId);
}

void GeometryProjectorObject::UpdateMaterial(BaseObject* op, BaseDocument* doc)
{
    BaseContainer* data = op->GetDataInstance();
    if (!data) return;

    BaseMaterial* mat = static_cast<BaseMaterial*>(
        data->GetLink(TARGET_MATERIAL, doc, Mmaterial));
    if (!mat) return;

    mat->Message(MSG_UPDATE);
    mat->Update(true, true);
    EventAdd();
}

void GeometryProjectorObject::CreateShader(BaseObject* op, BaseDocument* doc)
{
    BaseContainer* data = op->GetDataInstance();
    if (!data) return;

    BaseMaterial* mat = static_cast<BaseMaterial*>(
        data->GetLink(TARGET_MATERIAL, doc, Mmaterial));
    if (!mat)
    {
        MessageDialog("Please assign a Target Material first."_s);
        return;
    }

    BaseShader* shader = BaseShader::Alloc(PLUGIN_ID_PROJECTION_SHADER);
    if (!shader)
    {
        MessageDialog("Failed to allocate ProjectionShader."_s);
        return;
    }

    BaseContainer* shaderData = shader->GetDataInstance();
    if (shaderData)
    {
        shaderData->SetLink(SHADER_PROJECTOR_LINK, op);
    }

    Int32 channelParam = data->GetInt32(TARGET_CHANNEL, CHANNEL_COLOR);
    Int32 channelShaderId = GetMaterialChannelId(channelParam);

    // Добавлена полноценная отмена (Undo) для сохранения истории действий пользователя
    doc->StartUndo();

    mat->InsertShader(shader);
    doc->AddUndo(UNDOTYPE_NEW, shader);
    doc->AddUndo(UNDOTYPE_CHANGE, mat);

    BaseContainer* matData = mat->GetDataInstance();
    if (matData)
    {
        matData->SetLink(channelShaderId, shader);
        Int32 useFlag = GetChannelUseFlag(channelParam);
        matData->SetBool(useFlag, true);
    }

    doc->EndUndo();

    mat->Message(MSG_UPDATE);
    mat->Update(true, true);
    EventAdd();
}

void GeometryProjectorObject::BakeToFile(BaseObject* op, BaseDocument* doc)
{
    BaseContainer* data = op->GetDataInstance();
    if (!data) return;

    Filename bakePath = data->GetFilename(BAKE_PATH);
    if (!bakePath.IsPopulated())
    {
        MessageDialog("Please set a Bake Path first."_s);
        return;
    }

    std::vector<BaseObject*> srcObjs = GetSourceObjects(op, doc);
    if (srcObjs.empty())
    {
        MessageDialog("Source Objects list is empty."_s);
        return;
    }

    Int32 bakeW  = data->GetInt32(BAKE_WIDTH,  1024);
    Int32 bakeH  = data->GetInt32(BAKE_HEIGHT, 1024);
    Int32 bakeAA = data->GetInt32(BAKE_AA,     BAKE_AA_NONE);

    Int32 renderW = bakeW;
    Int32 renderH = bakeH;
    if (bakeAA == BAKE_AA_2X) { renderW *= 2; renderH *= 2; }
    if (bakeAA == BAKE_AA_4X) { renderW *= 4; renderH *= 4; }

    BaseObject* targetObj = srcObjs[0];
    ProjectionSettings settings = ProjectionSettings::FromContainer(data, renderW, targetObj, doc);
    settings.previewResolution = renderW;

    GeometryCollector collector;
    collector.Collect(srcObjs, doc, settings.splineSubdivision);

    GeometryProjector projector;
    ProjectedGeometry projected;
    projector.Project(collector.GetGeometry(), settings, projected);

    Rasterizer rasterizer;
    BaseBitmap* bitmap = rasterizer.Rasterize(projected, collector.GetGeometry(), settings);
    if (!bitmap)
    {
        MessageDialog("Rasterization failed."_s);
        return;
    }

    BaseBitmap* finalBitmap = bitmap;
    if (bakeAA != BAKE_AA_NONE)
    {
        BaseBitmap* scaledBm = BaseBitmap::Alloc();
        if (scaledBm && scaledBm->Init(bakeW, bakeH, 24) == IMAGERESULT::OK)
        {
            IMAGERESULT scaleResult = bitmap->ScaleBicubic(
                scaledBm,
                0, 0, renderW - 1, renderH - 1,
                0, 0, bakeW  - 1, bakeH  - 1);

            BaseBitmap::Free(bitmap);
            if (scaleResult == IMAGERESULT::OK)
                finalBitmap = scaledBm;
            else
            {
                BaseBitmap::Free(scaledBm);
                MessageDialog("Downscale failed."_s);
                return;
            }
        }
        else
        {
            if (scaledBm) BaseBitmap::Free(scaledBm);
            BaseBitmap::Free(bitmap);
            MessageDialog("Could not allocate downscale bitmap."_s);
            return;
        }
    }

    Int32 saveFormat = FILTER_PNG;
    switch (data->GetInt32(BAKE_FORMAT, FORMAT_PNG))
    {
        case FORMAT_PNG:  saveFormat = FILTER_PNG; break;
        case FORMAT_TIFF: saveFormat = FILTER_TIF; break;
        case FORMAT_JPG:  saveFormat = FILTER_JPG; break;
        case FORMAT_PSD:  saveFormat = FILTER_PSD; break;
    }

    IMAGERESULT ir = finalBitmap->Save(bakePath, saveFormat, nullptr, SAVEBIT_NONE);
    BaseBitmap::Free(finalBitmap);

    if (ir == IMAGERESULT::OK)
        MessageDialog("Texture saved: "_s + bakePath.GetString());
    else
        MessageDialog("File save failed."_s);
}

// ==================== Message ====================

Bool GeometryProjectorObject::Message(GeListNode* node, Int32 type, void* data)
{
    BaseObject* op = static_cast<BaseObject*>(node);

    if (type == MSG_DESCRIPTION_COMMAND)
    {
        DescriptionCommand* dc = static_cast<DescriptionCommand*>(data);
        if (dc)
        {
            Int32         descId = dc->_descId[0].id;
            BaseDocument* doc   = op->GetDocument();

            if (descId == BTN_CREATE_SHADER)       { CreateShader(op, doc); return true; }
            else if (descId == BTN_BAKE)           { BakeToFile(op, doc);   return true; }
            else if (descId == BTN_REFRESH)
            {
                ProjectionCache* cache = GetCache();
                if (cache) cache->InvalidateAll();
                op->SetDirty(DIRTYFLAGS_DATA);
                EventAdd();
                return true;
            }
        }
    }
    else if (type == MSG_DESCRIPTION_POSTSETPARAMETER)
    {
        op->SetDirty(DIRTYFLAGS_DATA);
    }

    return SUPER::Message(node, type, data);
}

// ==================== GetDEnabling ====================

Bool GeometryProjectorObject::GetDEnabling(GeListNode* node, const DescID& id,
                                             const GeData& t_data,
                                             DESCFLAGS_ENABLE flags,
                                             const BaseContainer* itemdesc)
{
    BaseObject*    op   = static_cast<BaseObject*>(node);
    BaseContainer* data = op->GetDataInstance();
    if (!data) return true;

    Int32 paramId = id[0].id;

    if (paramId == PROJ_DIRECTION)
        return data->GetInt32(PROJ_MODE) == PROJ_MODE_CUSTOM;

    if (paramId == PROJ_CAMERA_LINK)
        return data->GetInt32(PROJ_MODE) == PROJ_MODE_CAMERA;

    if (paramId == OUTLINE_WIDTH)
        return data->GetBool(DRAW_OUTLINE, true);

    return SUPER::GetDEnabling(node, id, t_data, flags, itemdesc);
}

// ==================== Draw ====================

DRAWRESULT GeometryProjectorObject::Draw(BaseObject* op, DRAWPASS drawpass,
                                           BaseDraw* bd, BaseDrawHelp* bh)
{
    if (drawpass != DRAWPASS::OBJECT)
        return DRAWRESULT::SKIP;

    BaseContainer* data = op->GetDataInstance();
    if (data && data->GetBool(SHOW_BOUNDS, false))
        DrawBounds(op, bd, bh);

    return DRAWRESULT::OK;
}

void GeometryProjectorObject::DrawBounds(BaseObject* op, BaseDraw* bd, BaseDrawHelp* bh)
{
    ProjectionCache* cache = GetCache();
    if (!cache || !cache->geometryValid) return;

    const CollectedGeometry& geom = cache->geometry;
    if (geom.points.empty()) return;

    Vector bmin = geom.points[0];
    Vector bmax = geom.points[0];
    for (const Vector& pt : geom.points)
    {
        if (pt.x < bmin.x) bmin.x = pt.x;
        if (pt.y < bmin.y) bmin.y = pt.y;
        if (pt.z < bmin.z) bmin.z = pt.z;
        if (pt.x > bmax.x) bmax.x = pt.x;
        if (pt.y > bmax.y) bmax.y = pt.y;
        if (pt.z > bmax.z) bmax.z = pt.z;
    }

    bd->SetMatrix_Matrix(nullptr, Matrix());
    bd->SetPen(GetViewColor(VIEWCOLOR_SELECTION_PREVIEW));

    Vector corners[8] = {
        Vector(bmin.x, bmin.y, bmin.z), Vector(bmax.x, bmin.y, bmin.z),
        Vector(bmax.x, bmax.y, bmin.z), Vector(bmin.x, bmax.y, bmin.z),
        Vector(bmin.x, bmin.y, bmax.z), Vector(bmax.x, bmin.y, bmax.z),
        Vector(bmax.x, bmax.y, bmax.z), Vector(bmin.x, bmax.y, bmax.z)
    };

    static const Int32 edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    for (auto& e : edges)
        bd->DrawLine(corners[e[0]], corners[e[1]], 0);
}
