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
    m_pixelData.clear();
    m_bitmapWidth    = 0;
    m_bitmapHeight   = 0;
    m_snapshotVersion = -1;

    BaseContainer* data = sh->GetDataInstance();
    if (!data) return INITRENDERRESULT::OK;

    m_blendMode = data->GetInt32(SHADER_BLEND_MODE, BLEND_REPLACE);

    BaseDocument* doc = irs.doc;
    if (!doc) return INITRENDERRESULT::OK;

    // Resolve the projector object via the link parameter
    BaseObject* projObj = static_cast<BaseObject*>(
        data->GetLink(SHADER_PROJECTOR_LINK, doc, PLUGIN_ID_PROJECTOR_OBJECT));
    if (!projObj) return INITRENDERRESULT::OK;

    BaseContainer* projData = projObj->GetDataInstance();
    if (!projData) return INITRENDERRESULT::OK;

    // Получаем постоянный сессионный ID из контейнера для связки оригинального объекта и рендер-клона
    Int64 cacheId = projData->GetInt64(PARAM_CACHE_ID);
    if (cacheId == 0)
        cacheId = (Int64)(intptr_t)projObj; // Запасной вариант

    ProjectionCache* cache = CacheRegistry::Instance().Get(cacheId);
    if (!cache || !cache->bitmap || !cache->rasterValid)
        return INITRENDERRESULT::OK;

    Int32 bw = cache->bitmap->GetBw();
    Int32 bh = cache->bitmap->GetBh();
    if (bw <= 0 || bh <= 0) return INITRENDERRESULT::OK;

    m_bitmapWidth  = bw;
    m_bitmapHeight = bh;
    m_pixelData.resize(bw * bh * 3);

    // Read pixel data row by row via GetPixelCnt.
    std::vector<uint8_t> rowBuf(bw * 3);
    for (Int32 y = 0; y < bh; y++)
    {
        cache->bitmap->GetPixelCnt(0, y, bw, rowBuf.data(), 3, COLORMODE::RGB, PIXELCNT::NONE);
        uint8_t* dst = m_pixelData.data() + y * bw * 3;
        for (Int32 x = 0; x < bw; x++)
        {
            dst[x * 3 + 0] = rowBuf[x * 3 + 0];
            dst[x * 3 + 1] = rowBuf[x * 3 + 1];
            dst[x * 3 + 2] = rowBuf[x * 3 + 2];
        }
    }

    m_snapshotVersion = cache->version.load();

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
    if (m_pixelData.empty() || m_bitmapWidth == 0)
        return CheckerPattern(cd->p.x, cd->p.y);

    Vector sample = SamplePixelData(cd->p.x, cd->p.y);

    if (m_blendMode == BLEND_REPLACE)
        return sample;

    return ApplyBlend(cd->t, sample);
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
