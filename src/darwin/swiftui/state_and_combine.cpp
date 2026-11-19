#include "state_and_combine.hpp"

#include <algorithm>
#include <utility>

namespace finn::darwin::swiftui {

StateSlot::StateSlot(std::string value) : value_(std::move(value)) {}

const std::string& StateSlot::value() const { return value_; }

void StateSlot::set(std::string value) {
    if (value_ == value) return;
    value_ = std::move(value);
    // Copy the sink list: a sink is allowed to subscribe/unsubscribe itself.
    auto sinks = sinks_;
    for (const auto& entry : sinks) entry.second(value_);
}

std::size_t StateSlot::subscribe(std::function<void(const std::string&)> sink) {
    const std::size_t token = next_token_++;
    sinks_.emplace_back(token, std::move(sink));
    return token;
}

void StateSlot::unsubscribe(std::size_t token) {
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(),
                                [token](const auto& e) { return e.first == token; }),
                 sinks_.end());
}

std::size_t StateSlot::subscriber_count() const { return sinks_.size(); }

Binding::Binding(std::shared_ptr<StateSlot> slot) : slot_(std::move(slot)) {}

std::string Binding::get() const { return slot_ ? slot_->value() : std::string(); }

void Binding::set(std::string value) {
    if (slot_) slot_->set(std::move(value));
}

bool Binding::valid() const { return slot_ != nullptr; }

CurrentValueSubject::CurrentValueSubject(std::string initial) : value_(std::move(initial)) {}

const std::string& CurrentValueSubject::value() const { return value_; }

void CurrentValueSubject::send(std::string value) {
    value_ = std::move(value);
    for (const auto& subscriber : subscribers_) subscriber(value_);
}

std::size_t CurrentValueSubject::sink(std::function<void(const std::string&)> fn) {
    subscribers_.push_back(std::move(fn));
    subscribers_.back()(value_);  // current-value subjects replay immediately
    return subscribers_.size();
}

void register_state_and_binding(SymbolTable*) {
    // @State/@Binding are compiler-generated storage; no dynamic symbols are
    // imported by the minimal SwiftUI app.  The real primitives above are the
    // implementation used by the view tree.
}

void register_combine(SymbolTable*) {
    // No Combine symbols are required by the minimal app; CurrentValueSubject
    // above provides real sink semantics for the state bridge.
}

void register_modifiers(SymbolTable*) {
    // The minimal app uses no view modifiers, so no modifier thunks are
    // registered.  None is reported missing by finn-darwin diagnose.
}

void register_containers(SymbolTable*) {
    // VStack/TupleView are provided by views_and_scene.cpp; no extra container
    // symbols are needed for the minimal app.
}

}  // namespace finn::darwin::swiftui
