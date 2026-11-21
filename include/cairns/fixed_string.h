#pragma once
#include <string_view>
#include <contracts>

namespace cairns{
    // Structural fixed_string template matching our performance pipeline
    template<size_t N>
    struct fixed_string {
        char data[N]{};
        std::size_t length = 0;
        constexpr fixed_string() = default;
        consteval fixed_string(const char (&str)[N]) : length(N > 0 ? N - 1 : 0) { std::copy_n(str, N, data); }
        constexpr fixed_string(const char* ptr, size_t len) {
            length = (len < N) ? len : (N - 1);
            for (std::size_t i = 0; i < length; ++i) {
                data[i] = ptr[i];
            }
            data[length] = '\0';
        }
        constexpr std::string_view view() const { return std::string_view(data, length); }

        constexpr void push_back(char c) noexcept
            pre(N > 0 && length + 1uz < N)
            post(length < N && data[length] == '\0')
        {
            data[length++] = c;
            data[length] = '\0';
        }

        constexpr fixed_string& append(std::string_view sv) noexcept
            pre(N > 0 && length < N)
            pre(sv.size() < N - length)
            post(r : r.length < N && r.data[r.length] == '\0')
        {
            for (char c : sv) {
                data[length++] = c;
            }
            data[length] = '\0';
            return *this;
        }

        constexpr fixed_string& append(std::u8string_view sv) noexcept {
            for (char8_t c : sv) {
                if (size() + 1 < N) {
                    push_back(static_cast<char>(c));
                } else {
                    break;
                }
            }
            return *this;
        }

        constexpr fixed_string& append_uint(size_t val) noexcept {
            if (val == 0) {
                return append("0");
            }
            char buf[24]{};
            size_t pos = 0;
            while (val > 0) {
                buf[pos++] = static_cast<char>('0' + (val % 10));
                val /= 10;
            }
            while (pos > 0 && size() + 1 < N) {
                push_back(buf[--pos]);
            }
            return *this;
        }

        template<size_t M>
        constexpr auto operator+(const fixed_string<M>& other) const {
            char merged[N + M - 1]{};
            std::copy_n(data, N - 1, merged);
            std::copy_n(other.data, M - 1, merged + N - 1);
            return fixed_string<N + M - 1>(merged, N + M - 2);
        }

        constexpr fixed_string& operator+=(std::string_view sv) noexcept { return append(sv); }
        constexpr fixed_string& operator+=(std::u8string_view sv) noexcept { return append(sv); }
        constexpr fixed_string& operator+=(char c) noexcept {
            if (size() + 1 < N) push_back(c);
            return *this;
        }
    };

    template<size_t N>
    fixed_string(const char (&)[N]) -> fixed_string<N>;

}
