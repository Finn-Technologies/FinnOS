#pragma once

#include "session/client.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace finn {

/* The search field's two rules, kept apart from the drawing so they can be
 * tested without a display. */

/* The installed apps a query finds, best first: a name that starts with the
 * query, then a word in the name that does, then a name or id that contains
 * it. Matching ignores ASCII case. An empty query finds nothing, and an app
 * that is not installed is never a result: a row the user can press must be
 * one the session can launch. */
std::vector<SessionApp> search_apps(const std::vector<SessionApp>& apps,
                                    std::string_view query);

/* Appends typed text to the query, up to a length the field can show. */
void search_type(std::string& query, std::string_view text);

/* Removes the last character, which in UTF-8 may be several bytes. */
void search_backspace(std::string& query);

}  // namespace finn
