#include "aurora_play_provider.hpp"
#include "protobuf_wire.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace finn::store {
namespace {

std::string json_extract_string(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t kpos = json.find(needle);
    if (kpos == std::string_view::npos) return {};
    const std::size_t colon = json.find(':', kpos + needle.size());
    if (colon == std::string_view::npos) return {};
    const std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string_view::npos) return {};
    std::string out;
    for (std::size_t i = q1 + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            out.push_back(json[++i]);
        } else if (json[i] == '"') {
            return out;
        } else {
            out.push_back(json[i]);
        }
    }
    return {};
}

std::string format_bytes(std::uint64_t bytes) {
    if (bytes == 0) return {};
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

// Decodes a Google Play FDFE Item / DocV2 message into a StoreAppItem.
// Field tags follow the public Aurora Store / googleplay-api proto schema:
//   1 = id (package name), 5 = title, 6 = creator/developer, 7 = descriptionHtml,
//   10 = image[] (field 1=imageType, 5=imageUrl), 13 = details (field 1=appDetails),
//   15 = aggregateRating (field 2=starRating float, 3=ratingsCount varint)
bool decode_play_doc_v2(std::string_view raw, StoreAppItem* out) {
    std::vector<pb::Field> f;
    if (!pb::decode_message(raw, &f)) return false;

    const std::string pkg = pb::first_string(f, 1);
    const std::string title = pb::first_string(f, 5);
    if (pkg.empty() || title.empty() || pkg.find('.') == std::string::npos) return false;

    StoreAppItem item;
    item.id = "android:" + pkg;
    item.install_ref = pkg;
    item.name = title;
    item.developer = pb::first_string(f, 6);
    item.description = pb::first_string(f, 7);
    item.subtitle = item.developer.empty() ? "Android App" : item.developer;
    item.ecosystem = StoreEcosystem::Android;
    item.category = "Android";
    item.verified = true;
    item.verified_source = "Google Play (Aurora)";
    item.accent_hex = "#10b981";

    // Images (field 10): imageType 4 = app icon, imageType 1 = screenshot
    for (std::string_view img_raw : pb::all_bytes(f, 10)) {
        std::vector<pb::Field> imf;
        if (!pb::decode_message(img_raw, &imf)) continue;
        const std::uint64_t itype = pb::first_varint(imf, 1);
        const std::string url = pb::first_string(imf, 5);
        if (url.empty()) continue;
        if (itype == 4 && item.icon.empty()) item.icon = url;
        else if (itype == 1 && item.screenshots.size() < 6) item.screenshots.push_back(url);
    }
    if (item.icon.empty()) item.icon = "android.svg";

    // Details -> AppDetails (field 13 -> field 1)
    for (std::string_view det_raw : pb::all_bytes(f, 13)) {
        std::vector<pb::Field> df;
        if (!pb::decode_message(det_raw, &df)) continue;
        for (std::string_view ad_raw : pb::all_bytes(df, 1)) {
            std::vector<pb::Field> adf;
            if (!pb::decode_message(ad_raw, &adf)) continue;
            if (item.developer.empty()) item.developer = pb::first_string(adf, 1);
            const std::string ver = pb::first_string(adf, 4);
            if (!ver.empty()) item.version = ver;
            const std::uint64_t inst_sz = pb::first_varint(adf, 9);
            if (inst_sz > 0) item.size_label = format_bytes(inst_sz);
            for (std::string_view perm : pb::all_bytes(adf, 10)) {
                if (item.permissions.size() < 8) item.permissions.emplace_back(perm);
            }
        }
    }

    // AggregateRating (field 15)
    for (std::string_view rat_raw : pb::all_bytes(f, 15)) {
        std::vector<pb::Field> rf;
        if (!pb::decode_message(rat_raw, &rf)) continue;
        const float stars = pb::first_float(rf, 2, 0.0f);
        if (stars > 0.0f && stars <= 5.0f) item.rating = static_cast<double>(stars);
        item.review_count = static_cast<int>(pb::first_varint(rf, 3, 0));
    }

    *out = std::move(item);
    return true;
}

// Recursively walks ResponseWrapper -> Payload -> ListResponse/DetailsResponse -> Item
void collect_items_recursive(std::string_view raw, int depth,
                             std::vector<StoreAppItem>* out) {
    if (depth > 6 || out->size() >= 64) return;
    StoreAppItem cand;
    if (decode_play_doc_v2(raw, &cand)) {
        bool dup = false;
        for (const auto& existing : *out) {
            if (existing.id == cand.id) { dup = true; break; }
        }
        if (!dup) out->push_back(std::move(cand));
    }
    std::vector<pb::Field> fields;
    if (!pb::decode_message(raw, &fields)) return;
    for (const auto& f : fields) {
        if (f.wire_type == pb::WireType::LengthDelimited && f.bytes.size() >= 8) {
            collect_items_recursive(f.bytes, depth + 1, out);
        }
    }
}

void find_delivery_data_recursive(std::string_view raw, int depth,
                                  PlayDeliveryBundle* out) {
    if (depth > 6 || out->ok) return;
    std::vector<pb::Field> f;
    if (!pb::decode_message(raw, &f)) return;

    // AndroidAppDeliveryData: field 1 = downloadSize, 2 = sha1/sha256,
    // 3 = downloadUrl, 5 = downloadAuthCookie[] (1=name, 2=value),
    // 15 = splitDeliveryData[] (1=name, 2=downloadSize, 5=downloadUrl)
    const std::string dl_url = pb::first_string(f, 3);
    if (dl_url.rfind("http://", 0) == 0 || dl_url.rfind("https://", 0) == 0) {
        out->base_apk.name = "base.apk";
        out->base_apk.download_url = dl_url;
        out->base_apk.size_bytes = pb::first_varint(f, 1, 0);
        out->base_apk.sha256_b64 = pb::first_string(f, 2);

        for (std::string_view ck : pb::all_bytes(f, 5)) {
            std::vector<pb::Field> cf;
            if (pb::decode_message(ck, &cf)) {
                out->cookie_name = pb::first_string(cf, 1);
                out->cookie_value = pb::first_string(cf, 2);
                break;
            }
        }
        for (std::string_view sp : pb::all_bytes(f, 15)) {
            std::vector<pb::Field> sf;
            if (!pb::decode_message(sp, &sf)) continue;
            PlaySplitFile split;
            split.name = pb::first_string(sf, 1);
            if (!split.name.empty() && split.name.find(".apk") == std::string::npos) {
                split.name += ".apk";
            }
            split.size_bytes = pb::first_varint(sf, 2, 0);
            split.download_url = pb::first_string(sf, 5);
            split.sha256_b64 = pb::first_string(sf, 4);
            if (!split.download_url.empty()) {
                out->splits.push_back(std::move(split));
            }
        }
        out->ok = true;
        return;
    }

    for (const auto& field : f) {
        if (field.wire_type == pb::WireType::LengthDelimited && field.bytes.size() >= 8) {
            find_delivery_data_recursive(field.bytes, depth + 1, out);
            if (out->ok) return;
        }
    }
}

}  // namespace

std::string build_aurora_device_properties_json(std::string_view abi) {
    const std::string arch = abi.empty() ? "arm64-v8a" : std::string(abi);
    std::ostringstream os;
    os << "{"
       << "\"UserReadableName\":\"FinnOS Desktop (" << arch << ")\","
       << "\"Build.HARDWARE\":\"finnos\","
       << "\"Build.VERSION.SDK_INT\":\"35\","
       << "\"Build.VERSION.RELEASE\":\"15\","
       << "\"Build.BRAND\":\"google\","
       << "\"Build.MANUFACTURER\":\"Google\","
       << "\"Build.MODEL\":\"Pixel 9\","
       << "\"Build.DEVICE\":\"tokay\","
       << "\"Build.PRODUCT\":\"tokay\","
       << "\"Platforms\":\"" << arch << "\","
       << "\"Screen.Density\":\"420\","
       << "\"Screen.Width\":\"1080\","
       << "\"Screen.Height\":\"2400\","
       << "\"Vending.version\":\"84312510\","
       << "\"Vending.versionString\":\"43.1.25-31 [0] [PR] 685144123\","
       << "\"Client\":\"android-google\""
       << "}";
    return os.str();
}

AuroraAuthSession parse_aurora_dispenser_json(std::string_view json) {
    AuroraAuthSession s;
    s.email = json_extract_string(json, "email");
    s.auth_token = json_extract_string(json, "auth");
    if (s.auth_token.empty()) s.auth_token = json_extract_string(json, "authToken");
    s.gsf_id = json_extract_string(json, "gsfId");
    s.device_checkin_consistency_token =
        json_extract_string(json, "deviceCheckInConsistencyToken");
    if (s.auth_token.empty() || s.gsf_id.empty()) {
        s.error = "Aurora dispenser response missing auth token or gsfId";
        return s;
    }
    s.authenticated = true;
    return s;
}

std::vector<std::pair<std::string, std::string>> build_play_fdfe_headers(
    const AuroraAuthSession& session) {
    return {
        {"Authorization", "Bearer " + session.auth_token},
        {"X-DFE-Device-Id", session.gsf_id},
        {"X-DFE-Client-Id", "am-android-google"},
        {"X-DFE-Device-Checkin-Consistency-Token", session.device_checkin_consistency_token},
        {"Accept-Language", session.locale},
        {"User-Agent", "Android-Finsky/43.1.25-31 (api=3,versionCode=84312510,sdk=35,device=tokay,hardware=finnos,product=tokay)"},
    };
}

std::vector<StoreAppItem> parse_play_fdfe_items_protobuf(std::string_view raw_pb) {
    std::vector<StoreAppItem> out;
    collect_items_recursive(raw_pb, 0, &out);
    return out;
}

PlayDeliveryBundle parse_play_delivery_protobuf(std::string_view raw_pb,
                                                const std::string& package_name) {
    PlayDeliveryBundle b;
    b.package_name = package_name;
    find_delivery_data_recursive(raw_pb, 0, &b);
    if (!b.ok) {
        b.error = "Google Play delivery payload did not include a downloadable APK URL";
    }
    return b;
}

}  // namespace finn::store
