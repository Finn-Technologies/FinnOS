#pragma once

#include "session/client.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace finn {

/* The installed apps a query finds, best first: a name that starts with the
 * query, then a word in the name that does, then a name or id that contains
 * it. Matching ignores ASCII case. An empty query finds nothing, and an app
 * that is not installed is never a result. */
std::vector<SessionApp> search_apps(const std::vector<SessionApp>& apps,
                                    std::string_view query);

void search_type(std::string& query, std::string_view text);
void search_backspace(std::string& query);

}  // namespace finn
