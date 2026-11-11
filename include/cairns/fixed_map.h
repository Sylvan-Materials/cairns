#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <optional>
#include <stdexcept>

namespace cairns {

namespace detail {

    // Compile-time 64-bit FNV-1a Hash
    template<typename CharT>
    constexpr uint64_t fnv1a_hash_str(std::basic_string_view<CharT> sv) {
        uint64_t hash = 14695981039346656037ULL;
        for (CharT c : sv) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    template<typename K>
    constexpr uint64_t default_hash(const K& key) {
        if constexpr (std::is_same_v<K, std::string_view>) {
            return fnv1a_hash_str(key);
        } else if constexpr (std::is_same_v<K, std::u8string_view>) {
            return fnv1a_hash_str(key);
        } else if constexpr (std::is_integral_v<K>) {
            // SplitMix64 integer hash
            uint64_t x = static_cast<uint64_t>(key);
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        } else {
            static_assert(sizeof(K) == 0, "Unsupported key type for cairns::fixed_map default_hash");
        }
    }

} // namespace detail

template<typename Key, typename Value, size_t Capacity = 1024>
struct fixed_map {
    static_assert(Capacity > 0, "cairns::fixed_map Capacity must be greater than 0");

    struct Entry {
        Key key{};
        Value value{};
        bool occupied = false;
    };

    std::array<Entry, Capacity> buckets{};
    size_t current_size = 0;

    constexpr fixed_map() = default;

    // Insert or overwrite a key-value pair in O(1) average time
    constexpr void insert(const Key& key, const Value& value) {
        uint64_t h = detail::default_hash(key);
        size_t start_idx = static_cast<size_t>(h % Capacity);

        for (size_t step = 0; step < Capacity; ++step) {
            size_t idx = (start_idx + step) % Capacity;
            if (!buckets[idx].occupied) {
                buckets[idx].key = key;
                buckets[idx].value = value;
                buckets[idx].occupied = true;
                current_size++;
                return;
            }
            if (buckets[idx].key == key) {
                buckets[idx].value = value; // Overwrite existing
                return;
            }
        }
        throw std::overflow_error("cairns::fixed_map capacity exceeded on insert");
    }

    // Fast O(1) lookup returning a pointer (nullptr if not found)
    constexpr const Value* find(const Key& key) const {
        if (current_size == 0) return nullptr;

        uint64_t h = detail::default_hash(key);
        size_t start_idx = static_cast<size_t>(h % Capacity);

        for (size_t step = 0; step < Capacity; ++step) {
            size_t idx = (start_idx + step) % Capacity;
            if (!buckets[idx].occupied) {
                return nullptr; // Empty slot means key does not exist
            }
            if (buckets[idx].key == key) {
                return &buckets[idx].value;
            }
        }
        return nullptr;
    }

    constexpr Value* find(const Key& key) {
        return const_cast<Value*>(static_cast<const fixed_map*>(this)->find(key));
    }

    constexpr bool contains(const Key& key) const {
        return find(key) != nullptr;
    }

    // Lookup with fallback default value
    constexpr Value get_or(const Key& key, const Value& default_val) const {
        if (const Value* ptr = find(key)) {
            return *ptr;
        }
        return default_val;
    }

    constexpr size_t size() const { return current_size; }
    constexpr bool empty() const { return current_size == 0; }
    
    constexpr void clear() {
        for (size_t i = 0; i < Capacity; ++i) {
            buckets[i].occupied = false;
        }
        current_size = 0;
    }
};

} // namespace cairns
