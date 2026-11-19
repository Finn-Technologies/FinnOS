// Minimal @State/@Binding storage and Combine sink semantics.
//
// These are real, testable primitives.  A state slot owns a value and notifies
// subscribers on change; a binding is a non-owning reference to a slot; a
// current-value subject delivers its value to subscribers and on change.

#ifndef FINN_DARWIN_SWIFTUI_STATE_AND_COMBINE_HPP
#define FINN_DARWIN_SWIFTUI_STATE_AND_COMBINE_HPP

#include "contract.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace finn::darwin::swiftui {

class StateSlot : public std::enable_shared_from_this<StateSlot> {
public:
    explicit StateSlot(std::string value);
    const std::string& value() const;
    void set(std::string value);
    std::size_t subscribe(std::function<void(const std::string&)> sink);
    void unsubscribe(std::size_t token);
    std::size_t subscriber_count() const;

private:
    std::string value_;
    std::vector<std::pair<std::size_t, std::function<void(const std::string&)>>> sinks_;
    std::size_t next_token_ = 1;
};

class Binding {
public:
    explicit Binding(std::shared_ptr<StateSlot> slot);
    std::string get() const;
    void set(std::string value);
    bool valid() const;

private:
    std::shared_ptr<StateSlot> slot_;
};

class CurrentValueSubject {
public:
    explicit CurrentValueSubject(std::string initial);
    const std::string& value() const;
    void send(std::string value);
    std::size_t sink(std::function<void(const std::string&)> fn);

private:
    std::string value_;
    std::vector<std::function<void(const std::string&)>> subscribers_;
};

void register_state_and_binding(SymbolTable* table);
void register_combine(SymbolTable* table);

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_STATE_AND_COMBINE_HPP
