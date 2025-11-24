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
    
    constexpr std::string_view sv() const {
        return {data, N - 1};
    }
};

template <size_t N>
FixedString(const char (&str)[N]) -> FixedString<N>;

// Helper trait to detect FixedString (must match Encoder.h)
template<typename T>
struct is_fixed_string : std::false_type {};

template<size_t N>
struct is_fixed_string<FixedString<N>> : std::true_type {};

template<typename T>
inline constexpr bool is_fixed_string_v = is_fixed_string<T>::value;

} // namespace logZ