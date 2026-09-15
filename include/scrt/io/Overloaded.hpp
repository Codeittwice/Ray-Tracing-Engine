#pragma once

namespace scrt::io {

/// Helper for std::visit: aggregates one lambda per variant alternative.
///
/// Used WITHOUT a catch-all lambda everywhere a document variant is visited, so that adding an
/// alternative (a surface type, a source type) that nobody serialises or builds is a compile
/// error at that visit rather than a silently dropped case. Shared here rather than copied so
/// the tripwire is one definition, not several drifting ones.
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace scrt::io
