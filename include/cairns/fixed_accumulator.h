#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <stdexcept>
#include <contracts>

namespace cairns{

template<size_t MaxCapacity = 8192>
struct fixed_accumulator {
    char data_buffer[MaxCapacity]{};
    size_t current_length = 0;

    constexpr fixed_accumulator() = default;

    constexpr void append(std::string_view sv) pre(current_length + sv.size() < MaxCapacity) {
        std::copy_n(sv.data(), sv.length(), data_buffer + current_length);
        current_length += sv.length();
        data_buffer[current_length] = '\0';
    }

    constexpr void append(std::u8string_view u8sv) {//pre(current_length + u8sv.size() < MaxCapacity){
        // contract_assert(const_cast<size_t>(current_length + u8sv.size()) < MaxCapacity);
        if (current_length + u8sv.size() >= MaxCapacity) [[unlikely]] {
            throw "CRITICAL: fixed_accumulator capacity blown during u8string_view emission!";
        }
        for (size_t i = 0; i < u8sv.length(); ++i) {
            // static_cast from char8_t to char is fully permitted in constexpr
            data_buffer[current_length + i] = static_cast<char>(u8sv[i]);
        }
        current_length += u8sv.length();
        data_buffer[current_length] = '\0';
    }

        // Appends a u8 string wrapped in quotes, automatically escaping GBNF conflicts
    constexpr void append_quoted_escaped(std::u8string_view u8sv) pre(current_length + u8sv.size() < MaxCapacity) {

        // 1. Opening structural quote for GBNF literal matching
        data_buffer[current_length++] = '"';

        // 2. Transcoding pass with automatic inline escaping
        for (char8_t code_unit : u8sv) {
            const char c = static_cast<char>(code_unit);

            switch (c) {
                case '"':
                    // Map a literal JSON quote to \" for GBNF terminal matching
                    data_buffer[current_length++] = '\\';
                    data_buffer[current_length++] = '"';
                    break;
                case '\\':
                    // Escape backslashes
                    data_buffer[current_length++] = '\\';
                    data_buffer[current_length++] = '\\';
                    break;
                case '\n':
                    // Flatten real newlines so they don't break the GBNF layout line structure
                    data_buffer[current_length++] = '\\';
                    data_buffer[current_length++] = 'n';
                    break;
                case '\t':
                    data_buffer[current_length++] = '\\';
                    data_buffer[current_length++] = 't';
                    break;
                default:
                    data_buffer[current_length++] = c;
                    break;
            }
        }

        // 3. Closing structural quote for GBNF literal matching
        data_buffer[current_length++] = '"';
    }
    
    constexpr void append_regex_as_quoted_gbnf(std::u8string_view pattern) {
        // 1. Safety check for the maximum potential expansion length
        if (current_length + pattern.size() + 8 >= MaxCapacity) [[unlikely]] {
            throw "CRITICAL: fixed_accumulator capacity blown during GBNF pattern rule emission!";
        }

        // 2. Force the opening JSON string quote token match for GBNF
        data_buffer[current_length++] = '"';
        data_buffer[current_length++] = '\\';
        data_buffer[current_length++] = '"';
        data_buffer[current_length++] = '"';
        data_buffer[current_length++] = ' ';

        append_regex_as_gbnf(pattern);

        // 5. Force the closing JSON string quote token match for GBNF
        data_buffer[current_length++] = ' ';
        data_buffer[current_length++] = '"';
        data_buffer[current_length++] = '\\';
        data_buffer[current_length++] = '"';
        data_buffer[current_length++] = '"';
    }

    constexpr void append_regex_as_gbnf(std::u8string_view pattern) {
        // 1. Safety check for the maximum potential expansion length
        if (current_length + pattern.size() + 8 >= MaxCapacity) [[unlikely]] {
            throw "CRITICAL: fixed_accumulator capacity blown during GBNF pattern rule emission!";
        }

        size_t i = 0;
        // 3. Skip the leading anchor if present
        if (!pattern.empty() && pattern[i] == u8'^') { i++; }

        // Calculate boundary limits to safely discard trailing anchors
        size_t length = pattern.size();
        if (length > 0 && pattern[length - 1] == u8'$') { length--; }

        bool in_brackets = false;
        // 4. Transcoding sweep loop over the char8_t string view
        while (i < length) {
            const char c = static_cast<char>(pattern[i]);

            // 1. Catch the opening of a character class set
            if (c == '[' && (i == 0 || pattern[i - 1] != u8'\\')) {
                in_brackets = true;
                data_buffer[current_length++] = c;
                i++;
                continue;
            }

            // 2. Catch the closing of a character class set (only if it wasn't escaped with a backslash!)
            if (c == ']' && in_brackets && (i == 0 || pattern[i - 1] != u8'\\')) {
                in_brackets = false;
                data_buffer[current_length++] = c;
                i++;
                continue;
            }

            // If inside [a-z] brackets, pass characters through natively without any quote marks!
            if (in_brackets) {
                data_buffer[current_length++] = c;
                i++;
                continue;
            }

            // Intercept and strip out non-capturing indicators (?: ...) -> ( ... )
            if (i + 2 < length && c == '(' && pattern[i+1] == u8'?' && pattern[i+2] == u8':') {
                data_buffer[current_length++] = ' ';
                data_buffer[current_length++] = '(';
                data_buffer[current_length++] = ' ';
                i += 3; 
                continue;
            }
            
            // Handle double-escaped character tokens (like turning \\* into a clean GBNF "*")
            if (c == '\\' && i + 1 < length) {
                data_buffer[current_length++] = ' ';
                data_buffer[current_length++] = '"';
                data_buffer[current_length++] = static_cast<char>(pattern[i+1]);
                data_buffer[current_length++] = '"';
                data_buffer[current_length++] = ' ';
                i += 2;
                continue;
            }

            // If it's a structural quantifier or modifier out in the open (+, *, ?), pass it directly
            if (c == '+' || c == '*' || c == '?' || c == '(' || c == ')') {
                data_buffer[current_length++] = c;
                i++;
                continue;
            }

            // Standard Literal Fallback: Safely wrap loose literal characters (like your leading '_') in string quotes
            data_buffer[current_length++] = ' ';
            data_buffer[current_length++] = '"';
            data_buffer[current_length++] = c;
            data_buffer[current_length++] = '"';
            data_buffer[current_length++] = ' ';
            i++;
        }

    }

    constexpr void append_int(size_t value) {
        if (value == 0) {
            if (current_length >= MaxCapacity) [[unlikely]] throw "Overflow";
            data_buffer[current_length++] = '0';
            return;
        }

        // A 64-bit unsigned int can have at most 20 digits
        char digit_stack[20];
        size_t stack_ptr = 0;

        while (value > 0) {
            digit_stack[stack_ptr++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }

        if (current_length + stack_ptr >= MaxCapacity) [[unlikely]] {
            throw "CRITICAL: fixed_accumulator integer append overflowed bounds!";
        }

        // Unwind the stack backward to write the digits in correct linear order
        while (stack_ptr > 0) {
            data_buffer[current_length++] = digit_stack[--stack_ptr];
        }
    }

    constexpr void append_synthetic_slice_ref(size_t index) {
        // 1. Append the base identifier string token directly
        this->append("property-slice-");
        
        // 2. Stream the unique integer directly into the trailing index slot
        this->append_int(index);
    }

    constexpr void append_pattern_key_ref(size_t index) {
        this->append("pattern-key-");
        this->append_int(index);
    }

    constexpr void append_property_assignment(std::u8string_view raw_key_name) {
        // 1. Open the structural quote
        this->append("\"\\\"");
        
        // 2. Transcode the raw schema key characters cleanly via your existing loop static_cast
        for (char8_t unit : raw_key_name) {
            data_buffer[current_length++] = static_cast<char>(unit);
        }
        
        // 3. Close the structural quote
        this->append("\\\"\" ws \":\" ws ");
    }

    constexpr void push_back(char c) {
        if (current_length + 2 <= MaxCapacity) {
            data_buffer[current_length++] = c;
            data_buffer[current_length] = '\0';
        } else {
            throw std::out_of_range("fixed_accumulator overflow on push_back");
        }
    }

    
    constexpr const char* data() const { return data_buffer; }
    constexpr size_t size() const { return current_length; }
    constexpr std::string_view view() const { return std::string_view(data_buffer, current_length); }
};

}
