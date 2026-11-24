// Headless ASan/UBSan unit tests for Finn Store providers:
//   - Protobuf wire codec (varint, fixed32, length-delimited)
//   - Aurora anonymous dispenser JSON & Google Play FDFE Item/Delivery protobufs
//   - Flathub API v2 hits/appstream JSON & flatpak updates parsing
//   - winget.run search JSON & microsoft/winget-pkgs YAML manifest parsing
//   - Curated multi-ecosystem catalog separation integrity

#include "providers/aurora_play_provider.hpp"
#include "providers/curated_catalog.hpp"
#include "providers/flathub_provider.hpp"
#include "providers/protobuf_wire.hpp"
#include "providers/winget_provider.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::fprintf(stderr, "test_store FAILED: %s (line %d)\n", #cond, __LINE__); \
            return false;                                                         \
        }                                                                         \
    } while (0)

bool test_protobuf_and_aurora_play() {
    using namespace finn::store;
    // 1. Dispenser JSON parsing
    const std::string disp_json =
        R"({"email":"anon@auroraoss.com","auth":"tok_abc123","gsfId":"3928174650","deviceCheckInConsistencyToken":"chk_xyz"})";
    const auto auth = parse_aurora_dispenser_json(disp_json);
    CHECK(auth.authenticated);
    CHECK(auth.auth_token == "tok_abc123");
    CHECK(auth.gsf_id == "3928174650");
    CHECK(build_play_fdfe_headers(auth).size() >= 5);
    CHECK(build_aurora_device_properties_json("arm64-v8a").find("arm64-v8a") != std::string::npos);

    // 2. Synthesize a Google Play FDFE ResponseWrapper -> Item protobuf
    std::string app_details;
    pb::encode_bytes_field(&app_details, 1, "Signal Foundation");
    pb::encode_bytes_field(&app_details, 4, "7.24.2");
    pb::encode_varint_field(&app_details, 9, 64 * 1024 * 1024ULL);
    pb::encode_bytes_field(&app_details, 10, "android.permission.INTERNET");

    std::string details;
    pb::encode_bytes_field(&details, 1, app_details);

    std::string rating;
    pb::encode_float_field(&rating, 2, 4.75f);
    pb::encode_varint_field(&rating, 3, 2100000);

    std::string icon_img;
    pb::encode_varint_field(&icon_img, 1, 4);
    pb::encode_bytes_field(&icon_img, 5, "https://play-lh.googleusercontent.com/signal-icon.png");

    std::string doc;
    pb::encode_bytes_field(&doc, 1, "org.thoughtcrime.securesms");
    pb::encode_bytes_field(&doc, 5, "Signal Private Messenger");
    pb::encode_bytes_field(&doc, 6, "Signal Foundation");
    pb::encode_bytes_field(&doc, 7, "Private encrypted messaging");
    pb::encode_bytes_field(&doc, 10, icon_img);
    pb::encode_bytes_field(&doc, 13, details);
    pb::encode_bytes_field(&doc, 15, rating);

    std::string wrapper;
    pb::encode_bytes_field(&wrapper, 1, doc);

    const auto items = parse_play_fdfe_items_protobuf(wrapper);
    CHECK(items.size() == 1);
    CHECK(items[0].id == "android:org.thoughtcrime.securesms");
    CHECK(items[0].name == "Signal Private Messenger");
    CHECK(items[0].version == "7.24.2");
    CHECK(items[0].rating > 4.7 && items[0].rating < 4.8);
    CHECK(items[0].icon == "https://play-lh.googleusercontent.com/signal-icon.png");
    CHECK(!items[0].permissions.empty());

    // 3. Synthesize a Google Play DeliveryResponse with base APK + split APK
    std::string cookie;
    pb::encode_bytes_field(&cookie, 1, "MarketDA");
    pb::encode_bytes_field(&cookie, 2, "cookie_secret_val");

    std::string split_arm64;
    pb::encode_bytes_field(&split_arm64, 1, "config.arm64_v8a");
    pb::encode_varint_field(&split_arm64, 2, 12345678);
    pb::encode_bytes_field(&split_arm64, 5, "https://play.googleapis.com/download/split-arm64.apk");

    std::string delivery_data;
    pb::encode_varint_field(&delivery_data, 1, 50000000);
    pb::encode_bytes_field(&delivery_data, 3, "https://play.googleapis.com/download/base.apk");
    pb::encode_bytes_field(&delivery_data, 5, cookie);
    pb::encode_bytes_field(&delivery_data, 15, split_arm64);

    std::string deliv_wrap;
    pb::encode_bytes_field(&deliv_wrap, 1, delivery_data);

    const auto bundle = parse_play_delivery_protobuf(deliv_wrap, "org.thoughtcrime.securesms");
    CHECK(bundle.ok);
    CHECK(bundle.base_apk.download_url == "https://play.googleapis.com/download/base.apk");
    CHECK(bundle.cookie_name == "MarketDA");
    CHECK(bundle.splits.size() == 1);
    CHECK(bundle.splits[0].name == "config.arm64_v8a.apk");
    return true;
}

bool test_flathub_and_winget_parsers() {
    using namespace finn::store;
    const std::string fh_json = R"({
      "hits": [
        {
          "app_id": "org.mozilla.firefox",
          "name": "Firefox",
          "summary": "Fast, Private & Safe Web Browser",
          "developer_name": "Mozilla",
          "icon": "https://dl.flathub.org/media/firefox.png",
          "verification_verified": true,
          "verification_website": "mozilla.org"
        }
      ]
    })";
    auto fh = parse_flathub_hits_json(fh_json);
    CHECK(fh.size() == 1);
    CHECK(fh[0].id == "flatpak:org.mozilla.firefox");
    CHECK(fh[0].verified);
    CHECK(fh[0].verified_source == "mozilla.org");

    const std::string appstream = R"({"version":"132.0","description":"<p>Browse freely.</p>","screenshots":[{"sizes":[{"src":"https://dl.flathub.org/shot1.png"}]}]})";
    CHECK(enrich_flathub_appstream_json(appstream, &fh[0]));
    CHECK(fh[0].version == "132.0");
    CHECK(fh[0].screenshots.size() == 1);

    const auto upd = parse_flatpak_updates_output("org.mozilla.firefox\t132.0\ncom.obsproject.Studio\t31.0\n");
    CHECK(upd.size() == 2);

    const std::string wg_yaml =
        "PackageIdentifier: 7zip.7zip\n"
        "PackageVersion: 24.08\n"
        "InstallerType: exe\n"
        "Installers:\n"
        "  - Architecture: x86\n"
        "    InstallerUrl: https://www.7-zip.org/a/7z2408.exe\n"
        "    InstallerSha256: 1111111111111111111111111111111111111111111111111111111111111111\n"
        "  - Architecture: x64\n"
        "    InstallerUrl: https://www.7-zip.org/a/7z2408-x64.exe\n"
        "    InstallerSha256: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n";
    const auto mf = parse_winget_installer_yaml(wg_yaml);
    CHECK(mf.valid);
    CHECK(mf.architecture == "x64");
    CHECK(mf.installer_url == "https://www.7-zip.org/a/7z2408-x64.exe");
    CHECK(mf.sha256.size() == 64);
    return true;
}

bool test_curated_catalog_ecosystems() {
    const auto cat = finn::store::build_curated_catalog();
    int n_native = 0, n_fp = 0, n_and = 0, n_win = 0;
    for (const auto& a : cat) {
        if (a.ecosystem == finn::store::StoreEcosystem::Native) ++n_native;
        else if (a.ecosystem == finn::store::StoreEcosystem::Flatpak) ++n_fp;
        else if (a.ecosystem == finn::store::StoreEcosystem::Android) ++n_and;
        else if (a.ecosystem == finn::store::StoreEcosystem::Windows) ++n_win;
    }
    CHECK(n_native >= 4);
    CHECK(n_fp >= 5);
    CHECK(n_and >= 3);
    CHECK(n_win >= 3);
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_protobuf_and_aurora_play();
    ok &= test_flathub_and_winget_parsers();
    ok &= test_curated_catalog_ecosystems();
    if (!ok) return 1;
    std::puts("test_store: ok");
    return 0;
}
