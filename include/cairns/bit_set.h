#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <bit>
#include <stdexcept>

namespace cairns {

template<size_t MaxBits>
struct bit_set {
    static constexpr size_t bits_per_word = 64;
    static constexpr size_t word_count = (MaxBits + bits_per_word - 1) / bits_per_word;

    std::array<uint64_t, word_count> words{};

    constexpr bit_set() = default;

    // Set a single bit to 1
    constexpr void set(size_t idx) {
        if (idx >= MaxBits) {
            throw std::out_of_range("cairns::bit_set::set index out of bounds");
        }
        words[idx / bits_per_word] |= (uint64_t{1} << (idx % bits_per_word));
    }

    // Clear a single bit to 0
    constexpr void reset(size_t idx) {
        if (idx >= MaxBits) {
            throw std::out_of_range("cairns::bit_set::reset index out of bounds");
        }
        words[idx / bits_per_word] &= ~(uint64_t{1} << (idx % bits_per_word));
    }

    // Clear all bits
    constexpr void clear() {
        words.fill(0);
    }

    // Check if a bit is 1
    constexpr bool test(size_t idx) const {
        if (idx >= MaxBits) {
            return false;
        }
        return (words[idx / bits_per_word] & (uint64_t{1} << (idx % bits_per_word))) != 0;
    }

    constexpr bool operator[](size_t idx) const {
        return test(idx);
    }

    // Set Union (this |= other) — returns true if any new bits were added (useful for fixed-point loops)
    constexpr bool union_with(const bit_set& other) {
        bool changed = false;
        for (size_t i = 0; i < word_count; ++i) {
            uint64_t prev = words[i];
            words[i] |= other.words[i];
            if (words[i] != prev) changed = true;
        }
        return changed;
    }

    // Set Intersection (this &= other)
    constexpr void intersect_with(const bit_set& other) {
        for (size_t i = 0; i < word_count; ++i) {
            words[i] &= other.words[i];
        }
    }

    // Count total active bits using C++20 hardware/constexpr popcount
    constexpr size_t count() const {
        size_t total = 0;
        for (size_t i = 0; i < word_count; ++i) {
            total += static_cast<size_t>(std::popcount(words[i]));
        }
        return total;
    }

    constexpr bool any() const {
        for (size_t i = 0; i < word_count; ++i) {
            if (words[i] != 0) return true;
        }
        return false;
    }

    constexpr bool none() const {
        return !any();
    }
};

} // namespace cairns
