#include "projector_object.h"

// КУВАЛДА: Форсируем чтение именно ваших новых файлов по прямому относительному пути!
#include "../res/description/Ogeometryprojector.h"
#include "../res/description/Xprojectionshader.h"

#include "customgui_inexclude.h"
#include "c4d_basedocument.h"
#include "c4d_basetag.h"
#include "c4d_libs/lib_collider.h"
#include "projection_shader.h"

// ---- Find the ProjectionShader inside a material (recursive) ----
// Searches top-level shaders and their children so the shader is found even
// if it's nested inside a Fusion/Layer shader.
static BaseShader* FindProjectionShaderRecursive(BaseShader* shd)
{
    while (shd)
    {
        if (shd->IsInstanceOf(PLUGIN_ID_PROJECTION_SHADER))
            return shd;
        BaseShader* found = FindProjectionShaderRecursive(shd->GetDown());
        if (found) return found;
        shd = shd->GetNext();
    }
    return nullptr;
}

// ---- Material channel ID lookup ----

static Int32 GetMaterialChannelId(Int32 channelParam)
{
    switch (channelParam)
    {
        case TCH_COLOR:     return MATERIAL_COLOR_SHADER;
        case TCH_LUMINANCE: return MATERIAL_LUMINANCE_SHADER;
        case TCH_ALPHA:     return MATERIAL_ALPHA_SHADER;
        case TCH_BUMP:      return MATERIAL_BUMP_SHADER;
        case TCH_DIFFUSION: return MATERIAL_DIFFUSION_SHADER;
        default:                return MATERIAL_COLOR_SHADER;
    }
}

// ---- Channel enable flag lookup ----

static Int32 GetChannelUseFlag(Int32 channelParam)
{
    switch (channelParam)
    {
        case TCH_COLOR:     return MATERIAL_USE_COLOR;
        case TCH_LUMINANCE: return MATERIAL_USE_LUMINANCE;
        case TCH_ALPHA:     return MATERIAL_USE_ALPHA;
        case TCH_BUMP:      return MATERIAL_USE_BUMP;
        case TCH_DIFFUSION: return MATERIAL_USE_DIFFUSION;
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

    m_cacheId = GenerateUniqueCacheID();
    data->SetInt64(PARAM_CACHE_ID, m_cacheId);

    // Default values
    data->SetInt32(TARGET_CHANNEL,      TCH_COLOR);
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

    m_cacheId = GenerateUniqueCacheID();
    BaseObject* op = (BaseObject*)node;
    BaseContainer* data = op->GetDataInstance();
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
        if (flags & COPYFLAGS::DOCUMENT)
        {
            destObj->m_cacheId = m_cacheId;
        }
        else
        {
            destObj->m_cacheId = GenerateUniqueCacheID();
        }

        BaseContainer* destData = static_cast<BaseObject*>(dnode)->GetDataInstance();
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

    // Official dependency tracking via AddDependence(): C4D monitors every
    // object we add to the list and automatically re-invokes GetVirtualObjects
    // when ANY of them changes (move/rotate/scale/param edit). This is the
    // correct mechanism for generators that depend on non-child objects
    // (InExclude list, linked target, linked camera). It replaces the fragile
    // CheckDirty + IsDirty checksum hacks which only reliably tracked the
    // first source object and could desync after add/remove/reorder.
    op->NewDependenceList();

    // Source objects (geometry to project)
    InExcludeData* srcList = (InExcludeData*)data->GetCustomDataType(SOURCE_OBJECTS, CUSTOMDATATYPE_INEXCLUDE_LIST);
    if (srcList)
    {
        Int32 cnt = srcList->GetObjectCount();
        for (Int32 i = 0; i < cnt; i++)
        {
            BaseObject* srcObj = static_cast<BaseObject*>(srcList->ObjectFromIndex(doc, i));
            if (srcObj)
                op->AddDependence(hh, srcObj, DIRTYFLAGS::MATRIX | DIRTYFLAGS::DATA | DIRTYFLAGS::CACHE);
        }
    }

    // Target object (its world bounds drive the UV layout)
    BaseObject* tgtObj = static_cast<BaseObject*>(data->GetLink(TARGET_OBJECT, doc, Obase));
    if (tgtObj)
        op->AddDependence(hh, tgtObj, DIRTYFLAGS::MATRIX | DIRTYFLAGS::DATA | DIRTYFLAGS::CACHE);

    // Camera in camera mode (explicit link OR editor camera)
    if (data->GetInt32(PROJ_MODE, PROJ_MODE_FLAT_Z) == PROJ_MODE_CAMERA)
    {
        BaseObject* cam = static_cast<BaseObject*>(data->GetLink(PROJ_CAMERA_LINK, doc, Ocamera));
        if (!cam)
        {
            BaseDraw* bd = doc->GetActiveBaseDraw();
            if (bd)
            {
                cam = bd->GetSceneCamera(doc);
                if (!cam) cam = bd->GetEditorCamera();
            }
        }
        if (cam)
            op->AddDependence(hh, cam, DIRTYFLAGS::MATRIX | DIRTYFLAGS::DATA);
    }

    // CompareDependenceList returns true if NOTHING changed since last eval.
    Bool dirty = !op->CompareDependenceList();

    // Check if the projector object's own parameters changed (line width,
    // fill, outline, colors, projection mode, etc.). CompareDependenceList
    // only tracks external object dependencies, not the object's own data.
    // op->IsDirty(DATA) catches parameter edits.
    if (!dirty && op->IsDirty(DIRTYFLAGS::DATA))
        dirty = true;

    // Supplementary check: if the dependence list says "no change" but a
    // source object IsDirty (e.g. it was moved but C4D's dependence tracking
    // hasn't registered it yet -- which can happen when no target object is
    // set and the only dependencies are InExclude links), force a rebuild.
    // This ensures real-time updates work even without a target object.
    if (!dirty && srcList)
    {
        Int32 cnt = srcList->GetObjectCount();
        for (Int32 i = 0; i < cnt && !dirty; i++)
        {
            BaseObject* srcObj = static_cast<BaseObject*>(srcList->ObjectFromIndex(doc, i));
            if (srcObj && srcObj->IsDirty(DIRTYFLAGS::MATRIX | DIRTYFLAGS::DATA | DIRTYFLAGS::CACHE))
                dirty = true;
        }
    }

    ProjectionCache* cache = GetCache(op);

    if (dirty || !cache || !cache->rasterValid)
        DoUpdate(op, doc, hh);

    return BaseObject::Alloc(Onull);
}

// ==================== DoUpdate ====================

void GeometryProjectorObject::DoUpdate(BaseObject* op, BaseDocument* doc, HierarchyHelp* hh)
{
    BaseContainer* data = op->GetDataInstance();
    if (!data) return;

    if (!data->GetBool(PREVIEW_ENABLED, true)) return;

    std::vector<BaseObject*> srcObjs = GetSourceObjects(op, doc);
    if (srcObjs.empty()) return;

    Int32 previewRes = GetResolutionFromParam(data->GetInt32(PREVIEW_RESOLUTION, PREVIEW_RES_256));

    // The TARGET OBJECT is the surface the geometry is projected ONTO (e.g. the
    // plane). Its world-space bounding box defines the UV layout, so a 1x1 cube
    // on a 10x10 plane occupies 0.1x0.1 of UV. Previously this used srcObjs[0]
    // (the cube itself), which made the cube stretch to fill the whole UV.
    BaseObject* targetObj = static_cast<BaseObject*>(data->GetLink(TARGET_OBJECT, doc, Obase));
    ProjectionSettings settings = ProjectionSettings::FromContainer(data, previewRes, targetObj, doc, srcObjs);

    ProjectionCache* cache = GetCache(op);
    if (!cache) return;

    // With the AddDependence() list in GetVirtualObjects, C4D only calls
    // DoUpdate when something actually changed (a source/target/camera object
    // moved, or projection/raster settings changed). So we can simplify: if
    // the projection settings hash changed, re-project; if the raster hash
    // changed, re-rasterize. We dropped the objectDirtyStates map (which keyed
    // on object pointers and could desync after add/remove/reorder, causing
    // the plugin to 'break' and stop reacting until the projector was rebuilt).
    UInt32 projHash   = ComputeProjectionHash(settings);
    UInt32 rasterHash = ComputeRasterHash(settings);

    // Determine the cheapest update level that satisfies all changes.
    // If the projection hash changed we must re-project (and re-rasterize).
    // If only the raster hash changed we can skip re-projection.
    // If neither changed but the cache is invalid (first run / object added or
    // removed -> AddDependence fired) we do a full rebuild.
    CacheUpdateLevel level;
    if (!cache->geometryValid)
        level = CacheUpdateLevel::GEOMETRY;
    else if (projHash != cache->projectionHash)
        level = CacheUpdateLevel::PROJECTION;
    else if (rasterHash != cache->rasterHash || previewRes != cache->bitmapWidth)
        level = CacheUpdateLevel::RASTER;
    else
        // A dependency fired (object moved) but settings are unchanged -- the
        // GEOMETRY itself changed, so we must re-collect and re-project.
        level = CacheUpdateLevel::GEOMETRY;

    if (level == CacheUpdateLevel::NONE) return;

    if (level >= CacheUpdateLevel::GEOMETRY)
    {
        GeometryCollector collector;
        collector.Collect(srcObjs, doc, hh, settings.splineSubdivision,
                      settings.fgColor, settings.lineWidth, settings.drawFill);
        cache->geometry           = collector.GetGeometry();
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
        BaseBitmap* newBitmap = nullptr;

        if (settings.projMode == PROJ_MODE_UVFOLLOW)
        {
            // UV-follow Approach A: build/reuse UV→3D lookup, then render
            // source orthographically and sample via lookup. Fast (no
            // per-pixel ray-casting) and seamless.
            BaseObject* tgtObj = static_cast<BaseObject*>(data->GetLink(TARGET_OBJECT, doc, Obase));
            if (tgtObj)
            {
                // Check if target changed (dirty count) or resolution changed
                BaseObject* surfObj = tgtObj;
                if (!surfObj->IsInstanceOf(Opolygon))
                {
                    BaseObject* c = surfObj->GetCache();
                    while (c && !c->IsInstanceOf(Opolygon)) c = c->GetCache();
                    if (c) surfObj = c;
                }
                UInt32 tgtDirty = surfObj ? surfObj->GetDirty(DIRTYFLAGS::DATA | DIRTYFLAGS::CACHE) : 0;
                if (!cache->uvFollowLookupValid ||
                    cache->uvFollowTargetDirty != tgtDirty ||
                    cache->uvFollowResolution != previewRes)
                {
                    BuildUVFollowLookup(op, settings, cache);
                }
            }
            if (cache->uvFollowLookupValid)
            {
                Rasterizer rasterizer;
                newBitmap = rasterizer.RasterizeUVFollowApproachA(
                    cache->geometry, settings,
                    cache->uvFollowLookup, cache->uvFollowResolution);
            }
            else
            {
                Rasterizer rasterizer;
                newBitmap = rasterizer.Rasterize(cache->projected, cache->geometry, settings);
            }
        }
        else
        {
            Rasterizer rasterizer;
            newBitmap = rasterizer.Rasterize(cache->projected, cache->geometry, settings);
        }

        cache->SetBitmap(newBitmap);
        cache->rasterHash  = rasterHash;
        cache->rasterValid = true;
        cache->version.fetch_add(1);
    }

    if (cache->bitmap)
        UpdateMaterial(op, doc);
}

// ==================== Helpers ====================

std::vector<BaseObject*> GeometryProjectorObject::GetSourceObjects(BaseObject* op, BaseDocument* doc)
{
    std::vector<BaseObject*> result;
    BaseContainer* data = op->GetDataInstance();
    if (!data) return result;

    InExcludeData* srcList = (InExcludeData*)data->GetCustomDataType(SOURCE_OBJECTS, CUSTOMDATATYPE_INEXCLUDE_LIST);
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

// ==================== UV Follow Approach A ====================

void GeometryProjectorObject::BuildUVFollowLookup(BaseObject* op,
                                                     const ProjectionSettings& settings,
                                                     ProjectionCache* cache)
{
    if (!settings.targetSurfaceObj) { cache->uvFollowLookupValid = false; return; }

    // Find the polygon object (walk cache if target is a generator)
    BaseObject* surfObj = settings.targetSurfaceObj;
    if (!surfObj->IsInstanceOf(Opolygon))
    {
        BaseObject* c = surfObj->GetCache();
        while (c && !c->IsInstanceOf(Opolygon)) c = c->GetCache();
        if (!c) { c = surfObj->GetDown(); while (c && !c->IsInstanceOf(Opolygon)) c = c->GetNext(); }
        if (c) surfObj = c; else { cache->uvFollowLookupValid = false; return; }
    }
    if (!surfObj->IsInstanceOf(Opolygon)) { cache->uvFollowLookupValid = false; return; }

    BaseTag* tag = surfObj->GetTag(Tuvw);
    if (!tag) { cache->uvFollowLookupValid = false; return; }
    UVWTag* uvwTag = static_cast<UVWTag*>(tag);

    PolygonObject* polyObj = static_cast<PolygonObject*>(surfObj);
    Int32 polyCount = polyObj->GetPolygonCount();
    Int32 pointCount = polyObj->GetPointCount();
    if (polyCount <= 0 || pointCount <= 0) { cache->uvFollowLookupValid = false; return; }

    const Vector*   pts  = polyObj->GetPointR();
    const CPolygon* polys = polyObj->GetPolygonR();
    if (!pts || !polys) { cache->uvFollowLookupValid = false; return; }

    Matrix mg = surfObj->GetMg();

    Int32 res = settings.previewResolution;
    cache->uvFollowLookup.assign((size_t)res * res, Vector(0));
    cache->uvFollowResolution = res;

    // Build UV grid for spatial indexing: divide [0..1]² into 32×32 cells.
    const Int32 GRID = 32;
    std::vector<std::vector<Int32>> grid((size_t)GRID * GRID);

    // For each polygon, compute UV bounding box and add to overlapping cells
    for (Int32 pi = 0; pi < polyCount; pi++)
    {
        UVWStruct us = uvwTag->GetSlow(pi);
        Float minU = std::min({us.a.x, us.b.x, us.c.x});
        Float maxU = std::max({us.a.x, us.b.x, us.c.x});
        Float minV = std::min({us.a.y, us.b.y, us.c.y});
        Float maxV = std::max({us.a.y, us.b.y, us.c.y});
        if (polys[pi].c != polys[pi].d)
        {
            minU = std::min(minU, us.d.x); maxU = std::max(maxU, us.d.x);
            minV = std::min(minV, us.d.y); maxV = std::max(maxV, us.d.y);
        }
        Int32 x0 = (Int32)(minU * GRID); if (x0 < 0) x0 = 0; if (x0 >= GRID) x0 = GRID - 1;
        Int32 x1 = (Int32)(maxU * GRID); if (x1 < 0) x1 = 0; if (x1 >= GRID) x1 = GRID - 1;
        Int32 y0 = (Int32)(minV * GRID); if (y0 < 0) y0 = 0; if (y0 >= GRID) y0 = GRID - 1;
        Int32 y1 = (Int32)(maxV * GRID); if (y1 < 0) y1 = 0; if (y1 >= GRID) y1 = GRID - 1;
        for (Int32 gy = y0; gy <= y1; gy++)
            for (Int32 gx = x0; gx <= x1; gx++)
                grid[(size_t)gy * GRID + gx].push_back(pi);
    }

    // Point-in-triangle test with barycentric coordinates.
    // Returns true if (u,v) is inside triangle (p0,p1,p2) in 2D.
    // Outputs barycentric weights (w0, w1, w2).
    auto pointInTri = [](Float u, Float v,
                          Float p0u, Float p0v,
                          Float p1u, Float p1v,
                          Float p2u, Float p2v,
                          Float& w0, Float& w1, Float& w2) -> bool {
        Float v0u = p1u - p0u, v0v = p1v - p0v;
        Float v1u = p2u - p0u, v1v = p2v - p0v;
        Float v2u = u   - p0u, v2v = v   - p0v;
        Float dot00 = v0u * v0u + v0v * v0v;
        Float dot01 = v0u * v1u + v0v * v1v;
        Float dot02 = v0u * v2u + v0v * v2v;
        Float dot11 = v1u * v1u + v1v * v1v;
        Float dot12 = v1u * v2u + v1v * v2v;
        Float denom = dot00 * dot11 - dot01 * dot01;
        if (std::abs(denom) < 1e-12) return false;
        w1 = (dot11 * dot02 - dot01 * dot12) / denom;
        w2 = (dot00 * dot12 - dot01 * dot02) / denom;
        w0 = 1.0 - w1 - w2;
        return (w1 >= -1e-6 && w2 >= -1e-6 && w0 >= -1e-6);
    };

    // For each UV pixel, find containing polygon and compute 3D position
    for (Int32 py = 0; py < res; py++)
    {
        Float v = (Float)py / (Float)(res - 1);
        Int32 gy = (Int32)(v * GRID); if (gy >= GRID) gy = GRID - 1;
        for (Int32 px = 0; px < res; px++)
        {
            Float u = (Float)px / (Float)(res - 1);
            Int32 gx = (Int32)(u * GRID); if (gx >= GRID) gx = GRID - 1;

            const auto& cell = grid[(size_t)gy * GRID + gx];
            bool found = false;
            for (Int32 pi : cell)
            {
                if (found) break;
                UVWStruct us = uvwTag->GetSlow(pi);
                const CPolygon& poly = polys[pi];
                Float w0, w1, w2;

                // Triangle 1: a, b, c
                if (pointInTri(u, v, us.a.x, us.a.y, us.b.x, us.b.y, us.c.x, us.c.y, w0, w1, w2))
                {
                    Vector localP = pts[poly.a] * w0 + pts[poly.b] * w1 + pts[poly.c] * w2;
                    cache->uvFollowLookup[(size_t)py * res + px] = mg * localP;
                    found = true;
                    break;
                }
                // Triangle 2 (quad only): a, c, d
                if (poly.c != poly.d)
                {
                    if (pointInTri(u, v, us.a.x, us.a.y, us.c.x, us.c.y, us.d.x, us.d.y, w0, w1, w2))
                    {
                        Vector localP = pts[poly.a] * w0 + pts[poly.c] * w1 + pts[poly.d] * w2;
                        cache->uvFollowLookup[(size_t)py * res + px] = mg * localP;
                        found = true;
                        break;
                    }
                }
            }
            // If not found (UV gap), leave as (0,0,0) -- pixel won't be drawn
        }
    }

    cache->uvFollowLookupValid = true;
    cache->uvFollowTargetDirty = surfObj->GetDirty(DIRTYFLAGS::DATA | DIRTYFLAGS::CACHE);
    cache->uvFollowResolution = res;
}

ProjectionCache* GeometryProjectorObject::GetCache(BaseObject* op)
{
    if (m_cacheId == 0)
    {
        m_cacheId = GenerateUniqueCacheID();
        BaseContainer* data = op->GetDataInstance(); 
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

    // The viewport caches a GL texture for each shader.  When the projector's
    // bitmap changes, the cache is stale — but C4D does not know this because
    // the dependency is a custom link, not a parent-child relationship.  We
    // must explicitly invalidate the GL image so C4D regenerates it (calling
    // InitGLImage or falling back to Output()) on the next redraw.
    BaseShader* projShd = FindProjectionShaderRecursive(mat->GetFirstShader());
    if (projShd)
    {
        projShd->InvalidateGLImage(doc);
        projShd->SetDirty(DIRTYFLAGS::DATA);
        projShd->Message(MSG_UPDATE);
    }

    // Also mark the material dirty + send update so C4D re-evaluates the
    // full material pipeline for the viewport.
    mat->SetDirty(DIRTYFLAGS::DATA);
    mat->Message(MSG_UPDATE);
    mat->Update(true, true);
    EventAdd(EVENT::FORCEREDRAW);
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

    Int32 channelParam = data->GetInt32(TARGET_CHANNEL, TCH_COLOR);
    Int32 channelShaderId = GetMaterialChannelId(channelParam);

    doc->StartUndo();

    mat->InsertShader(shader);
    
    // ИСПРАВЛЕНИЕ: Точное имя константы Undo для Cinema 4D
    doc->AddUndo(UNDOTYPE::NEWOBJ, shader);
    doc->AddUndo(UNDOTYPE::CHANGE, mat);

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

    BaseObject* targetObj = static_cast<BaseObject*>(data->GetLink(TARGET_OBJECT, doc, Obase));
    ProjectionSettings settings = ProjectionSettings::FromContainer(data, renderW, targetObj, doc, srcObjs);
    settings.previewResolution = renderW;

    GeometryCollector collector;
    collector.Collect(srcObjs, doc, nullptr, settings.splineSubdivision,
                      settings.fgColor, settings.lineWidth, settings.drawFill);

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
            bitmap->ScaleBicubic(
                scaledBm,
                0, 0, renderW - 1, renderH - 1,
                0, 0, bakeW  - 1, bakeH  - 1);

            BaseBitmap::Free(bitmap);
            finalBitmap = scaledBm;
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

    IMAGERESULT ir = finalBitmap->Save(bakePath, saveFormat, nullptr, SAVEBIT::NONE);
    BaseBitmap::Free(finalBitmap);

    if (ir == IMAGERESULT::OK)
        MessageDialog(String("Texture saved: ") + bakePath.GetString());
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
                ProjectionCache* cache = GetCache(op);
                if (cache) cache->InvalidateAll();
                op->SetDirty(DIRTYFLAGS::DATA);
                EventAdd();
                return true;
            }
        }
    }
    else if (type == MSG_DESCRIPTION_POSTSETPARAMETER)
    {
        op->SetDirty(DIRTYFLAGS::DATA);
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
    ProjectionCache* cache = GetCache(op);
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

// ==================== Registration ====================

Bool RegisterGeometryProjectorObject()
{
    // Load the icon from res/icons/. GeGetPluginResourcePath() returns the res
    // directory; we append "icons" as a subdirectory, then the filename.
    // Using nested Filename operations (not String with "/") because the
    // Filename + String operator does NOT parse path separators on Windows.
    BaseBitmap* icon = nullptr;
    {
        Filename resDir = GeGetPluginResourcePath();
        Filename iconsDir = resDir + Filename("icons"_s);
        Filename iconPath = iconsDir + Filename("ogeometryprojector.png"_s);
        icon = BaseBitmap::Alloc();
        if (icon)
        {
            if (icon->Init(iconPath) != IMAGERESULT::OK)
            {
                BaseBitmap::Free(icon);
                icon = nullptr;
            }
        }
    }

    if (!RegisterObjectPlugin(PLUGIN_ID_PROJECTOR_OBJECT, "Geometry Projector"_s,
            OBJECT_GENERATOR, GeometryProjectorObject::Alloc,
            "Ogeometryprojector"_s, icon, 0))
        return false;

    return true;
}