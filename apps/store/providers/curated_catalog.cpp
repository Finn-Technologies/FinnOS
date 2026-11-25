#include "curated_catalog.hpp"

namespace finn::store {

std::vector<StoreAppItem> build_curated_catalog() {
    std::vector<StoreAppItem> out;

    // ── Native FinnOS Apps (Primary Ecosystem) ───────────────────────────────
    {
        StoreAppItem a;
        a.id = "files";
        a.session_app_id = "files";
        a.name = "Finn Files";
        a.subtitle = "Fast native file manager & cross-OS package launcher";
        a.description = "Browse local volumes, inspect macOS .app and .finnapp bundles, and double-click Linux, Windows, Android, or OpenHarmony packages directly into Finn Store.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "Productivity";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "files";
        a.rating = 4.9;
        a.review_count = 1240;
        a.size_label = "4.2 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.featured_hero = true;
        a.editorial_tag = "FINNOS ESSENTIAL";
        a.accent_hex = "#3b82f6";
        a.permissions = {"Native Wayland", "Home Directory Access"};
        a.install_ref = "files";
        out.push_back(std::move(a));
    }
    {
        StoreAppItem a;
        a.id = "terminal";
        a.session_app_id = "terminal";
        a.name = "Finn Terminal";
        a.subtitle = "GPU-accelerated native terminal emulator";
        a.description = "Clean, low-latency terminal built with Qt 6 and FinnUI tokens. Supports tabs, split panes, truecolor themes, and seamless shell integration.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "Developer";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "terminal";
        a.rating = 4.9;
        a.review_count = 980;
        a.size_label = "3.6 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.editorial_tag = "DEVELOPER PICK";
        a.accent_hex = "#0ea5e9";
        a.permissions = {"Native Wayland", "User Shell"};
        a.install_ref = "terminal";
        out.push_back(std::move(a));
    }
    {
        StoreAppItem a;
        a.id = "notes";
        a.session_app_id = "notes";
        a.name = "Finn Notes";
        a.subtitle = "Distraction-free Markdown notes & scratchpad";
        a.description = "Instant startup, automatic saving, and crisp Fixel Display typography designed for everyday notes, code snippets, and plain-text documents.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "Productivity";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "notes";
        a.rating = 4.8;
        a.review_count = 740;
        a.size_label = "2.9 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.accent_hex = "#f59e0b";
        a.permissions = {"Native Wayland", "Documents Folder"};
        a.install_ref = "notes";
        out.push_back(std::move(a));
    }
    {
        StoreAppItem a;
        a.id = "viewer";
        a.session_app_id = "viewer";
        a.name = "Finn Viewer";
        a.subtitle = "Instant image & media viewer";
        a.description = "Inspect PNG, JPEG, WebP, SVG, and high-DPI screenshots with smooth zoom, pan, and keyboard navigation.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "Media";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "viewer";
        a.rating = 4.8;
        a.review_count = 510;
        a.size_label = "3.1 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.accent_hex = "#ec4899";
        a.permissions = {"Native Wayland", "Read-Only Media"};
        a.install_ref = "viewer";
        out.push_back(std::move(a));
    }
    {
        StoreAppItem a;
        a.id = "monitor";
        a.session_app_id = "monitor";
        a.name = "Finn Monitor";
        a.subtitle = "Real-time CPU, memory, thermal & process telemetry";
        a.description = "Inspect live system load, running Wayland and compatibility processes, and memory pressure without leaving the FinnOS design language.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "System";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "monitor";
        a.rating = 4.9;
        a.review_count = 620;
        a.size_label = "3.4 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.accent_hex = "#14b8a6";
        a.permissions = {"Native Wayland", "System Telemetry (/proc)"};
        a.install_ref = "monitor";
        out.push_back(std::move(a));
    }
    {
        StoreAppItem a;
        a.id = "calculator";
        a.session_app_id = "calculator";
        a.name = "Finn Calculator";
        a.subtitle = "Standard & scientific calculator with history";
        a.description = "Clean native calculator with keyboard entry, memory registers, scientific functions, and expression history.";
        a.developer = "FinnOS Core";
        a.version = "1.0.0";
        a.category = "Utilities";
        a.ecosystem = StoreEcosystem::Native;
        a.icon = "calculator";
        a.rating = 4.8;
        a.review_count = 410;
        a.size_label = "2.1 MB";
        a.verified = true;
        a.verified_source = "FinnOS Core";
        a.curated = true;
        a.accent_hex = "#334155";
        a.permissions = {"Native Wayland"};
        a.install_ref = "calculator";
        out.push_back(std::move(a));
    }

    // ── Linux (Flatpak / Flathub) ────────────────────────────────────────────
    auto add_fp = [&](const char* fpid, const char* name, const char* sub, const char* dev,
                      const char* cat, double rating, const char* sz, const char* vsrc,
                      const char* hex, std::vector<std::string> perms, bool hero = false,
                      const char* tag = "") {
        StoreAppItem a;
        a.id = std::string("flatpak:") + fpid;
        a.install_ref = fpid;
        a.name = name;
        a.subtitle = sub;
        a.description = std::string(sub) + ". Installed and updated cleanly inside a user-level Flatpak sandbox from Flathub.";
        a.developer = dev;
        a.category = cat;
        a.ecosystem = StoreEcosystem::Flatpak;
        a.icon = "linux-app";
        a.rating = rating;
        a.size_label = sz;
        a.verified = true;
        a.verified_source = vsrc;
        a.curated = true;
        a.featured_hero = hero;
        a.editorial_tag = tag;
        a.accent_hex = hex;
        a.permissions = std::move(perms);
        out.push_back(std::move(a));
    };

    add_fp("org.mozilla.firefox", "Firefox", "Fast, private web browser by Mozilla",
           "Mozilla", "Internet", 4.8, "98 MB", "mozilla.org", "#f97316",
           {"Flatpak Sandbox", "Wayland", "Network", "Audio", "Downloads"},
           true, "EDITOR'S CHOICE");
    add_fp("com.visualstudio.code", "Visual Studio Code", "Code editing. Redefined.",
           "Microsoft", "Developer", 4.9, "135 MB", "flathub.org", "#0284c7",
           {"Flatpak Sandbox", "Wayland", "Filesystem", "Network"}, false, "DEVELOPER");
    add_fp("org.gimp.GIMP", "GIMP", "Create images and edit photographs",
           "The GIMP Team", "Graphics", 4.7, "112 MB", "gimp.org", "#8b5cf6",
           {"Flatpak Sandbox", "Wayland", "Host Files"});
    add_fp("com.obsproject.Studio", "OBS Studio", "Live streaming and screen recording",
           "OBS Project", "Media", 4.9, "84 MB", "obsproject.com", "#334155",
           {"Flatpak Sandbox", "PipeWire ScreenCast", "Audio", "GPU"});
    add_fp("org.videolan.VLC", "VLC", "Play any video or audio format",
           "VideoLAN", "Media", 4.8, "62 MB", "videolan.org", "#ea580c",
           {"Flatpak Sandbox", "Wayland", "Audio", "Media Files"});
    add_fp("org.blender.Blender", "Blender", "Free and open 3D creation suite",
           "Blender Foundation", "Graphics", 4.9, "280 MB", "blender.org", "#d97706",
           {"Flatpak Sandbox", "Vulkan / OpenGL GPU", "Host Files"});

    // ── Android (Aurora / Google Play -> finn-android) ───────────────────────
    auto add_and = [&](const char* pkg, const char* name, const char* sub, const char* dev,
                       const char* cat, double rating, const char* sz, const char* hex,
                       std::vector<std::string> perms, const char* tag = "") {
        StoreAppItem a;
        a.id = std::string("android:") + pkg;
        a.install_ref = pkg;
        a.name = name;
        a.subtitle = sub;
        a.description = std::string(sub) + ". Delivered via Aurora anonymous Play API (including split APKs) and installed into FinnOS's clean-room finn-android runtime.";
        a.developer = dev;
        a.category = cat;
        a.ecosystem = StoreEcosystem::Android;
        a.icon = "android";
        a.rating = rating;
        a.size_label = sz;
        a.verified = true;
        a.verified_source = "Google Play (Aurora)";
        a.curated = true;
        a.editorial_tag = tag;
        a.accent_hex = hex;
        a.permissions = std::move(perms);
        out.push_back(std::move(a));
    };

    add_and("org.thoughtcrime.securesms", "Signal", "Private encrypted messaging",
            "Signal Foundation", "Social", 4.8, "64 MB", "#2563eb",
            {"Android Runtime (finn-android)", "Network", "Notifications"}, "PRIVACY PICK");
    add_and("org.fdroid.fdroid", "F-Droid", "Free & open-source Android app repository",
            "F-Droid Limited", "Utilities", 4.7, "11 MB", "#16a34a",
            {"Android Runtime (finn-android)", "Network", "Package Storage"});
    add_and("org.videolan.vlc", "VLC for Android", "Mobile & tablet media player",
            "Videolabs", "Media", 4.7, "38 MB", "#f97316",
            {"Android Runtime (finn-android)", "Media Storage", "Audio"});
    add_and("app.organicmaps", "Organic Maps", "Offline OpenStreetMap navigation",
            "Organic Maps", "Navigation", 4.9, "72 MB", "#10b981",
            {"Android Runtime (finn-android)", "Offline Maps Storage"});

    // ── Windows (winget-pkgs -> finn-win) ────────────────────────────────────
    auto add_win = [&](const char* wgid, const char* name, const char* sub, const char* dev,
                       const char* cat, double rating, const char* sz, const char* hex,
                       const char* dl_url = "", const char* tag = "") {
        StoreAppItem a;
        a.id = std::string("winget:") + wgid;
        a.install_ref = wgid;
        a.name = name;
        a.subtitle = sub;
        a.description = std::string(sub) + ". Sourced from microsoft/winget-pkgs with SHA-256 verification and installed into your FinnOS finn-win prefix.";
        a.developer = dev;
        a.category = cat;
        a.ecosystem = StoreEcosystem::Windows;
        a.icon = "windows";
        a.rating = rating;
        a.size_label = sz;
        a.verified = true;
        a.verified_source = "winget-pkgs (SHA-256)";
        a.curated = true;
        a.editorial_tag = tag;
        a.accent_hex = hex;
        a.download_url = dl_url;
        a.permissions = {"Windows Prefix (finn-win)", "SHA-256 Verified"};
        out.push_back(std::move(a));
    };

    add_win("7zip.7zip", "7-Zip", "High-compression archive manager",
            "Igor Pavlov", "Utilities", 4.9, "1.6 MB", "#475569", "", "CLASSIC");
    add_win("Notepad++.Notepad++", "Notepad++", "Fast source code & text editor",
            "Don Ho", "Developer", 4.8, "4.5 MB", "#22c55e", "", "POPULAR");
    add_win("SumatraPDF.SumatraPDF", "SumatraPDF", "Lightweight PDF, ePub & comic reader",
            "Krzysztof Kowalczyk", "Productivity", 4.8, "7.8 MB", "#eab308");
    add_win("KeePassXCTeam.KeePassXC", "KeePassXC", "Cross-platform password manager",
            "KeePassXC Team", "Security", 4.9, "31 MB", "#10b981");

    return out;
}

}  // namespace finn::store
