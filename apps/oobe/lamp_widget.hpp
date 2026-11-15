#pragma once

#include <FinnSDK/FinnSDK.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QQuaternion>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QVector3D>
#include <QVector4D>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace finn::oobe {

struct GlbVertex {
    float px, py, pz;
    float nx, ny, nz;
};

struct GlbSubMesh {
    enum class Role { Shade, Cord, Bulb, GlowPlane };
    Role role = Role::Shade;
    QVector4D base_color{0.6f, 0.5f, 0.35f, 1.0f};
    QVector3D emissive{0.0f, 0.0f, 0.0f};
    QMatrix4x4 model_matrix;
    std::vector<GlbVertex> vertices;
    std::vector<uint16_t> indices;
};

inline void play_lamp_switch_sound(bool turning_on) {
    static const QString player = []() -> QString {
        QString p = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
        if (p.isEmpty()) p = QStandardPaths::findExecutable(QStringLiteral("paplay"));
        return p;
    }();
    if (player.isEmpty()) return;

    const QString base_dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString dir = base_dir.isEmpty() ? QDir::tempPath() : base_dir;
    const QString wav_path = dir + (turning_on ? QStringLiteral("/finn-lamp-on.wav")
                                               : QStringLiteral("/finn-lamp-off.wav"));

    if (!QFileInfo::exists(wav_path)) {
        constexpr int kSampleRate = 44100;
        constexpr int kSamples = kSampleRate * 60 / 1000;  // 60ms crisp mechanical click
        QByteArray pcm(kSamples * 2, '\0');
        int16_t* dst = reinterpret_cast<int16_t*>(pcm.data());
        const float f_hi = turning_on ? 2450.0f : 1750.0f;
        const float f_mid = turning_on ? 920.0f : 660.0f;
        const float f_thud = turning_on ? 210.0f : 165.0f;

        for (int i = 0; i < kSamples; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            // Pre-latch tick at t=0 + main spring snap at t=11ms
            const float e1 = std::exp(-t * 420.0f);
            const float t2 = std::max(0.0f, t - 0.011f);
            const float e2 = (t >= 0.011f) ? std::exp(-t2 * 135.0f) : 0.0f;
            const float env_thud = (t >= 0.011f) ? std::exp(-t2 * 85.0f) : 0.0f;

            const float sig = 0.25f * e1 * std::sin(2.0f * 3.1415926f * f_hi * t)
                            + 0.45f * e2 * std::sin(2.0f * 3.1415926f * f_hi * t2)
                            + 0.35f * e2 * std::sin(2.0f * 3.1415926f * f_mid * t2)
                            + 0.30f * env_thud * std::sin(2.0f * 3.1415926f * f_thud * t2);
            const int v = std::clamp(static_cast<int>(sig * 19000.0f), -32767, 32767);
            dst[i] = static_cast<int16_t>(v);
        }

        QFile f(wav_path);
        if (f.open(QIODevice::WriteOnly)) {
            const uint32_t data_sz = static_cast<uint32_t>(pcm.size());
            const uint32_t riff_sz = 36u + data_sz;
            const uint16_t audio_fmt = 1;
            const uint16_t channels = 1;
            const uint32_t sr = kSampleRate;
            const uint32_t byte_rate = kSampleRate * 2;
            const uint16_t block_align = 2;
            const uint16_t bps = 16;
            f.write("RIFF", 4);
            f.write(reinterpret_cast<const char*>(&riff_sz), 4);
            f.write("WAVEfmt ", 8);
            const uint32_t fmt_sz = 16;
            f.write(reinterpret_cast<const char*>(&fmt_sz), 4);
            f.write(reinterpret_cast<const char*>(&audio_fmt), 2);
            f.write(reinterpret_cast<const char*>(&channels), 2);
            f.write(reinterpret_cast<const char*>(&sr), 4);
            f.write(reinterpret_cast<const char*>(&byte_rate), 4);
            f.write(reinterpret_cast<const char*>(&block_align), 2);
            f.write(reinterpret_cast<const char*>(&bps), 2);
            f.write("data", 4);
            f.write(reinterpret_cast<const char*>(&data_sz), 4);
            f.write(pcm);
        }
    }
    QProcess::startDetached(player, {wav_path});
}

inline QString resolve_model_path(const QString& filename) {
    const QStringList candidates = {
        QStringLiteral("/usr/share/finn/models/") + filename,
        QStringLiteral("assets/models/") + filename,
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/models/") + filename,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/finn/models/") + filename,
    };
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

inline bool load_glb_meshes(const QString& path, std::vector<GlbSubMesh>& out_meshes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = f.readAll();
    if (raw.size() < 20) return false;

    const auto* data = reinterpret_cast<const uint8_t*>(raw.constData());
    auto read_u32 = [&](int off) -> uint32_t {
        uint32_t v = 0;
        std::memcpy(&v, data + off, 4);
        return v;
    };
    auto read_f32 = [&](const uint8_t* ptr) -> float {
        float v = 0.0f;
        std::memcpy(&v, ptr, 4);
        return v;
    };

    if (read_u32(0) != 0x46546C67u || read_u32(4) != 2u) return false;

    int pos = 12;
    QByteArray json_bytes;
    QByteArray bin_bytes;
    while (pos + 8 <= raw.size()) {
        const uint32_t chunk_len = read_u32(pos);
        const uint32_t chunk_type = read_u32(pos + 4);
        pos += 8;
        if (pos + static_cast<int>(chunk_len) > raw.size()) break;
        if (chunk_type == 0x4E4F534Au) {
            json_bytes = raw.mid(pos, static_cast<int>(chunk_len));
        } else if (chunk_type == 0x004E4942u) {
            bin_bytes = raw.mid(pos, static_cast<int>(chunk_len));
        }
        pos += static_cast<int>(chunk_len);
    }
    if (json_bytes.isEmpty() || bin_bytes.isEmpty()) return false;

    const QJsonObject root = QJsonDocument::fromJson(json_bytes).object();
    const QJsonArray nodes = root[QStringLiteral("nodes")].toArray();
    const QJsonArray meshes = root[QStringLiteral("meshes")].toArray();
    const QJsonArray materials = root[QStringLiteral("materials")].toArray();
    const QJsonArray accessors = root[QStringLiteral("accessors")].toArray();
    const QJsonArray buffer_views = root[QStringLiteral("bufferViews")].toArray();

    std::vector<int> parent_idx(nodes.size(), -1);
    std::vector<QMatrix4x4> local_mats(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject n = nodes[i].toObject();
        if (n.contains(QStringLiteral("matrix"))) {
            const QJsonArray m = n[QStringLiteral("matrix")].toArray();
            if (m.size() == 16) {
                float vals[16];
                for (int k = 0; k < 16; ++k) vals[k] = static_cast<float>(m[k].toDouble());
                local_mats[i] = QMatrix4x4(vals).transposed();
            }
        } else {
            QMatrix4x4 m;
            if (n.contains(QStringLiteral("translation"))) {
                const QJsonArray t = n[QStringLiteral("translation")].toArray();
                if (t.size() == 3) {
                    m.translate(static_cast<float>(t[0].toDouble()),
                                static_cast<float>(t[1].toDouble()),
                                static_cast<float>(t[2].toDouble()));
                }
            }
            if (n.contains(QStringLiteral("rotation"))) {
                const QJsonArray r = n[QStringLiteral("rotation")].toArray();
                if (r.size() == 4) {
                    m.rotate(QQuaternion(static_cast<float>(r[3].toDouble()),
                                         static_cast<float>(r[0].toDouble()),
                                         static_cast<float>(r[1].toDouble()),
                                         static_cast<float>(r[2].toDouble())));
                }
            }
            if (n.contains(QStringLiteral("scale"))) {
                const QJsonArray s = n[QStringLiteral("scale")].toArray();
                if (s.size() == 3) {
                    m.scale(static_cast<float>(s[0].toDouble()),
                            static_cast<float>(s[1].toDouble()),
                            static_cast<float>(s[2].toDouble()));
                }
            }
            local_mats[i] = m;
        }
        const QJsonArray ch = n[QStringLiteral("children")].toArray();
        for (const QJsonValue& cv : ch) {
            const int ci = cv.toInt(-1);
            if (ci >= 0 && ci < nodes.size()) parent_idx[ci] = i;
        }
    }

    auto world_mat = [&](int idx) {
        QMatrix4x4 res;
        std::vector<int> chain;
        for (int cur = idx; cur >= 0; cur = parent_idx[cur]) chain.push_back(cur);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            res = res * local_mats[*it];
        }
        return res;
    };

    auto accessor_bytes = [&](int acc_idx, int& count, int& comp_type, int& stride) -> const uint8_t* {
        if (acc_idx < 0 || acc_idx >= accessors.size()) return nullptr;
        const QJsonObject acc = accessors[acc_idx].toObject();
        const int bv_idx = acc[QStringLiteral("bufferView")].toInt(-1);
        if (bv_idx < 0 || bv_idx >= buffer_views.size()) return nullptr;
        const QJsonObject bv = buffer_views[bv_idx].toObject();
        const int bv_off = bv[QStringLiteral("byteOffset")].toInt(0);
        const int acc_off = acc[QStringLiteral("byteOffset")].toInt(0);
        count = acc[QStringLiteral("count")].toInt(0);
        comp_type = acc[QStringLiteral("componentType")].toInt(0);
        stride = bv[QStringLiteral("byteStride")].toInt(0);
        const int total_off = bv_off + acc_off;
        if (total_off < 0 || total_off >= bin_bytes.size()) return nullptr;
        return reinterpret_cast<const uint8_t*>(bin_bytes.constData()) + total_off;
    };

    out_meshes.clear();
    for (int ni = 0; ni < nodes.size(); ++ni) {
        const QJsonObject n = nodes[ni].toObject();
        if (!n.contains(QStringLiteral("mesh"))) continue;
        const int mi = n[QStringLiteral("mesh")].toInt(-1);
        if (mi < 0 || mi >= meshes.size()) continue;

        const QJsonObject mesh_obj = meshes[mi].toObject();
        const QString mesh_name = mesh_obj[QStringLiteral("name")].toString();
        const QMatrix4x4 wmat = world_mat(ni);

        const QJsonArray prims = mesh_obj[QStringLiteral("primitives")].toArray();
        for (const QJsonValue& pv : prims) {
            const QJsonObject prim = pv.toObject();
            const QJsonObject attrs = prim[QStringLiteral("attributes")].toObject();
            const int pos_acc = attrs[QStringLiteral("POSITION")].toInt(-1);
            const int norm_acc = attrs[QStringLiteral("NORMAL")].toInt(-1);
            const int idx_acc = prim[QStringLiteral("indices")].toInt(-1);
            if (pos_acc < 0 || idx_acc < 0) continue;

            int v_count = 0, p_type = 0, p_stride = 0;
            const uint8_t* p_ptr = accessor_bytes(pos_acc, v_count, p_type, p_stride);
            if (!p_ptr || v_count <= 0 || p_type != 5126) continue;
            if (p_stride == 0) p_stride = 12;

            int n_count = 0, n_type = 0, n_stride = 0;
            const uint8_t* n_ptr = accessor_bytes(norm_acc, n_count, n_type, n_stride);
            if (n_stride == 0) n_stride = 12;

            int i_count = 0, i_type = 0, i_stride = 0;
            const uint8_t* i_ptr = accessor_bytes(idx_acc, i_count, i_type, i_stride);
            if (!i_ptr || i_count <= 0) continue;

            GlbSubMesh sub;
            sub.model_matrix = wmat;
            if (mesh_name.contains(QLatin1String("Cylinder.001")) ||
                mesh_name.contains(QLatin1String("Cylinder.003"))) {
                sub.role = GlbSubMesh::Role::Cord;
            } else if (mesh_name.contains(QLatin1String("Sphere"))) {
                sub.role = GlbSubMesh::Role::Bulb;
            } else if (mesh_name.contains(QLatin1String("Plane"))) {
                sub.role = GlbSubMesh::Role::GlowPlane;
            } else {
                sub.role = GlbSubMesh::Role::Shade;
            }

            const int mat_idx = prim[QStringLiteral("material")].toInt(-1);
            if (mat_idx >= 0 && mat_idx < materials.size()) {
                const QJsonObject mat = materials[mat_idx].toObject();
                const QJsonObject pbr = mat[QStringLiteral("pbrMetallicRoughness")].toObject();
                if (pbr.contains(QStringLiteral("baseColorFactor"))) {
                    const QJsonArray bc = pbr[QStringLiteral("baseColorFactor")].toArray();
                    if (bc.size() == 4) {
                        sub.base_color = QVector4D(
                            static_cast<float>(bc[0].toDouble()),
                            static_cast<float>(bc[1].toDouble()),
                            static_cast<float>(bc[2].toDouble()),
                            static_cast<float>(bc[3].toDouble()));
                    }
                }
                if (mat.contains(QStringLiteral("emissiveFactor"))) {
                    const QJsonArray ef = mat[QStringLiteral("emissiveFactor")].toArray();
                    if (ef.size() == 3) {
                        sub.emissive = QVector3D(
                            static_cast<float>(ef[0].toDouble()),
                            static_cast<float>(ef[1].toDouble()),
                            static_cast<float>(ef[2].toDouble()));
                    }
                }
            }

            sub.vertices.resize(static_cast<size_t>(v_count));
            for (int v = 0; v < v_count; ++v) {
                const uint8_t* vp = p_ptr + v * p_stride;
                GlbVertex gv{};
                gv.px = read_f32(vp);
                gv.py = read_f32(vp + 4);
                gv.pz = read_f32(vp + 8);
                if (n_ptr && n_type == 5126 && v < n_count) {
                    const uint8_t* np = n_ptr + v * n_stride;
                    gv.nx = read_f32(np);
                    gv.ny = read_f32(np + 4);
                    gv.nz = read_f32(np + 8);
                } else {
                    gv.ny = 1.0f;
                }
                sub.vertices[static_cast<size_t>(v)] = gv;
            }

            sub.indices.resize(static_cast<size_t>(i_count));
            for (int k = 0; k < i_count; ++k) {
                if (i_type == 5123) {
                    uint16_t val = 0;
                    const int step = i_stride > 0 ? i_stride : 2;
                    std::memcpy(&val, i_ptr + k * step, 2);
                    sub.indices[static_cast<size_t>(k)] = val;
                } else if (i_type == 5125) {
                    uint32_t val = 0;
                    const int step = i_stride > 0 ? i_stride : 4;
                    std::memcpy(&val, i_ptr + k * step, 4);
                    sub.indices[static_cast<size_t>(k)] = static_cast<uint16_t>(val);
                } else {
                    const int step = i_stride > 0 ? i_stride : 1;
                    sub.indices[static_cast<size_t>(k)] = *(i_ptr + k * step);
                }
            }
            if (sub.role == GlbSubMesh::Role::Cord) {
                // Subdivide Cord into 16 vertical rings so it flexes smoothly and flares into a brushed-brass neck collar
                sub.vertices.clear();
                sub.indices.clear();
                constexpr int kRings = 16;
                constexpr int kSides = 16;
                for (int r = 0; r <= kRings; ++r) {
                    const float t = static_cast<float>(r) / kRings;
                    const float y = 6.05f - t * 2.10f;
                    // Sleek 0.034 cord flaring cleanly into a stepped 0.115 brass collar at the shade neck
                    float rad = 0.034f;
                    if (t >= 0.90f) {
                        const float cu = (t - 0.90f) / 0.10f;
                        rad = 0.034f + 0.082f * (cu * cu * (3.0f - 2.0f * cu));
                    }
                    for (int s = 0; s < kSides; ++s) {
                        const float ang = static_cast<float>(s) * (2.0f * 3.1415926f / kSides);
                        const float nx = std::cos(ang);
                        const float nz = std::sin(ang);
                        sub.vertices.push_back({nx * rad, y, nz * rad, nx, 0.0f, nz});
                    }
                }
                for (int r = 0; r < kRings; ++r) {
                    for (int s = 0; s < kSides; ++s) {
                        const int s_next = (s + 1) % kSides;
                        const uint16_t a = static_cast<uint16_t>(r * kSides + s);
                        const uint16_t b = static_cast<uint16_t>(r * kSides + s_next);
                        const uint16_t c = static_cast<uint16_t>((r + 1) * kSides + s);
                        const uint16_t d = static_cast<uint16_t>((r + 1) * kSides + s_next);
                        sub.indices.push_back(a);
                        sub.indices.push_back(c);
                        sub.indices.push_back(b);
                        sub.indices.push_back(b);
                        sub.indices.push_back(c);
                        sub.indices.push_back(d);
                    }
                }
            } else if (sub.role == GlbSubMesh::Role::Shade) {
                // Designer ivory-alabaster linen shade inspired by luxury ceramic/linen lamps (#F8F4EC)
                sub.base_color.setX(0.97f);
                sub.base_color.setY(0.95f);
                sub.base_color.setZ(0.91f);
            }
            out_meshes.push_back(std::move(sub));
        }
    }
    return !out_meshes.empty();
}

// Interactive 3D pendant lamp widget using finn_lamp_lit.glb / finn_lamp_unlit.glb.
// Renders with a lightweight software Z-buffer rasterizer (zero GPU/driver dependency)
// with real damped pendulum + spring pull physics and directional spotlight lighting.
class OobeLamp3DWidget final : public QWidget {
public:
    explicit OobeLamp3DWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(320, 440);
        setCursor(Qt::OpenHandCursor);

        const bool dark = finn::ui::FinnThemeManager::instance().isDark();
        light_anim_ = dark ? 0.0f : 1.0f;
        target_light_ = light_anim_;

        const QString lit_path = resolve_model_path(QStringLiteral("finn_lamp_lit.glb"));
        if (!lit_path.isEmpty()) {
            load_glb_meshes(lit_path, meshes_);
        }
        if (meshes_.empty()) {
            const QString unlit_path = resolve_model_path(QStringLiteral("finn_lamp_unlit.glb"));
            if (!unlit_path.isEmpty()) load_glb_meshes(unlit_path, meshes_);
        }

        frame_img_ = QImage(360, 620, QImage::Format_ARGB32_Premultiplied);
        zbuf_.resize(360 * 620, 1e9f);

        timer_.setInterval(16);
        connect(&timer_, &QTimer::timeout, this, &OobeLamp3DWidget::stepPhysics);

        connect(&finn::ui::FinnThemeManager::instance(),
                &finn::ui::FinnThemeManager::themeChanged,
                this, [this]() {
                    const bool is_dark = finn::ui::FinnThemeManager::instance().isDark();
                    const float next_target = is_dark ? 0.0f : 1.0f;
                    if (std::abs(next_target - target_light_) > 0.1f) {
                        target_light_ = next_target;
                        light_flash_ = 1.0f;
                        light_phase_ = 0.0f;
                    }
                    ensureAnimating();
                });
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int w = std::max(1, width());
        const int h = std::max(1, height());
        if (frame_img_.width() != w || frame_img_.height() != h) {
            frame_img_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
            zbuf_.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 1e9f);
        }
    }

    void paintEvent(QPaintEvent*) override {
        const int W = frame_img_.width();
        const int H = frame_img_.height();
        if (W <= 0 || H <= 0) return;
        frame_img_.fill(Qt::transparent);
        std::fill(zbuf_.begin(), zbuf_.end(), 1e9f);

        // Effective illumination with filament ignition bloom on turn-on and warm afterglow on turn-off
        const float bloom = (target_light_ > 0.5f)
            ? light_flash_ * (0.28f - 0.12f * std::sin(light_phase_))
            : light_flash_ * 0.10f;
        const float t_lit = std::clamp(light_anim_ + bloom, 0.0f, 1.30f);
        const float t_surf = std::min(1.18f, t_lit);
        const float aspect = static_cast<float>(W) / std::max(1, H);
        QMatrix4x4 proj;
        proj.perspective(38.0f, aspect, 0.1f, 30.0f);

        // Camera positioned farther back so the lamp is compact and centered
        const QVector3D cam_pos(0.0f, 3.85f, 7.60f);
        QMatrix4x4 view;
        view.lookAt(cam_pos, QVector3D(0.0f, 3.80f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f));
        const QMatrix4x4 vp = proj * view;

        // Realistic hanging pendant lamp:
        //   1) Primary pendulum swing from ceiling (0, 6.60, 0) + spring pull downward
        //   2) Subtle, damped swivel lag at the shade neck (0, 3.88, 0)
        const float deg = 57.2958f;
        QMatrix4x4 swing;
        swing.translate(0.0f, 6.60f, 0.0f);
        swing.rotate(angle_z_ * deg, 0.0f, 0.0f, 1.0f);
        swing.rotate(angle_x_ * deg, 1.0f, 0.0f, 0.0f);
        swing.translate(0.0f, -6.60f - pull_y_, 0.0f);
        swing.translate(0.0f, 3.88f, 0.0f);
        swing.rotate(head_angle_z_ * deg, 0.0f, 0.0f, 1.0f);
        swing.rotate(head_angle_x_ * deg, 1.0f, 0.0f, 0.0f);
        swing.translate(0.0f, -3.88f, 0.0f);

        // Cubic Bezier cord from vertical ceiling mount to lamp neck
        const QVector3D cord_p0(0.0f, 6.60f, 0.0f);
        const QVector3D cord_p3 = swing.map(QVector3D(0.0f, 3.88f, 0.0f));
        const QVector3D lamp_up = swing.mapVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        const float cord_len = std::max(0.5f, (cord_p3 - cord_p0).length());
        const QVector3D cord_p1(cord_p3.x() * 0.14f + wire_bow_z_ * 0.18f,
                                6.60f - cord_len * 0.34f,
                                cord_p3.z() * 0.14f);
        const QVector3D cord_p2 = cord_p3 + lamp_up * (cord_len * 0.34f)
                                + QVector3D(wire_bow_z_ * 0.12f, 0.0f, 0.0f);

        auto eval_cord = [&](float t) -> QVector3D {
            const float u = 1.0f - t;
            return (u * u * u) * cord_p0
                 + (3.0f * u * u * t) * cord_p1
                 + (3.0f * u * t * t) * cord_p2
                 + (t * t * t) * cord_p3;
        };
        auto eval_cord_tan = [&](float t) -> QVector3D {
            const float u = 1.0f - t;
            QVector3D d = (3.0f * u * u) * (cord_p1 - cord_p0)
                        + (6.0f * u * t) * (cord_p2 - cord_p1)
                        + (3.0f * t * t) * (cord_p3 - cord_p2);
            return d.lengthSquared() > 1e-6f ? d.normalized() : QVector3D(0.0f, -1.0f, 0.0f);
        };

        const QVector3D bulb_world = swing.map(QVector3D(0.0f, 3.05f, 0.0f));
        const QVector3D lamp_down = (-lamp_up).normalized();
        const QVector3D key_dir = QVector3D(0.45f, 0.85f, 0.55f).normalized();

        struct XformVert {
            QVector3D wp;
            QVector3D wn;
            float sx, sy, sz, inv_w;
        };

        auto shade_pixel = [&](const GlbSubMesh& m, const QVector3D& wp, const QVector3D& wn,
                               float& r, float& g, float& b, float& a) {
            const QVector3D N = (m.role == GlbSubMesh::Role::Bulb && (wp - bulb_world).lengthSquared() > 1e-6f)
                ? (wp - bulb_world).normalized()
                : wn.normalized();
            const QVector3D V = (cam_pos - wp).normalized();
            const float ndv = std::max(0.0f, QVector3D::dotProduct(N, V));
            const float fresnel = std::pow(1.0f - ndv, 3.0f);
            const QVector3D H_key = (key_dir + V).normalized();
            const float ndh_key = std::max(0.0f, QVector3D::dotProduct(N, H_key));

            auto soft_tonemap = [](float v) {
                return v / (1.0f + 0.22f * std::max(0.0f, v - 0.85f));
            };

            if (m.role == GlbSubMesh::Role::Bulb) {
                // High-end frosted opal glass bulb: soft luminous alabaster-white core with warm cream rim
                const float bl = std::min(1.0f, t_surf);
                const float core = std::pow(ndv, 2.0f);
                const float edge_fresnel = (1.0f - ndv) * (1.0f - ndv);
                const float glass_spec = std::pow(ndh_key, 24.0f) * 0.25f;

                // Unlit: smoked frosted alabaster-opal glass
                const float unlit_r = 0.72f + 0.14f * core + 0.12f * edge_fresnel + glass_spec * 0.55f;
                const float unlit_g = 0.74f + 0.14f * core + 0.12f * edge_fresnel + glass_spec * 0.60f;
                const float unlit_b = 0.79f + 0.12f * core + 0.12f * edge_fresnel + glass_spec * 0.70f;
                const float unlit_a = 0.68f + 0.22f * edge_fresnel;

                // Lit: pure warm ivory core (#FFFDF8) fading gently into soft champagne-cream (#FFEACA)
                const float flash_warm = light_flash_ * (1.0f - target_light_);
                const float lit_r = soft_tonemap(1.01f + glass_spec * 0.08f);
                const float lit_g = soft_tonemap(0.91f + 0.08f * core + 0.04f * edge_fresnel - 0.08f * flash_warm + glass_spec * 0.10f);
                const float lit_b = soft_tonemap(0.76f + 0.18f * core + 0.08f * edge_fresnel - 0.16f * flash_warm + glass_spec * 0.14f);
                const float lit_a = 0.94f + 0.05f * core;

                r = unlit_r * (1.0f - bl) + lit_r * bl;
                g = unlit_g * (1.0f - bl) + lit_g * bl;
                b = unlit_b * (1.0f - bl) + lit_b * bl;
                a = unlit_a * (1.0f - bl) + lit_a * bl;
                return;
            }

            if (m.role == GlbSubMesh::Role::Cord) {
                // Transition smoothly into a warm brushed antique-brass collar at the shade neck
                const float neck_dist = (wp - cord_p3).length();
                const float brass_blend = 1.0f - std::clamp((neck_dist - 0.14f) / 0.12f, 0.0f, 1.0f);
                const float diff_key = std::max(0.0f, QVector3D::dotProduct(N, key_dir));
                const float cord_spec = std::pow(ndh_key, 20.0f) * 0.22f;
                const float brass_spec = std::pow(ndh_key, 32.0f) * 0.42f;

                // Matte dark-bronze fabric cord above
                const float c_base = 0.11f + 0.10f * diff_key + 0.05f * fresnel + cord_spec * 0.5f;
                const float cr = c_base * 1.04f;
                const float cg = c_base * 0.98f;
                const float cb = c_base * 0.90f;

                // Warm brushed antique brass (#C8A05A) collar at the shade neck
                const float b_lit = 0.42f + 0.48f * diff_key + 0.22f * fresnel;
                const float br = soft_tonemap(0.78f * b_lit + brass_spec * 0.95f);
                const float bg = soft_tonemap(0.61f * b_lit + brass_spec * 0.80f);
                const float bb = soft_tonemap(0.34f * b_lit + brass_spec * 0.45f);

                r = cr * (1.0f - brass_blend) + br * brass_blend;
                g = cg * (1.0f - brass_blend) + bg * brass_blend;
                b = cb * (1.0f - brass_blend) + bb * brass_blend;
                a = 1.0f;
                return;
            }

            // Shade: velvety ivory linen-ceramic with soft internal warm-cream subsurface glow when lit
            // (inspired by luxury cream-linen shades — no harsh plastic specular dot)
            const float diff_key = std::max(0.0f, QVector3D::dotProduct(N, key_dir));
            const float wrap_key = std::max(0.0f, (QVector3D::dotProduct(N, key_dir) + 0.50f) / 1.50f);
            const QVector3D fill_dir = QVector3D(-0.65f, 0.35f, 0.45f).normalized();
            const float diff_fill = std::max(0.0f, QVector3D::dotProduct(N, fill_dir));

            const QVector3D to_bulb = bulb_world - wp;
            const float dist = std::max(0.05f, to_bulb.length());
            const QVector3D L = to_bulb / dist;
            // Directional downward spotlight on the lower rim + soft linen subsurface translucency inside the dome
            const float spot_cos = QVector3D::dotProduct(-L, lamp_down);
            const float cone_u = std::clamp((spot_cos - 0.02f) / 0.68f, 0.0f, 1.0f);
            const float spot_cone = cone_u * cone_u * (3.0f - 2.0f * cone_u);
            const float n_dot_l = QVector3D::dotProduct(N, L);
            const float diff_bulb = std::max(0.0f, n_dot_l) * spot_cone;
            const float atten = 1.0f / (1.0f + 0.32f * dist * dist);

            // Soft translucent linen glow through the middle of the shade + subtle vertical woven-linen slub
            const float linen_sss = std::pow(ndv, 1.20f) * (1.0f / (1.0f + 0.38f * dist * dist)) * t_lit;
            const float weave_ang = std::atan2(wp.x() - cord_p3.x(), wp.z() - cord_p3.z());
            const float weave = 0.016f * std::sin(weave_ang * 140.0f) + 0.008f * std::sin(weave_ang * 310.0f + wp.y() * 14.0f);

            // Velvety matte-linen sheen (very broad & subtle, zero harsh plastic hotspot)
            const float spec = std::pow(ndh_key, 7.0f) * 0.05f;

            // Studio neutral-warm ambient
            const float up_fac = N.y() * 0.5f + 0.5f;
            const float amb_r = (0.48f + 0.14f * t_lit) + 0.05f * up_fac;
            const float amb_g = (0.47f + 0.11f * t_lit) + 0.05f * up_fac;
            const float amb_b = (0.46f + 0.06f * t_lit) + 0.06f * up_fac;

            const float key_term = diff_key * 0.24f + wrap_key * 0.22f;
            const float key_r = 0.26f * key_term + 0.12f * diff_fill;
            const float key_g = 0.25f * key_term + 0.12f * diff_fill;
            const float key_b = 0.24f * key_term + 0.13f * diff_fill;

            const float bulb_f = diff_bulb * atten * t_lit;
            const float rim = fresnel * 0.10f;

            r = soft_tonemap(m.base_color.x() * (amb_r + key_r + 0.65f * bulb_f + weave) + 0.34f * linen_sss + spec + rim);
            g = soft_tonemap(m.base_color.y() * (amb_g + key_g + 0.54f * bulb_f + weave) + 0.22f * linen_sss + spec * 0.96f + rim);
            b = soft_tonemap(m.base_color.z() * (amb_b + key_b + 0.36f * bulb_f + weave) + 0.09f * linen_sss + spec * 0.90f + rim * 1.05f);
            a = m.base_color.w();
        };

        auto rasterize_mesh = [&](const GlbSubMesh& m, bool depth_write) {
            if (m.role == GlbSubMesh::Role::GlowPlane) return;
            const QMatrix4x4 model = swing * m.model_matrix;

            std::vector<XformVert> xv(m.vertices.size());
            for (size_t i = 0; i < m.vertices.size(); ++i) {
                const auto& gv = m.vertices[i];
                QVector3D wp;
                QVector3D wn;
                if (m.role == GlbSubMesh::Role::Cord) {
                    const float t = std::clamp((6.05f - gv.py) / 2.10f, 0.0f, 1.0f);
                    const QVector3D c = eval_cord(t);
                    const QVector3D tan = eval_cord_tan(t);
                    const QVector3D fwd(0.0f, 0.0f, 1.0f);
                    const QVector3D right = QVector3D::crossProduct(fwd, tan).normalized();
                    const QVector3D binorm = QVector3D::crossProduct(tan, right).normalized();
                    wp = c + right * gv.px + binorm * gv.pz;
                    wn = (right * gv.nx + binorm * gv.nz).normalized();
                } else {
                    wp = model.map(QVector3D(gv.px, gv.py, gv.pz));
                    wn = model.mapVector(QVector3D(gv.nx, gv.ny, gv.nz)).normalized();
                }
                const QVector4D clip = vp * QVector4D(wp, 1.0f);
                const float inv_w = (std::abs(clip.w()) > 1e-5f) ? (1.0f / clip.w()) : 1.0f;
                const float ndc_x = clip.x() * inv_w;
                const float ndc_y = clip.y() * inv_w;
                const float ndc_z = clip.z() * inv_w;
                xv[i] = {wp, wn,
                         (ndc_x * 0.5f + 0.75f) * W,
                         (0.5f - ndc_y * 0.5f) * H,
                         ndc_z, inv_w};
            }

            auto edge = [](float ax, float ay, float bx, float by, float cx, float cy) {
                return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
            };

            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                const auto& v0 = xv[m.indices[i]];
                const auto& v1 = xv[m.indices[i + 1]];
                const auto& v2 = xv[m.indices[i + 2]];
                const float area = edge(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
                if (std::abs(area) < 1e-4f) continue;

                const int min_x = std::max(0, static_cast<int>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
                const int max_x = std::min(W - 1, static_cast<int>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
                const int min_y = std::max(0, static_cast<int>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
                const int max_y = std::min(H - 1, static_cast<int>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));
                const float inv_area = 1.0f / area;

                for (int y = min_y; y <= max_y; ++y) {
                    QRgb* row = reinterpret_cast<QRgb*>(frame_img_.scanLine(y));
                    for (int x = min_x; x <= max_x; ++x) {
                        const float px = x + 0.5f;
                        const float py = y + 0.5f;
                        const float w0 = edge(v1.sx, v1.sy, v2.sx, v2.sy, px, py) * inv_area;
                        const float w1 = edge(v2.sx, v2.sy, v0.sx, v0.sy, px, py) * inv_area;
                        const float w2 = edge(v0.sx, v0.sy, v1.sx, v1.sy, px, py) * inv_area;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                        const float z = w0 * v0.sz + w1 * v1.sz + w2 * v2.sz;
                        const int idx = y * W + x;
                        if (z >= zbuf_[idx]) continue;
                        if (depth_write) zbuf_[idx] = z;

                        const QVector3D wp = v0.wp * w0 + v1.wp * w1 + v2.wp * w2;
                        const QVector3D wn = v0.wn * w0 + v1.wn * w1 + v2.wn * w2;
                        float r = 0, g = 0, b = 0, a = 1;
                        shade_pixel(m, wp, wn, r, g, b, a);

                        const int ir = std::clamp(static_cast<int>(r * 255.0f), 0, 255);
                        const int ig = std::clamp(static_cast<int>(g * 255.0f), 0, 255);
                        const int ib = std::clamp(static_cast<int>(b * 255.0f), 0, 255);
                        const int ia = std::clamp(static_cast<int>(a * 255.0f), 0, 255);
                        if (ia >= 250) {
                            row[x] = qRgba(ir, ig, ib, 255);
                        } else if (ia > 0) {
                            const QRgb dst = row[x];
                            const float sa = ia / 255.0f;
                            const float da = 1.0f - sa;
                            const int out_r = std::clamp(static_cast<int>(ir * sa + qRed(dst) * da), 0, 255);
                            const int out_g = std::clamp(static_cast<int>(ig * sa + qGreen(dst) * da), 0, 255);
                            const int out_b = std::clamp(static_cast<int>(ib * sa + qBlue(dst) * da), 0, 255);
                            const int out_a = std::clamp(static_cast<int>(ia + qAlpha(dst) * da), 0, 255);
                            row[x] = qRgba(out_r, out_g, out_b, out_a);
                        }
                    }
                }
            }
        };

        for (const auto& m : meshes_) {
            if (m.role == GlbSubMesh::Role::Shade || m.role == GlbSubMesh::Role::Cord) {
                rasterize_mesh(m, true);
            }
        }
        for (const auto& m : meshes_) {
            if (m.role == GlbSubMesh::Role::Bulb) {
                rasterize_mesh(m, false);
            }
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QVector4D bclip = vp * QVector4D(bulb_world, 1.0f);
        const float binv_w = (std::abs(bclip.w()) > 1e-5f) ? (1.0f / bclip.w()) : 1.0f;
        const qreal bx = (bclip.x() * binv_w * 0.5 + 0.75) * W;
        const qreal by = (0.5 - bclip.y() * binv_w * 0.5) * H;

        // Project the lamp's downward axis to screen space so the directional cone tracks swing
        const QVector3D beam_target = bulb_world + lamp_down * 2.5f;
        const QVector4D beam_clip = vp * QVector4D(beam_target, 1.0f);
        const float beam_inv_w = (std::abs(beam_clip.w()) > 1e-5f) ? (1.0f / beam_clip.w()) : 1.0f;
        const qreal btx = (beam_clip.x() * beam_inv_w * 0.5 + 0.75) * W;
        const qreal bty = (0.5 - beam_clip.y() * beam_inv_w * 0.5) * H;
        const qreal dir_dx = btx - bx;
        const qreal dir_dy = std::max<qreal>(12.0, bty - by);
        const qreal beam_angle_deg = std::atan2(dir_dy, dir_dx) * (180.0 / 3.14159265358979323846) - 90.0;
        const qreal floor_y = H * 0.975;
        const qreal floor_bx = bx + (dir_dx / dir_dy) * (floor_y - by);

        // Subtle back-wall depth & blurred ground plane gradient at the very bottom
        {
            QLinearGradient wall_grad(0.0, 0.0, 0.0, H);
            const int top_shade = t_lit > 0.5f ? 0 : 10;
            const int horizon_a = qRound(10.0 + 12.0 * (1.0f - t_lit));
            const int floor_a = qRound(28.0 + 26.0 * (1.0f - t_lit));
            wall_grad.setColorAt(0.0, QColor(0, 0, 0, top_shade));
            wall_grad.setColorAt(0.76, QColor(0, 0, 0, 0));
            wall_grad.setColorAt(0.90, QColor(0, 0, 0, horizon_a));
            wall_grad.setColorAt(1.0, QColor(0, 0, 0, floor_a));
            p.fillRect(rect(), wall_grad);
        }

        // Warm plaster wall wash (ambient wall bounce behind the shade + directional downward cone)
        if (t_lit > 0.01f) {
            // 1) Broad, cozy warm-peach architectural wall glow behind the shade (mimics warm plaster bounce)
            {
                p.save();
                p.translate(bx, by - 55.0);
                p.scale(1.25, 1.15);
                QRadialGradient wall_bounce(QPointF(0.0, 0.0), 380.0);
                wall_bounce.setColorAt(0.0, QColor(255, 224, 182, qRound(68.0 * t_lit)));
                wall_bounce.setColorAt(0.32, QColor(250, 214, 170, qRound(44.0 * t_lit)));
                wall_bounce.setColorAt(0.65, QColor(242, 204, 162, qRound(18.0 * t_lit)));
                wall_bounce.setColorAt(1.0, QColor(235, 198, 158, 0));
                p.setPen(Qt::NoPen);
                p.setBrush(wall_bounce);
                p.drawEllipse(QPointF(0.0, 0.0), 380.0, 380.0);
                p.restore();
            }

            p.save();
            p.translate(bx, by + 6.0);
            p.rotate(beam_angle_deg);

            // 2) Smooth continuous directional cone wash expanding downward from the shade opening
            struct BeamLobe {
                qreal cy;
                qreal rx;
                qreal ry;
                qreal alpha;
            };
            const BeamLobe lobes[] = {
                {110.0, 175.0, 155.0, 52.0},
                {255.0, 330.0, 290.0, 32.0},
                {410.0, 465.0, 365.0, 16.0},
            };
            p.setPen(Qt::NoPen);
            for (const auto& lobe : lobes) {
                p.save();
                p.translate(0.0, lobe.cy);
                p.scale(1.0, lobe.ry / lobe.rx);
                QRadialGradient g(QPointF(0.0, 0.0), lobe.rx);
                g.setColorAt(0.0, QColor(255, 222, 168, qRound(lobe.alpha * t_lit)));
                g.setColorAt(0.35, QColor(252, 214, 156, qRound(lobe.alpha * 0.60 * t_lit)));
                g.setColorAt(0.70, QColor(246, 204, 142, qRound(lobe.alpha * 0.20 * t_lit)));
                g.setColorAt(1.0, QColor(240, 196, 134, 0));
                p.setBrush(g);
                p.drawEllipse(QPointF(0.0, 0.0), lobe.rx, lobe.rx);
                p.restore();
            }
            p.restore();

            // 3) Soft warm light pool reflected on the blurred ground where the directional beam hits
            p.save();
            p.translate(floor_bx, floor_y);
            p.scale(2.10, 0.28);
            QRadialGradient floor_glow(QPointF(0.0, 0.0), 175.0);
            floor_glow.setColorAt(0.0, QColor(255, 222, 172, qRound(56.0 * t_lit)));
            floor_glow.setColorAt(0.55, QColor(250, 210, 148, qRound(22.0 * t_lit)));
            floor_glow.setColorAt(1.0, QColor(242, 198, 134, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(floor_glow);
            p.drawEllipse(QPointF(0.0, 0.0), 175.0, 175.0);
            p.restore();
        }

        // Ultra-soft warm taupe-umber architectural shadows behind the cord, shade, and ground
        {
            const QVector4D tclip = vp * QVector4D(cord_p0, 1.0f);
            const float tinv_w = (std::abs(tclip.w()) > 1e-5f) ? (1.0f / tclip.w()) : 1.0f;
            const qreal cx_top = (tclip.x() * tinv_w * 0.5 + 0.75) * W - 24.0;

            const QVector3D shade_world = swing.map(QVector3D(0.0f, 3.45f, 0.0f));
            const QVector4D sclip = vp * QVector4D(shade_world, 1.0f);
            const float sinv_w = (std::abs(sclip.w()) > 1e-5f) ? (1.0f / sclip.w()) : 1.0f;
            const qreal sh_x = (sclip.x() * sinv_w * 0.5 + 0.75) * W - 32.0;
            const qreal sh_y = (0.5 - sclip.y() * sinv_w * 0.5) * H + 46.0;

            const double peak_a = 48.0 - 14.0 * t_lit;

            // Extra-wide diffused warm-taupe shadow behind the cord
            for (int pass = 0; pass < 8; ++pass) {
                const qreal w_px = 44.0 + pass * 26.0;
                const int a_px = qRound((peak_a * 0.14) / (pass + 1.6));
                QPen cpen(QColor(52, 40, 30, a_px), w_px, Qt::SolidLine, Qt::RoundCap);
                p.setPen(cpen);
                p.drawLine(QPointF(cx_top, 0.0), QPointF(sh_x, sh_y - 36.0));
            }

            // Wide, ultra-soft diagonal warm-taupe penumbra shadow on the wall behind/under the shade
            p.save();
            p.translate(sh_x - 10.0, sh_y + 14.0);
            p.rotate(-(angle_z_ + head_angle_z_) * 57.2958 * 0.5 - 12.0);
            p.scale(1.65, 1.28);
            QRadialGradient wall_shadow(QPointF(0.0, 0.0), 340.0);
            wall_shadow.setColorAt(0.0, QColor(52, 40, 30, qRound(peak_a)));
            wall_shadow.setColorAt(0.28, QColor(56, 44, 34, qRound(peak_a * 0.62)));
            wall_shadow.setColorAt(0.56, QColor(62, 48, 38, qRound(peak_a * 0.26)));
            wall_shadow.setColorAt(0.82, QColor(68, 54, 42, qRound(peak_a * 0.07)));
            wall_shadow.setColorAt(1.0, QColor(68, 54, 42, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(wall_shadow);
            p.drawEllipse(QPointF(0.0, 0.0), 340.0, 340.0);
            p.restore();

            // Blurry warm-taupe cast shadow on the ground at the bottom of the screen
            p.save();
            p.translate(sh_x + 4.0, H * 0.985);
            p.scale(2.30, 0.34);
            QRadialGradient ground_shadow(QPointF(0.0, 0.0), 310.0);
            const double g_a = peak_a * 0.88;
            ground_shadow.setColorAt(0.0, QColor(44, 34, 24, qRound(g_a)));
            ground_shadow.setColorAt(0.45, QColor(48, 38, 28, qRound(g_a * 0.44)));
            ground_shadow.setColorAt(0.78, QColor(54, 44, 34, qRound(g_a * 0.12)));
            ground_shadow.setColorAt(1.0, QColor(54, 44, 34, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(ground_shadow);
            p.drawEllipse(QPointF(0.0, 0.0), 310.0, 310.0);
            p.restore();
        }

        // Warm directional aperture glow below the shade opening when lit (with ignition pulse)
        if (t_lit > 0.01f) {
            const qreal halo_r = 125.0 + 42.0 * light_flash_ * (target_light_ > 0.5f ? 1.0 : -0.35);
            p.save();
            p.translate(bx, by + 26.0);
            p.rotate(beam_angle_deg);
            p.scale(1.05, 0.85);
            QRadialGradient halo(QPointF(0.0, 18.0), halo_r);
            halo.setColorAt(0.0, QColor(255, 224, 150, std::min(220, qRound(125.0 * t_lit))));
            halo.setColorAt(0.45, QColor(255, 210, 120, std::min(110, qRound(48.0 * t_lit))));
            halo.setColorAt(1.0, QColor(255, 200, 100, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(QPointF(0.0, 18.0), halo_r, halo_r);
            p.restore();
        }

        p.drawImage(rect(), frame_img_);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && event->pos().x() >= width() * 0.40) {
            dragging_ = true;
            toggled_on_drag_ = false;
            press_pos_ = event->pos();
            last_pos_ = event->pos();
            setCursor(Qt::ClosedHandCursor);
            ensureAnimating();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_) return;
        const QPoint delta = event->pos() - last_pos_;
        last_pos_ = event->pos();

        const float dy = static_cast<float>(event->pos().y() - press_pos_.y());
        pull_y_ = std::clamp(dy / 220.0f, 0.0f, 0.60f);
        pull_vel_y_ = 0.0f;

        vel_z_ += static_cast<float>(delta.x()) * 0.014f;
        wire_bow_vel_z_ -= static_cast<float>(delta.x()) * 0.025f;
        wire_bow_z_ = std::clamp(wire_bow_z_ - static_cast<float>(delta.x()) * 0.0025f, -0.55f, 0.55f);
        angle_z_ = std::clamp(angle_z_ + static_cast<float>(delta.x()) * 0.0024f, -0.34f, 0.34f);

        if (!toggled_on_drag_ && pull_y_ >= 0.24f) {
            toggled_on_drag_ = true;
            toggleThemeWithRecoil();
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            setCursor(Qt::OpenHandCursor);
            const QPoint total = event->pos() - press_pos_;
            if (!toggled_on_drag_ && total.manhattanLength() < 8) {
                // Click/tap gives a physical tug/swing/wire-whip reaction without toggling light
                pull_vel_y_ = 1.9f;
                vel_z_ += 0.35f;
                wire_bow_vel_z_ -= 0.45f;
            } else {
                pull_vel_y_ = -2.2f;
            }
            ensureAnimating();
        }
    }

private:
    void toggleThemeWithRecoil() {
        auto& tm = finn::ui::FinnThemeManager::instance();
        tm.setDark(!tm.isDark());
        const bool turning_on = !tm.isDark();
        target_light_ = turning_on ? 1.0f : 0.0f;
        light_flash_ = 1.0f;
        light_phase_ = 0.0f;
        pull_vel_y_ -= 1.5f;
        vel_z_ += 0.40f;
        wire_bow_vel_z_ -= 0.55f;
        play_lamp_switch_sound(turning_on);
    }

    void ensureAnimating() {
        if (!timer_.isActive()) timer_.start();
    }

    void stepPhysics() {
        constexpr float dt = 0.016f;
        constexpr float kGravityOverLen = 14.0f;
        constexpr float kPendulumDamp = 2.2f;
        const float acc_z = -kGravityOverLen * std::sin(angle_z_) - kPendulumDamp * vel_z_;
        const float acc_x = -kGravityOverLen * std::sin(angle_x_) - kPendulumDamp * vel_x_;
        vel_z_ += acc_z * dt;
        vel_x_ += acc_x * dt;
        angle_z_ += vel_z_ * dt;
        angle_x_ += vel_x_ * dt;

        // Subtle cord flex wave coupled to pendulum acceleration
        constexpr float kWireStiff = 28.0f;
        constexpr float kWireDamp = 3.2f;
        const float bow_acc_z = -kWireStiff * wire_bow_z_ - kWireDamp * wire_bow_vel_z_ - 0.18f * acc_z;
        wire_bow_vel_z_ += bow_acc_z * dt;
        wire_bow_z_ = std::clamp(wire_bow_z_ + wire_bow_vel_z_ * dt, -0.55f, 0.55f);

        // Subtle shade swivel lag at the lamp neck
        constexpr float kHeadStiff = 32.0f;
        constexpr float kHeadDamp = 4.6f;
        const float head_acc_z = -kHeadStiff * head_angle_z_ - kHeadDamp * head_vel_z_ - 0.22f * acc_z;
        const float head_acc_x = -kHeadStiff * head_angle_x_ - kHeadDamp * head_vel_x_ - 0.22f * acc_x;
        head_vel_z_ += head_acc_z * dt;
        head_vel_x_ += head_acc_x * dt;
        head_angle_z_ = std::clamp(head_angle_z_ + head_vel_z_ * dt, -0.10f, 0.10f);
        head_angle_x_ = std::clamp(head_angle_x_ + head_vel_x_ * dt, -0.10f, 0.10f);

        if (!dragging_) {
            constexpr float kSpring = 65.0f;
            constexpr float kSpringDamp = 7.5f;
            const float acc_y = -kSpring * pull_y_ - kSpringDamp * pull_vel_y_;
            pull_vel_y_ += acc_y * dt;
            pull_y_ = std::max(0.0f, pull_y_ + pull_vel_y_ * dt);
        }

        const float light_rate = (target_light_ > 0.5f) ? 0.24f : 0.11f;
        light_anim_ += (target_light_ - light_anim_) * light_rate;
        if (std::abs(target_light_ - light_anim_) < 0.003f) {
            light_anim_ = target_light_;
        }
        if (light_flash_ > 0.0f) {
            light_phase_ += dt * 36.0f;
            light_flash_ *= (target_light_ > 0.5f) ? 0.86f : 0.88f;
            if (light_flash_ < 0.004f) {
                light_flash_ = 0.0f;
            }
        }

        update();

        const bool resting = !dragging_ &&
            std::abs(angle_z_) < 0.0015f && std::abs(vel_z_) < 0.0015f &&
            std::abs(angle_x_) < 0.0015f && std::abs(vel_x_) < 0.0015f &&
            std::abs(head_angle_z_) < 0.0015f && std::abs(head_vel_z_) < 0.0015f &&
            std::abs(head_angle_x_) < 0.0015f && std::abs(head_vel_x_) < 0.0015f &&
            std::abs(wire_bow_z_) < 0.0015f && std::abs(wire_bow_vel_z_) < 0.0015f &&
            std::abs(pull_y_) < 0.0015f && std::abs(pull_vel_y_) < 0.0015f &&
            std::abs(target_light_ - light_anim_) < 0.001f &&
            light_flash_ < 0.002f;
        if (resting) {
            angle_z_ = vel_z_ = angle_x_ = vel_x_ = 0.0f;
            head_angle_z_ = head_vel_z_ = head_angle_x_ = head_vel_x_ = 0.0f;
            wire_bow_z_ = wire_bow_vel_z_ = 0.0f;
            pull_y_ = pull_vel_y_ = 0.0f;
            timer_.stop();
        }
    }

    std::vector<GlbSubMesh> meshes_;
    QImage frame_img_;
    std::vector<float> zbuf_;
    QTimer timer_;

    bool dragging_ = false;
    bool toggled_on_drag_ = false;
    QPoint press_pos_;
    QPoint last_pos_;

    float angle_z_ = 0.0f;
    float vel_z_ = 0.0f;
    float angle_x_ = 0.0f;
    float vel_x_ = 0.0f;
    float head_angle_z_ = 0.0f;
    float head_vel_z_ = 0.0f;
    float head_angle_x_ = 0.0f;
    float head_vel_x_ = 0.0f;
    float wire_bow_z_ = 0.0f;
    float wire_bow_vel_z_ = 0.0f;
    float pull_y_ = 0.0f;
    float pull_vel_y_ = 0.0f;

    float light_anim_ = 1.0f;
    float target_light_ = 1.0f;
    float light_flash_ = 0.0f;
    float light_phase_ = 0.0f;
};

}  // namespace finn::oobe
