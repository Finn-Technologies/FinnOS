#ifndef FLOURISH_START_PAGE_HPP
#define FLOURISH_START_PAGE_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QString>
#include <QVector>

#include "tab_model.hpp"

namespace flourish {

/**
 * @brief Generates the theme-matched HTML for flourish://home using live FinnTokens.
 */
inline QString render_start_page_html(const finn::ui::FinnTokens& t,
                                      const BrowserSpace& space,
                                      const QVector<BrowserTab>& favorites) {
    const QString bg = t.bone.name(QColor::HexRgb);
    const QString tile = t.tile.name(QColor::HexRgb);
    const QString input_bg = t.surface_input.name(QColor::HexRgb);
    const QString ink = t.ink.name(QColor::HexRgb);
    const QString dim = t.dim.name(QColor::HexRgb);
    const QString mute = t.mute.name(QColor::HexRgb);
    const QString accent = space.accent.isValid()
                               ? space.accent.name(QColor::HexRgb)
                               : t.accent.name(QColor::HexRgb);
    auto css_rgba = [](const QColor& c) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alphaF(), 0, 'f', 3);
    };
    const QString hair = css_rgba(t.hair);
    const QString hover_bg = css_rgba(t.surface_hover);

    QString fav_cards;
    for (const auto& f : favorites) {
        if (f.url.scheme() == QStringLiteral("flourish")) continue;
        const QString host = display_host_for_url(f.url);
        const QString title = f.title.toHtmlEscaped();
        const QString href = f.url.toString().toHtmlEscaped();
        const QString initial = title.isEmpty() ? QStringLiteral("W") : title.left(1).toUpper();
        fav_cards += QStringLiteral(
            "<a class='fav' href='%1'>"
            "  <div class='fav-icon'>%2</div>"
            "  <div class='fav-meta'>"
            "    <div class='fav-title'>%3</div>"
            "    <div class='fav-host'>%4</div>"
            "  </div>"
            "</a>")
            .arg(href, initial.toHtmlEscaped(), title, host.toHtmlEscaped());
    }

    if (fav_cards.isEmpty()) {
        fav_cards = QStringLiteral(
            "<div class='empty-fav'>Pin any tab to Favorites from the sidebar to keep it one click away.</div>");
    }

    return QStringLiteral(
        "<!doctype html>"
        "<html><head><meta charset='utf-8'>"
        "<title>Flourish — %1</title>"
        "<style>"
        "  * { box-sizing: border-box; }"
        "  body {"
        "    margin: 0; min-height: 100vh;"
        "    background: %2; color: %3;"
        "    font-family: 'Fixel Display', 'DejaVu Sans', system-ui, sans-serif;"
        "    display: flex; align-items: center; justify-content: center;"
        "    padding: 40px 24px;"
        "  }"
        "  .wrap { width: 100%%; max-width: 660px; }"
        "  .pill {"
        "    display: inline-flex; align-items: center; gap: 8px;"
        "    padding: 5px 12px; border-radius: 999px;"
        "    background: %4; border: 1px solid %5;"
        "    font-size: 12px; color: %6; margin-bottom: 18px;"
        "  }"
        "  .dot { width: 8px; height: 8px; border-radius: 999px; background: %7; }"
        "  h1 {"
        "    margin: 0 0 8px 0; font-size: 36px; font-weight: 600;"
        "    letter-spacing: -0.02em; color: %3;"
        "  }"
        "  .sub { margin: 0 0 28px 0; font-size: 14px; color: %6; }"
        "  .search-box {"
        "    display: flex; align-items: center; gap: 10px;"
        "    background: %8; border: 1px solid %5; border-radius: 12px;"
        "    padding: 12px 16px; margin-bottom: 28px;"
        "    transition: border-color 140ms ease;"
        "  }"
        "  .search-box:focus-within { border-color: %7; }"
        "  .search-box input {"
        "    flex: 1; border: none; outline: none; background: transparent;"
        "    color: %3; font-size: 15px; font-family: inherit;"
        "  }"
        "  .search-box input::placeholder { color: %9; }"
        "  .kbd {"
        "    font-size: 11px; color: %9; padding: 3px 7px;"
        "    border: 1px solid %5; border-radius: 6px;"
        "  }"
        "  .section-label {"
        "    font-size: 12px; font-weight: 500;"
        "    color: %9; margin-bottom: 12px;"
        "  }"
        "  .grid {"
        "    display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));"
        "    gap: 10px; margin-bottom: 28px;"
        "  }"
        "  .fav {"
        "    display: flex; align-items: center; gap: 12px; text-decoration: none;"
        "    background: %4; border: 1px solid %5; border-radius: 10px;"
        "    padding: 12px 14px; color: %3; transition: background 120ms ease;"
        "  }"
        "  .fav:hover { background: %10; border-color: %7; }"
        "  .fav-icon {"
        "    width: 30px; height: 30px; border-radius: 8px;"
        "    background: %7; color: #fff; font-weight: 600; font-size: 13px;"
        "    display: flex; align-items: center; justify-content: center; flex-shrink: 0;"
        "  }"
        "  .fav-meta { min-width: 0; }"
        "  .fav-title { font-size: 13px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }"
        "  .fav-host { font-size: 11px; color: %6; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }"
        "  .empty-fav { font-size: 13px; color: %6; padding: 14px; background: %4; border: 1px solid %5; border-radius: 10px; }"
        "  .hints { display: flex; flex-wrap: wrap; gap: 16px; font-size: 12px; color: %6; }"
        "  .hints span { display: inline-flex; align-items: center; gap: 6px; }"
        "</style></head><body>"
        "<div class='wrap'>"
        "  <div class='pill'><span class='dot'></span><span>Flourish · %1</span></div>"
        "  <h1>Flourish</h1>"
        "  <p class='sub'>Web browser for FinnOS</p>"
        "  <form class='search-box' action='https://www.google.com/search' method='get'>"
        "    <input type='text' name='q' placeholder='Search or enter address' autofocus />"
        "    <span class='kbd'>Ctrl+L</span>"
        "  </form>"
        "  <div class='section-label'>Favorites</div>"
        "  <div class='grid'>%11</div>"
        "  <div class='hints'>"
        "    <span><span class='kbd'>Ctrl+T</span> Command Bar</span>"
        "    <span><span class='kbd'>Ctrl+S</span> Toggle Sidebar</span>"
        "    <span><span class='kbd'>Ctrl+Shift+D</span> Split View</span>"
        "    <span><span class='kbd'>Ctrl+F</span> Find in Page</span>"
        "  </div>"
        "</div>"
        "</body></html>")
        .arg(space.name.toHtmlEscaped(), bg, ink, tile, hair, dim, accent,
             input_bg, mute, hover_bg, fav_cards);
}

}  // namespace flourish

#endif
