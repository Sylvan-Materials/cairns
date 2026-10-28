#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <sstream>
#include <array>
#include <stdexcept>
#include <meta>
#include <format>
#include <iostream>
#include <contracts>

namespace cairns{

    // A zero-allocation, compile-time stack container
    template<typename T, size_t Capacity>
    struct fixed_stack : std::array<T, Capacity> {
        size_t current_size = 0;

   constexpr fixed_stack() = default;
    constexpr fixed_stack(fixed_stack&& other) noexcept : _size(other._size) {
        for (size_t i = 0; i < _size; ++i) {
            _data[i] = std::move(other._data[i]);
        }
    };

        constexpr void push(T val) pre(current_size < Capacity) {
            if (current_size < Capacity) {
                (*this)[current_size] = val;
                current_size++;
            }
        }

        template<typename... Args>
        constexpr void emplace(Args&&... args) pre(current_size < Capacity) {
            // Construct the element perfectly in place
            (*this)[current_size] = T(std::forward<Args>(args)...);
            current_size++;
        }
        
        constexpr void pop() {
            if (current_size > 0) {
                current_size--;
            }
        }

        constexpr T& back() { 
            return (*this)[current_size - 1]; 
        }

        constexpr T back() const {
            return (current_size > 0) ? (*this)[current_size - 1] : T{};
        }

        constexpr size_t size() const { return current_size; }
        constexpr bool empty() const { return current_size == 0; }
        constexpr void clear() { current_size = 0; }

        constexpr bool anyOf(const T& val) const {
            for (size_t i = 0; i < current_size; ++i) {
                if ((*this)[i] == val) {
                    return true;
                }
            }
            return false;
        }

    };

}