#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <sstream>

namespace cairns{
    // Structural fixed_string template matching our performance pipeline
    template<size_t N>
    struct fixed_string {
        char data[N]{};
        std::size_t length = 0;
        consteval fixed_string(const char (&str)[N]) { std::copy_n(str, N, data); }
        constexpr fixed_string(const char* ptr, size_t len) {
            length = (len < N) ? len : (N - 1);
            for (std::size_t i = 0; i < length; ++i) {
                data[i] = ptr[i];
            }
            data[len] = '\0';
        }
        constexpr std::string_view view() const { return std::string_view(data, length); }

        template<size_t M>
        constexpr auto operator+(const fixed_string<M>& other) const {
            char merged[N + M - 1]{};
            std::copy_n(data, N - 1, merged);
            std::copy_n(other.data, M - 1, merged + N - 1);
            return fixed_string<N + M - 1>(merged, N + M - 2);
        }
    };

    template<size_t N>
    fixed_string(const char (&)[N]) -> fixed_string<N>;

}
