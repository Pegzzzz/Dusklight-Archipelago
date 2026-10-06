#pragma once
// Harness stand-in for Dusklight's logging (borealis): prints to stderr
#include <fmt/format.h>
#include <cstdio>
#include <string_view>
struct HarnessLog {
    const char* name;
    template <class... A> void print(const char* level, fmt::format_string<A...> f, A&&... a) const {
        std::fprintf(stderr, "[%s %s] %s\n", name, level, fmt::format(f, std::forward<A>(a)...).c_str());
    }
    template <class... A> void trace(fmt::format_string<A...> f, A&&... a) const { print("trace", f, std::forward<A>(a)...); }
    template <class... A> void debug(fmt::format_string<A...> f, A&&... a) const { print("debug", f, std::forward<A>(a)...); }
    template <class... A> void info(fmt::format_string<A...> f, A&&... a) const { print("info", f, std::forward<A>(a)...); }
    template <class... A> void warn(fmt::format_string<A...> f, A&&... a) const { print("warn", f, std::forward<A>(a)...); }
    template <class... A> void error(fmt::format_string<A...> f, A&&... a) const { print("error", f, std::forward<A>(a)...); }
    template <class... A> [[noreturn]] void fatal(fmt::format_string<A...> f, A&&... a) const { print("fatal", f, std::forward<A>(a)...); std::abort(); }
};
extern bool StubLogEnabled;
inline constexpr HarnessLog DuskLog{"dusk"};
#define STUB_LOG()
#define STUB_RET(...) return __VA_ARGS__;
