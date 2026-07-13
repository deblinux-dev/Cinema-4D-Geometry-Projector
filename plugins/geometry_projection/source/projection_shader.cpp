#include "projection_shader.h"
#include "projector_object.h"
#include "description/Xprojectionshader.h"

// ==================== Alloc / Init ====================

NodeData* ProjectionShader::Alloc()
{
    return NewObjClear(ProjectionShader);
}

Bool ProjectionShader::Init(GeListNode* node)
{
    BaseShader*    sh   = static_cast<BaseShader*>(node);
    BaseContainer* data = sh->GetDataInstance();
    if (!data) return false;

    data->SetInt32(SHADER_BLEND_MODE, BLEND_REPLACE);
    return true;
}

SHADERINFO ProjectionShader::GetRenderInfo(BaseShader* sh)
{
    return SHADERINFO::NONE;
}

// ==================== InitRender ====================

INITRENDERRESULT ProjectionShader::InitRender(BaseShader* sh, const InitRenderStruct& irs)
{
    // Reset the per-render snapshot state. The actual pixel data is fetched
    // lazily in Output() so it always reflects the latest cache version.
    m_pixelData.clear();
    m_bitmapWidth    = 0;
    m_bitmapHeight   = 0;
    m_snapshotVersion = -1;
    m_cacheId        = 0;

    BaseContainer* data = sh->GetDataInstance();
    if (!data) return INITRENDERRESULT::OK;

    m_blendMode = data->GetInt32(SHADER_BLEND_MODE, BLEND_REPLACE);

    BaseDocument* doc = irs.doc;
    if (!doc) return INITRENDERRESULT::OK;

    // Resolve the projector object and stash its persistent cache ID.
    // The ID is identical for the original projector and its render clones
    // (set in Init/CopyTo), so the shader can always reach the right cache
    // through the global CacheRegistry, even from a render-thread clone.
    BaseObject* projObj = static_cast<BaseObject*>(
        data->GetLink(SHADER_PROJECTOR_LINK, doc, PLUGIN_ID_PROJECTOR_OBJECT));
    if (!projObj) return INITRENDERRESULT::OK;

    BaseContainer* projData = projObj->GetDataInstance();
    if (!projData) return INITRENDERRESULT::OK;

    Int64 cacheId = projData->GetInt64(PARAM_CACHE_ID);
    if (cacheId == 0)
        cacheId = (Int64)(intptr_t)projObj; // fallback (editor, not render clone)
    m_cacheId = cacheId;

    return INITRENDERRESULT::OK;
}

void ProjectionShader::FreeRender(BaseShader* sh)
{
    m_pixelData.clear();
    m_bitmapWidth    = 0;
    m_bitmapHeight   = 0;
    m_snapshotVersion = -1;
}

// ==================== Output ====================

Vector ProjectionShader::Output(BaseShader* sh, ChannelData* cd)
{
    // Lazily refresh the pixel snapshot if the projector's cache bitmap
    // changed since the last sample. This is what makes the shader update in
    // real time in the viewport when the user moves/edits source objects or
    // the camera, instead of only when the timeline plays.
    RefreshSnapshot(sh);

    if (m_pixelData.empty() || m_bitmapWidth == 0)
        return CheckerPattern(cd->p.x, cd->p.y);

    Vector sample = SamplePixelData(cd->p.x, cd->p.y);

    if (m_blendMode == BLEND_REPLACE)
        return sample;

    // Note: a true base surface color is not available in Output() (the shader
    // output IS the channel color; blending with the material base is handled
    // by C4D's material system). Non-replace modes therefore return the sample
    // directly for now; the blend enum is kept for future VolumeData-based use.
    return sample;
}

// ==================== RefreshSnapshot ====================

void ProjectionShader::RefreshSnapshot(BaseShader* sh)
{
    Int64 cacheId = m_cacheId;

    // If InitRender did not resolve a cache ID yet (e.g. viewport preview
    // before the first render), try to resolve it now from the shader's link.
    if (cacheId == 0)
    {
        BaseContainer* data = sh->GetDataInstance();
        if (!data) return;
        BaseDocument* doc = sh->GetDocument();
        if (!doc) return;
        BaseObject* projObj = static_cast<BaseObject*>(
            data->GetLink(SHADER_PROJECTOR_LINK, doc, PLUGIN_ID_PROJECTOR_OBJECT));
        if (!projObj) return;
        BaseContainer* projData = projObj->GetDataInstance();
        if (!projData) return;
        cacheId = projData->GetInt64(PARAM_CACHE_ID);
        if (cacheId == 0) cacheId = (Int64)(intptr_t)projObj;
        m_cacheId = cacheId;
    }
    if (cacheId == 0) return;

    ProjectionCache* cache = CacheRegistry::Instance().Get(cacheId);
    if (!cache) return;

    Int32 currentVersion = cache->version.load(std::memory_order_acquire);

    // Fast path: snapshot already current. Read m_snapshotVersion under the
    // lock to avoid tearing with a concurrent refresh.
    {
        std::lock_guard<std::mutex> lock(m_refreshMutex);
        if (currentVersion == m_snapshotVersion && !m_pixelData.empty())
            return;
    }

    // Slow path: copy the cache bitmap's pixels into our private buffer.
    // SnapshotPixels() takes the cache's bitmap mutex, so this is safe even if
    // the projector replaces the bitmap concurrently.
    std::vector<uint8_t> newPixels;
    Int32 newW = 0, newH = 0;
    if (!cache->SnapshotPixels(newPixels, newW, newH))
        return;

    std::lock_guard<std::mutex> lock(m_refreshMutex);
    m_pixelData      = std::move(newPixels);
    m_bitmapWidth    = newW;
    m_bitmapHeight   = newH;
    m_snapshotVersion = currentVersion;
}

// ==================== SamplePixelData ====================

Vector ProjectionShader::SamplePixelData(Float u, Float v) const
{
    // Clamp UV to [0..1]
    if (u < 0.0) u = 0.0; else if (u > 1.0) u = 1.0;
    if (v < 0.0) v = 0.0; else if (v > 1.0) v = 1.0;

    Int32 px = (Int32)(u * (Float)(m_bitmapWidth  - 1) + 0.5);

    // Оптимизация: Корректное обратное преобразование оси V для устранения перевернутой текстуры
    Int32 py = (Int32)((1.0 - v) * (Float)(m_bitmapHeight - 1) + 0.5);

    if (px < 0) px = 0; else if (px >= m_bitmapWidth)  px = m_bitmapWidth  - 1;
    if (py < 0) py = 0; else if (py >= m_bitmapHeight) py = m_bitmapHeight - 1;

    const uint8_t* p = m_pixelData.data() + (py * m_bitmapWidth + px) * 3;

    return Vector(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0);
}

// ==================== ApplyBlend ====================

Vector ProjectionShader::ApplyBlend(const Vector& base, const Vector& sample) const
{
    switch (m_blendMode)
    {
        case BLEND_MULTIPLY:
            return Vector(base.x * sample.x,
                          base.y * sample.y,
                          base.z * sample.z);

        case BLEND_ADD:
        {
            Float r = base.x + sample.x;
            Float g = base.y + sample.y;
            Float b = base.z + sample.z;
            return Vector(r > 1.0 ? 1.0 : r,
                          g > 1.0 ? 1.0 : g,
                          b > 1.0 ? 1.0 : b);
        }

        case BLEND_OVERLAY:
        {
            auto ch = [](Float a, Float b) -> Float
            {
                return (a < 0.5) ? (2.0 * a * b)
                                 : (1.0 - 2.0 * (1.0 - a) * (1.0 - b));
            };
            return Vector(ch(base.x, sample.x),
                          ch(base.y, sample.y),
                          ch(base.z, sample.z));
        }

        default:
            return sample;
    }
}

// ==================== CheckerPattern ====================

Vector ProjectionShader::CheckerPattern(Float u, Float v)
{
    Int32 iu = (Int32)(u * 8.0);
    Int32 iv = (Int32)(v * 8.0);
    Float c = ((iu + iv) % 2 == 0) ? 0.4 : 0.6;
    return Vector(c, c, c);
}

// ==================== Registration ====================

Bool RegisterProjectionShader()
{
    BaseBitmap* icon = nullptr;
    {
        Filename resDir = GeGetPluginResourcePath();
        Filename iconsDir = resDir + Filename("icons"_s);
        Filename iconPath = iconsDir + Filename("xprojectionshader.png"_s);
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

    if (!RegisterShaderPlugin(PLUGIN_ID_PROJECTION_SHADER, "Projection Shader"_s,
            0, ProjectionShader::Alloc, "Xprojectionshader"_s, 0))
        return false;

    return true;
}
