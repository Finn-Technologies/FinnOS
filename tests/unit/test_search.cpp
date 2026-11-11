#include "shell/search.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<finn::SessionApp> catalogue() {
    return {
        {"settings", "Settings", "settings-app.svg", true},
        {"files", "Finn Files", "files.svg", true},
        {"store", "Finn Store", "store.svg", false},
        {"fonts", "Fonts", "fonts.svg", true},
    };
}

std::vector<std::string> ids(const std::vector<finn::SessionApp>& apps) {
    std::vector<std::string> out;
    for (const auto& app : apps) out.push_back(app.id);
    return out;
}

void test_empty_query_finds_nothing() {
    assert(finn::search_apps(catalogue(), "").empty());
    assert(finn::search_apps(catalogue(), "   ").empty());
}

void test_match_ignores_case_and_outer_spaces() {
    assert(ids(finn::search_apps(catalogue(), "SETT")) ==
           std::vector<std::string>{"settings"});
    assert(ids(finn::search_apps(catalogue(), "  sett ")) ==
           std::vector<std::string>{"settings"});
}

void test_uninstalled_apps_are_never_results() {
    assert(finn::search_apps(catalogue(), "store").empty());
    // "finn" names two apps in the catalogue; only the installed one is found.
    assert(ids(finn::search_apps(catalogue(), "finn")) ==
           std::vector<std::string>{"files"});
}

void test_name_prefix_before_word_prefix_before_substring() {
    // "f": Fonts starts with it, Finn Files starts with it too; by name.
    assert(ids(finn::search_apps(catalogue(), "f")) ==
           (std::vector<std::string>{"files", "fonts"}));
    // "fi": Finn Files by name prefix only.
    assert(ids(finn::search_apps(catalogue(), "fi")) ==
           std::vector<std::string>{"files"});
    // "fo": Fonts starts with it; nothing else contains it.
    assert(ids(finn::search_apps(catalogue(), "fo")) ==
           std::vector<std::string>{"fonts"});
    // "files": a word of Finn Files, not its start.
    assert(ids(finn::search_apps(catalogue(), "files")) ==
           std::vector<std::string>{"files"});
    // "n": no name starts with it; it is inside all three installed names.
    assert(ids(finn::search_apps(catalogue(), "n")) ==
           (std::vector<std::string>{"files", "fonts", "settings"}));
    // "s": Settings starts with it and beats the names that only contain it.
    assert(ids(finn::search_apps(catalogue(), "s")).front() == "settings");
}

void test_no_match() {
    assert(finn::search_apps(catalogue(), "terminal").empty());
}

void test_typing_and_backspace() {
    std::string query;
    finn::search_type(query, " ");
    assert(query.empty());
    finn::search_type(query, "f");
    finn::search_type(query, "i");
    finn::search_type(query, " ");
    assert(query == "fi ");
    finn::search_backspace(query);
    assert(query == "fi");

    // One backspace removes one character, however many bytes it is.
    finn::search_type(query, "\xC3\xA7");          // ç
    finn::search_type(query, "\xF0\x9F\x99\x82");  // a four-byte emoji
    finn::search_backspace(query);
    assert(query == "fi\xC3\xA7");
    finn::search_backspace(query);
    assert(query == "fi");
    finn::search_backspace(query);
    finn::search_backspace(query);
    finn::search_backspace(query);
    assert(query.empty());
}

void test_query_length_is_capped() {
    std::string query;
    for (int i = 0; i < 200; ++i) finn::search_type(query, "a");
    assert(query.size() == 48);
}

}  // namespace

int main() {
    test_empty_query_finds_nothing();
    test_match_ignores_case_and_outer_spaces();
    test_uninstalled_apps_are_never_results();
    test_name_prefix_before_word_prefix_before_substring();
    test_no_match();
    test_typing_and_backspace();
    test_query_length_is_capped();
    std::puts("search: ok");
    return 0;
}
