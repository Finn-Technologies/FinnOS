// Generates X11/Wayland XCursor binary themes (finn-light and finn-dark)
// at nominal sizes 24, 32, and 48 from the Figma pointer SVGs in assets/pointers/.

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace {

constexpr int kSizes[3] = {24, 32, 48};

struct ThemePalette {
    QString id;
    QString display_name;
    QString comment;
    QColor body;
    QColor outline;
    QColor accent;
};

struct CursorSpec {
    QString primary;
    QStringList aliases;
    qreal hot_x_28 = 14.0;
    qreal hot_y_28 = 14.0;
};

void appendU32LE(QByteArray& out, std::uint32_t v) {
    char buf[4];
    buf[0] = static_cast<char>(v & 0xFFu);
    buf[1] = static_cast<char>((v >> 8) & 0xFFu);
    buf[2] = static_cast<char>((v >> 16) & 0xFFu);
    buf[3] = static_cast<char>((v >> 24) & 0xFFu);
    out.append(buf, 4);
}

QString readSvgFile(const QString& svg_dir, const QString& filename) {
    QFile f(QDir(svg_dir).filePath(filename));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

QString makeHaloSvg(const QString& raw_svg, const QColor& halo_color, qreal extra_width) {
    QString out = raw_svg;
    static const QRegularExpression kStrokeWidthRe(
        QStringLiteral("stroke-width=\"([0-9.]+)\""));

    int offset = 0;
    QRegularExpressionMatchIterator it = kStrokeWidthRe.globalMatch(out);
    QString rebuilt;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        rebuilt.append(out.mid(offset, m.capturedStart() - offset));
        const double w = m.captured(1).toDouble() + extra_width;
        rebuilt.append(QStringLiteral("stroke-width=\"%1\"").arg(w, 0, 'f', 2));
        offset = m.capturedEnd();
    }
    rebuilt.append(out.mid(offset));
    out = rebuilt;

    // Ensure paths without an explicit stroke-width (default 1.0) also get widened.
    static const QRegularExpression kPathTagRe(QStringLiteral("<path\\b([^>]*)>"));
    offset = 0;
    rebuilt.clear();
    it = kPathTagRe.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        rebuilt.append(out.mid(offset, m.capturedStart() - offset));
        QString attrs = m.captured(1);
        if (!attrs.contains(QStringLiteral("stroke-width="))) {
            attrs.prepend(
                QStringLiteral(" stroke-width=\"%1\"").arg(1.0 + extra_width, 0, 'f', 2));
        }
        rebuilt.append(QStringLiteral("<path") + attrs + QStringLiteral(">"));
        offset = m.capturedEnd();
    }
    rebuilt.append(out.mid(offset));
    out = rebuilt;

    out.replace(QStringLiteral("stroke=\"black\""),
                QStringLiteral("stroke=\"%1\"").arg(halo_color.name()));
    return out;
}

QString makeForegroundStrokeSvg(const QString& raw_svg, const QColor& body_color) {
    QString out = raw_svg;
    out.replace(QStringLiteral("stroke=\"black\""),
                QStringLiteral("stroke=\"%1\"").arg(body_color.name()));
    return out;
}

void renderSvgLayer(QPainter& p, const QString& svg_xml, int size, qreal rotation_deg = 0.0) {
    if (svg_xml.isEmpty()) return;
    QSvgRenderer renderer(svg_xml.toUtf8());
    if (!renderer.isValid()) return;
    p.save();
    if (std::abs(rotation_deg) > 0.01) {
        const qreal half = static_cast<qreal>(size) * 0.5;
        p.translate(half, half);
        p.rotate(rotation_deg);
        p.translate(-half, -half);
    }
    renderer.render(&p, QRectF(0.0, 0.0, static_cast<qreal>(size), static_cast<qreal>(size)));
    p.restore();
}

void renderDefaultArrow(QPainter& p, const QString& default_svg, int size,
                        const ThemePalette& pal, bool with_hand_badge) {
    const qreal scale = static_cast<qreal>(size) / 28.0;

    // 1. Subtle drop shadow for depth over any surface
    QString shadow_svg = default_svg;
    shadow_svg.replace(
        QStringLiteral("fill=\"black\" fill-opacity=\"0.5\""),
        QStringLiteral("fill=\"#000000\" fill-opacity=\"0.20\" stroke=\"#000000\" "
                       "stroke-opacity=\"0.18\" stroke-width=\"2.6\" stroke-linejoin=\"round\""));
    p.save();
    p.translate(0.0, 0.55 * scale);
    renderSvgLayer(p, shadow_svg, size);
    p.restore();

    // 2. Crisp 1.2px outer outline (2.4px centered stroke behind the fill)
    QString outline_svg = default_svg;
    outline_svg.replace(
        QStringLiteral("fill=\"black\" fill-opacity=\"0.5\""),
        QStringLiteral("fill=\"%1\" fill-opacity=\"1.0\" stroke=\"%1\" "
                       "stroke-width=\"2.4\" stroke-linejoin=\"round\" stroke-linecap=\"round\"")
            .arg(pal.outline.name()));
    renderSvgLayer(p, outline_svg, size);

    // 3. Crisp high-contrast interior fill
    QString body_svg = default_svg;
    body_svg.replace(
        QStringLiteral("fill=\"black\" fill-opacity=\"0.5\""),
        QStringLiteral("fill=\"%1\" fill-opacity=\"1.0\"").arg(pal.body.name()));
    renderSvgLayer(p, body_svg, size);

    if (with_hand_badge) {
        const QRectF badge(16.0 * scale, 16.0 * scale, 7.6 * scale, 7.6 * scale);
        p.setPen(QPen(pal.outline, std::max<qreal>(1.1, 1.2 * scale)));
        p.setBrush(pal.accent);
        p.drawEllipse(badge);

        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        const QPointF center = badge.center();
        const qreal r = 1.35 * scale;
        p.drawEllipse(center, r, r);
    }
}

void renderForbiddenCursor(QPainter& p, int size, const ThemePalette& pal) {
    const qreal scale = static_cast<qreal>(size) / 28.0;
    const QPointF center(14.0 * scale, 14.0 * scale);
    const qreal radius = 7.4 * scale;
    const QPointF slash_a(8.8 * scale, 8.8 * scale);
    const QPointF slash_b(19.2 * scale, 19.2 * scale);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pal.outline, 3.8 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawEllipse(center, radius, radius);
    p.drawLine(slash_a, slash_b);

    p.setPen(QPen(pal.body, 1.55 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawEllipse(center, radius, radius);
    p.drawLine(slash_a, slash_b);
}

QByteArray serializeXCursor(const std::vector<QImage>& images, qreal hot_x_28, qreal hot_y_28) {
    const std::uint32_t ntoc = static_cast<std::uint32_t>(images.size());
    QByteArray out;
    out.reserve(8192);

    // Xcursor file header (16 bytes)
    appendU32LE(out, 0x72756358u);  // "Xcur"
    appendU32LE(out, 16u);          // header size
    appendU32LE(out, 0x00010000u);  // version 1.0
    appendU32LE(out, ntoc);

    std::uint32_t offset = 16u + ntoc * 12u;
    for (const QImage& img : images) {
        const std::uint32_t sz = static_cast<std::uint32_t>(img.width());
        appendU32LE(out, 0xfffd0002u);  // XCURSOR_IMAGE_TYPE
        appendU32LE(out, sz);           // subtype = nominal size
        appendU32LE(out, offset);
        offset += 36u + sz * sz * 4u;
    }

    for (const QImage& raw : images) {
        const QImage img = raw.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const std::uint32_t w = static_cast<std::uint32_t>(img.width());
        const std::uint32_t h = static_cast<std::uint32_t>(img.height());
        const std::uint32_t xhot = static_cast<std::uint32_t>(std::clamp<int>(
            static_cast<int>(std::lround(hot_x_28 * static_cast<qreal>(w) / 28.0)),
            0, static_cast<int>(w) - 1));
        const std::uint32_t yhot = static_cast<std::uint32_t>(std::clamp<int>(
            static_cast<int>(std::lround(hot_y_28 * static_cast<qreal>(h) / 28.0)),
            0, static_cast<int>(h) - 1));

        // 36-byte image chunk header
        appendU32LE(out, 36u);
        appendU32LE(out, 0xfffd0002u);
        appendU32LE(out, w);
        appendU32LE(out, 1u);
        appendU32LE(out, w);
        appendU32LE(out, h);
        appendU32LE(out, xhot);
        appendU32LE(out, yhot);
        appendU32LE(out, 50u);

        for (std::uint32_t y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(static_cast<int>(y)));
            for (std::uint32_t x = 0; x < w; ++x) {
                appendU32LE(out, static_cast<std::uint32_t>(row[x]));
            }
        }
    }
    return out;
}

bool writeCursorWithAliases(const QDir& cursors_dir, const CursorSpec& spec,
                            const QByteArray& payload) {
    const QString primary_path = cursors_dir.filePath(spec.primary);
    {
        QFile f(primary_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "finn-cursors: cannot write %s\n",
                         primary_path.toUtf8().constData());
            return false;
        }
        if (f.write(payload) != payload.size()) return false;
    }

    for (const QString& alias : spec.aliases) {
        if (alias == spec.primary) continue;
        const QString link_path = cursors_dir.filePath(alias);
        QFile::remove(link_path);
        if (!QFile::link(spec.primary, link_path)) {
            QFile af(link_path);
            if (!af.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            af.write(payload);
        }
    }
    return true;
}

bool writeTextFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(content) == content.size();
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);

    QString svg_dir = QStringLiteral("assets/pointers");
    if (!QFileInfo::exists(svg_dir)) {
        svg_dir = QStringLiteral("/usr/share/finn/pointers");
    }
    QString out_icons_dir = QStringLiteral("/usr/share/icons");

    const QStringList args = app.arguments();
    if (args.size() >= 2 && !args[1].isEmpty()) svg_dir = args[1];
    if (args.size() >= 3 && !args[2].isEmpty()) out_icons_dir = args[2];

    const QString default_svg = readSvgFile(svg_dir, QStringLiteral("default.svg"));
    const QString text_svg = readSvgFile(svg_dir, QStringLiteral("text.svg"));
    const QString cross_svg = readSvgFile(svg_dir, QStringLiteral("cross.svg"));
    const QString loading_svg = readSvgFile(svg_dir, QStringLiteral("loading.svg"));
    const QString zoom_in_svg = readSvgFile(svg_dir, QStringLiteral("zoom-in.svg"));
    const QString zoom_out_svg = readSvgFile(svg_dir, QStringLiteral("zoom-out.svg"));
    const QString resize_ew_svg = readSvgFile(svg_dir, QStringLiteral("resize-ew.svg"));
    const QString resize_nwse_svg = readSvgFile(svg_dir, QStringLiteral("resize-nwse.svg"));
    const QString resize_nesw_svg = readSvgFile(svg_dir, QStringLiteral("resize-nesw.svg"));

    if (default_svg.isEmpty() || resize_ew_svg.isEmpty()) {
        std::fprintf(stderr, "finn-cursors: missing pointer SVGs in %s\n",
                     svg_dir.toUtf8().constData());
        return 1;
    }

    const std::vector<ThemePalette> palettes = {
        {
            QStringLiteral("finn-light"),
            QStringLiteral("Finn Light"),
            QStringLiteral("FinnOS Figma Light Pointers"),
            QColor(0x14, 0x16, 0x1B),
            QColor(0xFF, 0xFF, 0xFF),
            QColor(0x00, 0x7B, 0xFF),
        },
        {
            QStringLiteral("finn-dark"),
            QStringLiteral("Finn Dark"),
            QStringLiteral("FinnOS Figma Dark Pointers"),
            QColor(0xF4, 0xF6, 0xFA),
            QColor(0x14, 0x16, 0x1B),
            QColor(0x00, 0x7B, 0xFF),
        },
    };

    for (const auto& pal : palettes) {
        const QString theme_root = QDir(out_icons_dir).filePath(pal.id);
        const QString cursors_path = QDir(theme_root).filePath(QStringLiteral("cursors"));
        if (!QDir().mkpath(cursors_path)) {
            std::fprintf(stderr, "finn-cursors: cannot create %s\n",
                         cursors_path.toUtf8().constData());
            return 1;
        }
        const QDir cursors_dir(cursors_path);

        const QByteArray index_theme =
            QStringLiteral("[Icon Theme]\nName=%1\nComment=%2\n")
                .arg(pal.display_name, pal.comment)
                .toUtf8();
        if (!writeTextFile(QDir(theme_root).filePath(QStringLiteral("index.theme")),
                           index_theme)) {
            return 1;
        }

        auto buildImages = [&](const std::function<void(QPainter&, int)>& paint_fn) {
            std::vector<QImage> imgs;
            imgs.reserve(3);
            for (int sz : kSizes) {
                QImage img(sz, sz, QImage::Format_ARGB32_Premultiplied);
                img.fill(Qt::transparent);
                QPainter p(&img);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                paint_fn(p, sz);
                p.end();
                imgs.push_back(std::move(img));
            }
            return imgs;
        };

        auto buildStrokeImages = [&](const QString& svg, qreal rot_deg = 0.0) {
            const QString halo = makeHaloSvg(svg, pal.outline, 2.2);
            const QString fg = makeForegroundStrokeSvg(svg, pal.body);
            return buildImages([&](QPainter& p, int sz) {
                renderSvgLayer(p, halo, sz, rot_deg);
                renderSvgLayer(p, fg, sz, rot_deg);
            });
        };

        // 1. Default arrow (Figma 2:6, tip at 5.4, 5.4 -> hotspot 5, 5)
        {
            const CursorSpec spec{
                QStringLiteral("left_ptr"),
                {QStringLiteral("default"), QStringLiteral("arrow"),
                 QStringLiteral("top_left_arrow"), QStringLiteral("right_ptr"),
                 QStringLiteral("context-menu"), QStringLiteral("alias"),
                 QStringLiteral("copy"), QStringLiteral("dnd-copy"),
                 QStringLiteral("dnd-none"), QStringLiteral("help"),
                 QStringLiteral("whats_this"), QStringLiteral("question_arrow")},
                5.0,
                5.0,
            };
            const auto imgs = buildImages([&](QPainter& p, int sz) {
                renderDefaultArrow(p, default_svg, sz, pal, false);
            });
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 2. Pointing hand / interactive pointer (hotspot 5, 5)
        {
            const CursorSpec spec{
                QStringLiteral("hand2"),
                {QStringLiteral("pointer"), QStringLiteral("pointing_hand"),
                 QStringLiteral("hand1"), QStringLiteral("openhand")},
                5.0,
                5.0,
            };
            const auto imgs = buildImages([&](QPainter& p, int sz) {
                renderDefaultArrow(p, default_svg, sz, pal, true);
            });
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 3. Text / I-beam (Figma text.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("xterm"),
                {QStringLiteral("ibeam"), QStringLiteral("text"),
                 QStringLiteral("vertical-text")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(text_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 4. Crosshair (Figma cross.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("crosshair"),
                {QStringLiteral("cross"), QStringLiteral("tcross"),
                 QStringLiteral("plus"), QStringLiteral("cell")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(cross_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 5. Loading / wait (Figma loading.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("watch"),
                {QStringLiteral("wait"), QStringLiteral("left_ptr_watch"),
                 QStringLiteral("progress"), QStringLiteral("half-busy")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(loading_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 6. Zoom in (Figma zoom-in.svg, hotspot 13, 13)
        {
            const CursorSpec spec{
                QStringLiteral("zoom-in"),
                {QStringLiteral("zoom_in")},
                13.0,
                13.0,
            };
            const auto imgs = buildStrokeImages(zoom_in_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 7. Zoom out (Figma zoom-out.svg, hotspot 13, 13)
        {
            const CursorSpec spec{
                QStringLiteral("zoom-out"),
                {QStringLiteral("zoom_out")},
                13.0,
                13.0,
            };
            const auto imgs = buildStrokeImages(zoom_out_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 8. Horizontal resize (Figma resize-ew.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("ew-resize"),
                {QStringLiteral("sb_h_double_arrow"), QStringLiteral("size_hor"),
                 QStringLiteral("e-resize"), QStringLiteral("w-resize"),
                 QStringLiteral("col-resize"), QStringLiteral("h_double_arrow"),
                 QStringLiteral("left_side"), QStringLiteral("right_side"),
                 QStringLiteral("split_h")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(resize_ew_svg, 0.0);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 9. Vertical resize (90-degree rotated Figma resize-ew.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("ns-resize"),
                {QStringLiteral("size_ver"), QStringLiteral("sb_v_double_arrow"),
                 QStringLiteral("n-resize"), QStringLiteral("s-resize"),
                 QStringLiteral("row-resize"), QStringLiteral("v_double_arrow"),
                 QStringLiteral("top_side"), QStringLiteral("bottom_side"),
                 QStringLiteral("split_v")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(resize_ew_svg, 90.0);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 10. Diagonal NW-SE resize (Figma resize-nwse.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("nwse-resize"),
                {QStringLiteral("size_fdiag"), QStringLiteral("nw-resize"),
                 QStringLiteral("se-resize"), QStringLiteral("top_left_corner"),
                 QStringLiteral("bottom_right_corner"), QStringLiteral("bd_double_arrow")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(resize_nwse_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 11. Diagonal NE-SW resize (Figma resize-nesw.svg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("nesw-resize"),
                {QStringLiteral("size_bdiag"), QStringLiteral("ne-resize"),
                 QStringLiteral("sw-resize"), QStringLiteral("top_right_corner"),
                 QStringLiteral("bottom_left_corner"), QStringLiteral("fd_double_arrow")},
                14.0,
                14.0,
            };
            const auto imgs = buildStrokeImages(resize_nesw_svg);
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 12. 4-way move / grab / grabbing (Figma resize-ew.svg + 90-deg, hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("fleur"),
                {QStringLiteral("move"), QStringLiteral("grab"),
                 QStringLiteral("grabbing"), QStringLiteral("all-scroll"),
                 QStringLiteral("size_all"), QStringLiteral("closedhand"),
                 QStringLiteral("dnd-move"), QStringLiteral("dnd-ask")},
                14.0,
                14.0,
            };
            const QString halo = makeHaloSvg(resize_ew_svg, pal.outline, 2.2);
            const QString fg = makeForegroundStrokeSvg(resize_ew_svg, pal.body);
            const auto imgs = buildImages([&](QPainter& p, int sz) {
                renderSvgLayer(p, halo, sz, 0.0);
                renderSvgLayer(p, halo, sz, 90.0);
                renderSvgLayer(p, fg, sz, 0.0);
                renderSvgLayer(p, fg, sz, 90.0);
            });
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }

        // 13. Forbidden / not-allowed (hotspot 14, 14)
        {
            const CursorSpec spec{
                QStringLiteral("not-allowed"),
                {QStringLiteral("forbidden"), QStringLiteral("crossed_circle"),
                 QStringLiteral("no-drop"), QStringLiteral("pirate")},
                14.0,
                14.0,
            };
            const auto imgs = buildImages([&](QPainter& p, int sz) {
                renderForbiddenCursor(p, sz, pal);
            });
            if (!writeCursorWithAliases(cursors_dir, spec,
                                        serializeXCursor(imgs, spec.hot_x_28, spec.hot_y_28))) {
                return 1;
            }
        }
    }

    // Write /usr/share/icons/default/index.theme -> Inherits=finn-light
    const QString default_theme_index =
        QDir(out_icons_dir).filePath(QStringLiteral("default/index.theme"));
    const QByteArray default_index_bytes = QByteArrayLiteral(
        "[Icon Theme]\n"
        "Name=Default\n"
        "Comment=Default Cursor Theme\n"
        "Inherits=finn-light\n");
    if (!writeTextFile(default_theme_index, default_index_bytes)) {
        return 1;
    }

    return 0;
}
