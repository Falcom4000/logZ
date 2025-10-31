#pragma once

#include <string_view>
#include <algorithm>
namespace logZ {

template <size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&str)[N]) {
        std::copy_n(str, N, data);
    }
    
    // 返回一个 constexpr 的 string_view
    constexpr std::string_view sv() const {
        return {data, N - 1};
    }
};

template <size_t N>
FixedString(const char (&str)[N]) -> FixedString<N>;

} // namespace logZ