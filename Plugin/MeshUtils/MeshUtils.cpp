#include "pch.h"
#include "MeshUtils.h"
#include "mikktspace.h"
#include "RawVector.h"
#include "IntrusiveArray.h"

#ifdef muEnableHalf
#ifdef _WIN32
    #pragma comment(lib, "half.lib")
#endif
#endif // muEnableHalf

namespace mu {

const float PI = 3.14159265358979323846264338327950288419716939937510f;
const float Deg2Rad = PI / 180.0f;


#ifdef muEnableHalf
void FloatToHalf_Generic(half *dst, const float *src, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i] = src[i];
    }
}
void HalfToFloat_Generic(float *dst, const half *src, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i] = src[i];
    }
}
#endif // muEnableHalf

void InvertX_Generic(float3 *dst, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i].x *= -1.0f;
    }
}
void InvertX_Generic(float4 *dst, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i].x *= -1.0f;
    }
}

void InvertV(float2 *dst, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i].y = 1.0f - dst[i].y;
    }
}


void Scale_Generic(float *dst, float s, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i] *= s;
    }
}
void Scale_Generic(float3 *dst, float s, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i] *= s;
    }
}

void ComputeBounds_Generic(const float3 *p, size_t num, float3& omin, float3& omax)
{
    if (num == 0) { return; }
    float3 rmin = p[0], rmax = p[0];
    for (size_t i = 1; i < num; ++i) {
        auto _ = p[i];
        rmin.x = std::min<float>(rmin.x, _.x);
        rmin.y = std::min<float>(rmin.y, _.y);
        rmin.z = std::min<float>(rmin.z, _.z);
        rmax.x = std::max<float>(rmax.x, _.x);
        rmax.y = std::max<float>(rmax.y, _.y);
        rmax.z = std::max<float>(rmax.z, _.z);
    }
    omin = rmin;
    omax = rmax;
}

void Normalize_Generic(float3 *dst, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        dst[i] = normalize(dst[i]);
    }
}

bool GenerateNormals(
    IArray<float3> dst, const IArray<float3> points,
    const IArray<int> counts, const IArray<int> offsets, const IArray<int> indices)
{
    if (dst.size() != points.size()) {
        return false;
    }
    dst.zeroclear();

    size_t num_faces = counts.size();
    for (size_t fi = 0; fi < num_faces; ++fi)
    {
        int count = counts[fi];
        const int *face = &indices[offsets[fi]];
        float3 p0 = points[face[0]];
        float3 p1 = points[face[1]];
        float3 p2 = points[face[2]];
        float3 n = cross(p1 - p0, p2 - p0);
        for (int ci = 0; ci < count; ++ci) {
            dst[face[ci]] += n;
        }
    }
    Normalize(dst.data(), dst.size());
    return true;
}

void TopologyRefiner::prepare(
    const IArray<int>& counts_, const IArray<int>& offsets_, const IArray<int>& indices_, const IArray<float3>& points_)
{
    counts = counts_;
    offsets = offsets_;
    indices = indices_;
    points = points_;
    normals.reset(nullptr, 0);
    uv.reset(nullptr, 0);

    v2f_counts.clear();
    v2f_offsets.clear();
    shared_faces.clear();
    shared_indices.clear();
    face_normals.clear();
    vertex_normals.clear();
    tangents.clear();

    new_points.clear();
    new_normals.clear();
    new_uv.clear();
    new_indices.clear();
    old2new.clear();

    splits.clear();
}

void TopologyRefiner::genNormals()
{
    vertex_normals.resize(points.size());
    vertex_normals.zeroclear();

    size_t num_faces = counts.size();
    for (size_t fi = 0; fi < num_faces; ++fi)
    {
        int count = counts[fi];
        const int *face = &indices[offsets[fi]];
        float3 p0 = points[face[0]];
        float3 p1 = points[face[1]];
        float3 p2 = points[face[2]];
        float3 n = cross(p1 - p0, p2 - p0);
        for (int ci = 0; ci < count; ++ci) {
            vertex_normals[face[ci]] += n;
        }
    }
    Normalize(vertex_normals.data(), vertex_normals.size());

    normals = vertex_normals;
}

void TopologyRefiner::genNormals(float smooth_angle)
{
    if (v2f_counts.empty()) { buildConnection(); }

    size_t num_indices = indices.size();
    size_t num_faces = counts.size();
    vertex_normals.resize(num_indices);

    // gen face normals
    face_normals.resize(num_faces);
    face_normals.zeroclear();
    for (size_t fi = 0; fi < num_faces; ++fi)
    {
        int offset = offsets[fi];
        const int *face = &indices[offset];
        float3 p0 = points[face[0]];
        float3 p1 = points[face[1]];
        float3 p2 = points[face[2]];
        float3 n = cross(p1 - p0, p2 - p0);
        face_normals[fi] = n;
    }
    Normalize(face_normals.data(), face_normals.size());

    // gen vertex normals
    const float angle = std::cos(smooth_angle * Deg2Rad) - 0.001f;
    for (size_t fi = 0; fi < num_faces; ++fi)
    {
        int count = counts[fi];
        int offset = offsets[fi];
        const int *face = &indices[offset];
        auto& face_normal = face_normals[fi];
        for (int ci = 0; ci < count; ++ci) {
            int vi = face[ci];

            int num_connections = v2f_counts[vi];
            int *connection = &shared_faces[v2f_offsets[vi]];
            auto normal = float3::zero();
            for (int ni = 0; ni < num_connections; ++ni) {
                auto& connected_normal = face_normals[connection[ni]];
                float dp = dot(face_normal, connected_normal);
                if (dp > angle) {
                    normal += connected_normal;
                }
            }
            vertex_normals[offset + ci] = normal;
        }
    }

    // normalize
    Normalize(vertex_normals.data(), vertex_normals.size());

    normals = vertex_normals;
}

template<class Body>
void TopologyRefiner::doRefine(const Body& body)
{
    buildConnection();

    int num_indices = (int)indices.size();
    new_points.reserve(num_indices);
    new_normals.reserve(num_indices);
    new_uv.reserve(num_indices);
    new_indices.reserve(num_indices);

    splits.push_back({});
    old2new.resize(num_indices, -1);

    int num_faces = (int)counts.size();
    for (int fi = 0; fi < num_faces; ++fi) {
        int offset = offsets[fi];
        int count = counts[fi];

        if (split_unit > 0 && (int)new_points.size() - splits.back().offset_points + count > split_unit) {
            // add new split
            splits.push_back({});
            auto& prev = splits[splits.size() - 2];
            auto& cur = splits.back();
            prev.num_points = (int)new_points.size() - prev.offset_points;
            prev.num_indices = (int)new_indices.size() - prev.offset_indices;
            cur.offset_faces = prev.offset_faces + prev.num_faces;
            cur.offset_points = prev.offset_points + prev.num_points;
            cur.offset_indices = prev.offset_indices + prev.num_indices;
            std::fill(old2new.begin(), old2new.end(), -1);
        }

        auto& si = splits.back();
        for (int ci = 0; ci < count; ++ci) {
            int i = offset + ci;
            int vi = indices[i];
            int ni = body(vi, i);
            new_indices.push_back(ni - si.offset_points);
        }
        ++si.num_faces;
        si.num_indices_triangulated += (count - 2) * 3;
    }

    {
        auto& cur = splits.back();
        cur.num_points = (int)new_points.size() - cur.offset_points;
        cur.num_indices = (int)new_indices.size() - cur.offset_indices;
    }


    if (triangulate) {
        int num_indices_triangulated = 0;
        for (auto& split : splits) {
            num_indices_triangulated += split.num_indices_triangulated;
        }

        new_indices_triangulated.resize(num_indices_triangulated);
        int *sub_indices = new_indices_triangulated.data();
        for (auto& split : splits) {
            mu::TriangulateWithIndices(sub_indices,
                IntrusiveArray<int>(&counts[split.offset_faces], split.num_faces),
                IntrusiveArray<int>(&new_indices[split.offset_indices], split.num_indices),
                swap_faces);
            sub_indices += split.num_indices_triangulated;
        }
    }
    else if (swap_faces) {
        // todo
    }
}

bool TopologyRefiner::refine()
{
    int num_points = (int)points.size();
    int num_indices = (int)indices.size();
    int num_normals = (int)normals.size();
    int num_uv = (int)uv.size();

    if (!uv.empty()) {
        if (!normals.empty()) {
            if (!tangents.empty()) {
                if (num_normals == num_indices && num_uv == num_indices) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNTU(vi, points[vi], normals[i], tangents[i], uv[i]);
                    });
                }
                else if (num_normals == num_indices && num_uv == num_points) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNTU(vi, points[vi], normals[i], tangents[i], uv[vi]);
                    });
                }
                else if (num_normals == num_points && num_uv == num_indices) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNTU(vi, points[vi], normals[vi], tangents[i], uv[i]);
                    });
                }
                else if (num_normals == num_points && num_uv == num_points) {
                    doRefine([this](int vi, int) {
                        return findOrAddVertexPNTU(vi, points[vi], normals[vi], tangents[vi], uv[vi]);
                    });
                }
            }
            else {
                if (num_normals == num_indices && num_uv == num_indices) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNU(vi, points[vi], normals[i], uv[i]);
                    });
                }
                else if (num_normals == num_indices && num_uv == num_points) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNU(vi, points[vi], normals[i], uv[vi]);
                    });
                }
                else if (num_normals == num_points && num_uv == num_indices) {
                    doRefine([this](int vi, int i) {
                        return findOrAddVertexPNU(vi, points[vi], normals[vi], uv[i]);
                    });
                }
                else if (num_normals == num_points && num_uv == num_points) {
                    doRefine([this](int vi, int) {
                        return findOrAddVertexPNU(vi, points[vi], normals[vi], uv[vi]);
                    });
                }
            }
        }
        else {
            if (num_uv == num_indices) {
                doRefine([this](int vi, int i) {
                    return findOrAddVertexPU(vi, points[vi], uv[i]);
                });
            }
            else if (num_uv == num_points) {
                doRefine([this](int vi, int) {
                    return findOrAddVertexPU(vi, points[vi], uv[vi]);
                });
            }
        }
    }
    else {
        if (num_normals == num_indices) {
            doRefine([this](int vi, int i) {
                return findOrAddVertexPN(vi, points[vi], normals[i]);
            });
        }
        else if (num_normals == num_points) {
            doRefine([this](int vi, int) {
                return findOrAddVertexPN(vi, points[vi], normals[vi]);
            });
        }
    }
    return true;
}

void TopologyRefiner::genTangents()
{
    tangents.resize(std::max<size_t>(normals.size(), uv.size()));
    mu::GenerateTangents(tangents, points, normals, uv, counts, offsets, indices);
}

void TopologyRefiner::buildConnection()
{
    if (v2f_counts.size() == points.size()) { return; }

    size_t num_faces = counts.size();
    size_t num_indices = indices.size();
    size_t num_points = points.size();

    v2f_counts.resize(num_points);
    v2f_offsets.resize(num_points);
    shared_faces.resize(num_indices);
    shared_indices.resize(num_indices);
    memset(v2f_counts.data(), 0, sizeof(int)*num_points);

    {
        const int *idx = indices.data();
        for (auto& c : counts) {
            for (int i = 0; i < c; ++i) {
                v2f_counts[idx[i]]++;
            }
            idx += c;
        }
    }

    RawVector<int> v2f_indices;
    v2f_indices.resize(num_points);
    memset(v2f_indices.data(), 0, sizeof(int)*num_points);

    {
        int offset = 0;
        for (size_t i = 0; i < num_points; ++i) {
            v2f_offsets[i] = offset;
            offset += v2f_counts[i];
        }
    }
    {
        int i = 0;
        for (int fi = 0; fi < (int)num_faces; ++fi) {
            int c = counts[fi];
            for (int ci = 0; ci < c; ++ci) {
                int vi = indices[i + ci];
                int ti = v2f_offsets[vi] + v2f_indices[vi]++;
                shared_faces[ti] = fi;
                shared_indices[ti] = i + ci;
            }
            i += c;
        }
    }
}

int TopologyRefiner::findOrAddVertexPNTU(int vi, const float3& p, const float3& n, const float4& t, const float2& u)
{
    int offset = v2f_offsets[vi];
    int count = v2f_counts[vi];
    for (int ci = 0; ci < count; ++ci) {
        int& ni = old2new[shared_indices[offset + ci]];
        // tangent can be omitted as it is generated by point, normal and uv
        if (ni != -1 && near_equal(new_points[ni], p) && near_equal(new_normals[ni], n) && near_equal(new_uv[ni], u)) {
            return ni;
        }
        else if (ni == -1) {
            ni = (int)new_points.size();
            new_points.push_back(p);
            new_normals.push_back(n);
            new_tangents.push_back(t);
            new_uv.push_back(u);
            return ni;
        }
    }
    return 0;
}

int TopologyRefiner::findOrAddVertexPNU(int vi, const float3& p, const float3& n, const float2& u)
{
    int offset = v2f_offsets[vi];
    int count = v2f_counts[vi];
    for (int ci = 0; ci < count; ++ci) {
        int& ni = old2new[shared_indices[offset + ci]];
        if (ni != -1 && near_equal(new_points[ni], p) && near_equal(new_normals[ni], n) && near_equal(new_uv[ni], u)) {
            return ni;
        }
        else if (ni == -1) {
            ni = (int)new_points.size();
            new_points.push_back(p);
            new_normals.push_back(n);
            new_uv.push_back(u);
            return ni;
        }
    }
    return 0;
}

int TopologyRefiner::findOrAddVertexPN(int vi, const float3& p, const float3& n)
{
    int offset = v2f_offsets[vi];
    int count = v2f_counts[vi];
    for (int ci = 0; ci < count; ++ci) {
        int& ni = old2new[shared_indices[offset + ci]];
        if (ni != -1 && near_equal(new_points[ni], p) && near_equal(new_normals[ni], n)) {
            return ni;
        }
        else if (ni == -1) {
            ni = (int)new_points.size();
            new_points.push_back(p);
            new_normals.push_back(n);
            return ni;
        }
    }
    return 0;
}

int TopologyRefiner::findOrAddVertexPU(int vi, const float3& p, const float2& u)
{
    int offset = v2f_offsets[vi];
    int count = v2f_counts[vi];
    for (int ci = 0; ci < count; ++ci) {
        int& ni = old2new[shared_indices[offset + ci]];
        if (ni != -1 && near_equal(new_points[ni], p) && near_equal(new_uv[ni], u)) {
            return ni;
        }
        else if (ni == -1) {
            ni = (int)new_points.size();
            new_points.push_back(p);
            new_uv.push_back(u);
            return ni;
        }
    }
    return 0;
}

struct TSpaceContext
{
    IArray<float4> dst;
    const IArray<float3> points;
    const IArray<float3> normals;
    const IArray<float2> uv;
    const IArray<int> counts;
    const IArray<int> offsets;
    const IArray<int> indices;

    static int getNumFaces(const SMikkTSpaceContext *tctx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        return (int)_this->counts.size();
    }

    static int getCount(const SMikkTSpaceContext *tctx, int i)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        return (int)_this->counts[i];
    }

    static void getPosition(const SMikkTSpaceContext *tctx, float *o_pos, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        const int *face = &_this->indices[_this->offsets[iface]];
        (float3&)*o_pos = _this->points[face[ivtx]];
    }

    static void getPositionFlattened(const SMikkTSpaceContext *tctx, float *o_pos, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        (float3&)*o_pos = _this->points[_this->offsets[iface] + ivtx];
    }

    static void getNormal(const SMikkTSpaceContext *tctx, float *o_normal, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        const int *face = &_this->indices[_this->offsets[iface]];
        (float3&)*o_normal = _this->normals[face[ivtx]];
    }

    static void getNormalFlattened(const SMikkTSpaceContext *tctx, float *o_normal, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        (float3&)*o_normal = _this->normals[_this->offsets[iface] + ivtx];
    }

    static void getTexCoord(const SMikkTSpaceContext *tctx, float *o_tcoord, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        const int *face = &_this->indices[_this->offsets[iface]];
        (float2&)*o_tcoord = _this->uv[face[ivtx]];
    }

    static void getTexCoordFlattened(const SMikkTSpaceContext *tctx, float *o_tcoord, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        (float2&)*o_tcoord = _this->uv[_this->offsets[iface] + ivtx];
    }

    static void setTangent(const SMikkTSpaceContext *tctx, const float* tangent, const float* /*bitangent*/,
        float /*fMagS*/, float /*fMagT*/, tbool IsOrientationPreserving, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        const int *face = &_this->indices[_this->offsets[iface]];
        float sign = (IsOrientationPreserving != 0) ? 1.0f : -1.0f;
        _this->dst[face[ivtx]] = { tangent[0], tangent[1], tangent[2], sign };
    }

    static void setTangentFlattened(const SMikkTSpaceContext *tctx, const float* tangent, const float* /*bitangent*/,
        float /*fMagS*/, float /*fMagT*/, tbool IsOrientationPreserving, int iface, int ivtx)
    {
        auto *_this = reinterpret_cast<TSpaceContext*>(tctx->m_pUserData);
        float sign = (IsOrientationPreserving != 0) ? 1.0f : -1.0f;
        _this->dst[_this->offsets[iface] + ivtx] = { tangent[0], tangent[1], tangent[2], sign };
    }
};

bool GenerateTangents(
    IArray<float4> dst, const IArray<float3> points, const IArray<float3> normals, const IArray<float2> uv,
    const IArray<int> counts, const IArray<int> offsets, const IArray<int> indices)
{
    TSpaceContext ctx = {dst, points, normals, uv, counts, offsets, indices};

    SMikkTSpaceInterface iface;
    memset(&iface, 0, sizeof(iface));
    iface.m_getNumFaces = TSpaceContext::getNumFaces;
    iface.m_getNumVerticesOfFace = TSpaceContext::getCount;
    iface.m_getPosition = points.size()  == indices.size() ? TSpaceContext::getPositionFlattened : TSpaceContext::getPosition;
    iface.m_getNormal   = normals.size() == indices.size() ? TSpaceContext::getNormalFlattened : TSpaceContext::getNormal;
    iface.m_getTexCoord = uv.size()      == indices.size() ? TSpaceContext::getTexCoord : TSpaceContext::getTexCoordFlattened;
    iface.m_setTSpace   = dst.size()     == indices.size() ? TSpaceContext::setTangentFlattened : TSpaceContext::setTangent;

    SMikkTSpaceContext tctx;
    memset(&tctx, 0, sizeof(tctx));
    tctx.m_pInterface = &iface;
    tctx.m_pUserData = &ctx;

    return genTangSpaceDefault(&tctx) != 0;
}

template<class VertexT> static inline void InterleaveImpl(VertexT *dst, const typename VertexT::arrays_t& src, size_t i);

template<> inline void InterleaveImpl(vertex_v3n3 *dst, const vertex_v3n3::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
}
template<> inline void InterleaveImpl(vertex_v3n3c4 *dst, const vertex_v3n3c4::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
    dst[i].c = src.colors[i];
}
template<> inline void InterleaveImpl(vertex_v3n3u2 *dst, const vertex_v3n3u2::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
    dst[i].u = src.uvs[i];
}
template<> inline void InterleaveImpl(vertex_v3n3c4u2 *dst, const vertex_v3n3c4u2::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
    dst[i].c = src.colors[i];
    dst[i].u = src.uvs[i];
}
template<> inline void InterleaveImpl(vertex_v3n3u2t4 *dst, const vertex_v3n3u2t4::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
    dst[i].u = src.uvs[i];
    dst[i].t = src.tangents[i];
}
template<> inline void InterleaveImpl(vertex_v3n3c4u2t4 *dst, const vertex_v3n3c4u2t4::arrays_t& src, size_t i)
{
    dst[i].p = src.points[i];
    dst[i].n = src.normals[i];
    dst[i].c = src.colors[i];
    dst[i].u = src.uvs[i];
    dst[i].t = src.tangents[i];
}

template<class VertexT>
void TInterleave(VertexT *dst, const typename VertexT::arrays_t& src, size_t num)
{
    for (size_t i = 0; i < num; ++i) {
        InterleaveImpl(dst, src, i);
    }
}

VertexFormat GuessVertexFormat(
    const float3 *points,
    const float3 *normals,
    const float4 *colors,
    const float2 *uvs,
    const float4 *tangents
)
{
    if (points && normals) {
        if (colors && uvs && tangents) { return VertexFormat::V3N3C4U2T4; }
        if (colors && uvs) { return VertexFormat::V3N3C4U2; }
        if (uvs && tangents) { return VertexFormat::V3N3U2T4; }
        if (uvs) { return VertexFormat::V3N3U2; }
        if (colors) { return VertexFormat::V3N3C4; }
        return VertexFormat::V3N3;
    }
    return VertexFormat::Unknown;
}

size_t GetVertexSize(VertexFormat format)
{
    switch (format) {
    case VertexFormat::V3N3: return sizeof(vertex_v3n3);
    case VertexFormat::V3N3C4: return sizeof(vertex_v3n3c4);
    case VertexFormat::V3N3U2: return sizeof(vertex_v3n3u2);
    case VertexFormat::V3N3C4U2: return sizeof(vertex_v3n3c4u2);
    case VertexFormat::V3N3U2T4: return sizeof(vertex_v3n3u2t4);
    case VertexFormat::V3N3C4U2T4: return sizeof(vertex_v3n3c4u2t4);
    default: return 0;
    }
}

void Interleave(void *dst, VertexFormat format, size_t num,
    const float3 *points,
    const float3 *normals,
    const float4 *colors,
    const float2 *uvs,
    const float4 *tangents
)
{
    switch (format) {
    case VertexFormat::V3N3: TInterleave((vertex_v3n3*)dst, {points, normals}, num); break;
    case VertexFormat::V3N3C4: TInterleave((vertex_v3n3c4*)dst, { points, normals, colors }, num); break;
    case VertexFormat::V3N3U2: TInterleave((vertex_v3n3u2*)dst, { points, normals, uvs }, num); break;
    case VertexFormat::V3N3C4U2: TInterleave((vertex_v3n3c4u2*)dst, { points, normals, colors, uvs }, num); break;
    case VertexFormat::V3N3U2T4: TInterleave((vertex_v3n3u2t4*)dst, { points, normals, uvs, tangents }, num); break;
    case VertexFormat::V3N3C4U2T4: TInterleave((vertex_v3n3c4u2t4*)dst, { points, normals, colors, uvs, tangents }, num); break;
    default: break;
    }
}


#ifdef muEnableISPC
#include "MeshUtilsCore.h"

#ifdef muEnableHalf
void FloatToHalf_ISPC(half *dst, const float *src, size_t num)
{
    ispc::FloatToHalf((uint16_t*)dst, src, (int)num);
}
void HalfToFloat_ISPC(float *dst, const half *src, size_t num)
{
    ispc::HalfToFloat(dst, (const uint16_t*)src, (int)num);
}
#endif // muEnableHalf

void InvertX_ISPC(float3 *dst, size_t num)
{
    ispc::InvertXF3((ispc::float3*)dst, (int)num);
}
void InvertX_ISPC(float4 *dst, size_t num)
{
    ispc::InvertXF4((ispc::float4*)dst, (int)num);
}

void Scale_ISPC(float *dst, float s, size_t num)
{
    ispc::ScaleF((float*)dst, s, (int)num * 1);
}
void Scale_ISPC(float3 *dst, float s, size_t num)
{
    ispc::ScaleF((float*)dst, s, (int)num * 3);
}

void ComputeBounds_ISPC(const float3 *p, size_t num, float3& omin, float3& omax)
{
    if (num == 0) { return; }
    ispc::ComputeBounds((ispc::float3*)p, (int)num, (ispc::float3&)omin, (ispc::float3&)omax);
}

void Normalize_ISPC(float3 *dst, size_t num)
{
    ispc::Normalize((ispc::float3*)dst, (int)num);
}

void GenerateNormals_ISPC(
    float3 *dst, const float3 *p,
    const int *counts, const int *offsets, const int *indices, size_t num_points, size_t num_faces)
{
    memset(dst, 0, sizeof(float3)*num_points);

    for (size_t fi = 0; fi < num_faces; ++fi)
    {
        int count = counts[fi];
        const int *face = &indices[offsets[fi]];
        int i0 = face[0];
        int i1 = face[1];
        int i2 = face[2];
        float3 p0 = p[i0];
        float3 p1 = p[i1];
        float3 p2 = p[i2];
        float3 n = cross(p1 - p0, p2 - p0);
        for (int ci = 0; ci < count; ++ci) {
            dst[face[ci]] += n;
        }
    }

    ispc::Normalize((ispc::float3*)dst, (int)num_points);
}
#endif



#ifdef muEnableISPC
    #define Forward(Name, ...) Name##_ISPC(__VA_ARGS__)
#else
    #define Forward(Name, ...) Name##_Generic(__VA_ARGS__)
#endif

#ifdef muEnableHalf
void FloatToHalf(half *dst, const float *src, size_t num)
{
    Forward(FloatToHalf, dst, src, num);
}
void HalfToFloat(float *dst, const half *src, size_t num)
{
    Forward(HalfToFloat, dst, src, num);
}
#endif // muEnableHalf

void InvertX(float3 *dst, size_t num)
{
    Forward(InvertX, dst, num);
}
void InvertX(float4 *dst, size_t num)
{
    Forward(InvertX, dst, num);
}

void Scale(float *dst, float s, size_t num)
{
    Forward(Scale, dst, s, num);
}
void Scale(float3 *dst, float s, size_t num)
{
    Forward(Scale, dst, s, num);
}

void ComputeBounds(const float3 *p, size_t num, float3& omin, float3& omax)
{
    Forward(ComputeBounds, p, num, omin, omax);
}

void Normalize(float3 *dst, size_t num)
{
    Forward(Normalize, dst, num);
}

} // namespace mu
