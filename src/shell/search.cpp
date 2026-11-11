#include "search.hpp"

#include <algorithm>
#include <cstddef>

namespace finn {
namespace {

// The field is 300 px wide; past this the text would run under its edge.
constexpr std::size_t kMaxQueryBytes = 48;

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

bool is_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// 0 is the best rank; a negative rank is no match.
int rank(const SessionApp& app, const std::string& needle) {
    const std::string name = lowered(app.name);
    const std::size_t at = name.find(needle);
    if (at == 0) return 0;
    for (std::size_t i = at; i != std::string::npos; i = name.find(needle, i + 1)) {
        if (name[i - 1] == ' ') return 1;
    }
    if (at != std::string::npos) return 2;
    if (lowered(app.id).find(needle) != std::string::npos) return 2;
    return -1;
}

}  // namespace

std::vector<SessionApp> search_apps(const std::vector<SessionApp>& apps,
                                    std::string_view query) {
    const std::size_t first = query.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    const std::size_t last = query.find_last_not_of(' ');
    const std::string needle = lowered(query.substr(first, last - first + 1));

    struct Ranked {
        int rank;
        const SessionApp* app;
    };
    std::vector<Ranked> ranked;
    for (const SessionApp& app : apps) {
        if (!app.installed) continue;
        const int r = rank(app, needle);
        if (r >= 0) ranked.push_back(Ranked{r, &app});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.app->name < b.app->name;
    });

    std::vector<SessionApp> found;
    found.reserve(ranked.size());
    for (const Ranked& entry : ranked) found.push_back(*entry.app);
    return found;
}

void search_type(std::string& query, std::string_view text) {
    if (query.empty() && text.find_first_not_of(' ') == std::string_view::npos) return;
    if (query.size() + text.size() > kMaxQueryBytes) return;
    query += text;
}

void search_backspace(std::string& query) {
    while (!query.empty() && is_continuation(query.back())) query.pop_back();
    if (!query.empty()) query.pop_back();
}

}  // namespace finn
