#pragma once

#include <cairns/dfs.h>
#include <cairns/fixed_stack.h>
#include <cairns/fixed_string.h>
#include <concepts>
#include <limits>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace cairns {

struct skip_print {};

template <size_t N>
struct print_label {
    fixed_string<N> text;
    consteval print_label(const char (&str)[N]) : text(str) {}
};

template <size_t N>
print_label(const char (&)[N]) -> print_label<N>;

struct print_bool_when {
    bool expected = true;
};

namespace detail {

    template <typename Attr>
    consteval bool has_annotation(std::meta::info mem) noexcept {
        for (auto a : std::meta::annotations_of(mem)) {
            if (std::meta::type_of(a) == ^^Attr) {
                return true;
            }
        }
        return false;
    }

    template <typename Attr>
    consteval Attr get_annotation(std::meta::info mem, Attr fallback = {}) noexcept {
        for (auto a : std::meta::annotations_of(mem)) {
            if (std::meta::type_of(a) == ^^Attr) {
                return std::meta::extract<Attr>(a);
            }
        }
        return fallback;
    }

    template <template <auto...> class TemplateAttr>
    consteval std::meta::info find_annotation_template(std::meta::info mem) noexcept {
        for (auto a : std::meta::annotations_of(mem)) {
            std::meta::info t = std::meta::type_of(a);
            if (std::meta::has_template_arguments(t) &&
                std::meta::template_of(t) == ^^TemplateAttr) {
                return a;
            }
        }
        return std::meta::info{};
    }
    
    template <typename E>
    requires std::is_enum_v<E>
    constexpr std::string_view reflect_enum_name(E value) noexcept {
        template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (value == [:e:]) {
                return std::meta::identifier_of(e);
            }
        }
        return "UNKNOWN";
    }

    constexpr std::string_view trim_enum_prefix(std::string_view name) noexcept {
        // Strip common uppercase enum prefixes like EDGE_KIND_ or JSON_
        size_t pos = name.find('_');
        if (pos != std::string_view::npos && pos + 1 < name.size()) {
            // Check if prefix is all-caps (e.g., EDGE_KIND_FOO -> FOO)
            size_t last_underscore = name.rfind('_');
            if (last_underscore != std::string_view::npos && last_underscore + 1 < name.size()) {
                return name.substr(last_underscore + 1);
            }
        }
        return name;
    }

    template <size_t N, typename T>
    constexpr void format_field_value(fixed_string<N>& out, std::string_view label, const T& val, bool is_top_level = false) noexcept {
        using U = std::remove_cvref_t<T>;

        if constexpr (std::is_same_v<U, bool>) {
            if (val) {
                if (!is_top_level) out += ' ';
                out += label;
            }
        } else if constexpr (std::is_enum_v<U>) {
            // Skip default (0) enumerator unless top-level
            if (static_cast<std::underlying_type_t<U>>(val) != 0 || is_top_level) {
                if (!is_top_level) out += ' ';
                if (!label.empty() && !is_top_level) {
                    out += label;
                    out += '=';
                }
                out += trim_enum_prefix(reflect_enum_name(val));
            }
        } else if constexpr (std::integral<U>) {
            if (val != 0 && val != std::numeric_limits<U>::max()) {
                if (!is_top_level) out += ' ';
                if (!label.empty()) {
                    out += label;
                    out += '=';
                }
                out.append_uint(static_cast<size_t>(val));
            }
        } else if constexpr (std::is_same_v<U, std::string_view> || std::is_same_v<U, std::u8string_view>) {
            if (!val.empty()) {
                if (!is_top_level) out += ' ';
                if (!label.empty()) {
                    out += label;
                    out += '=';
                }
                out += '"';
                out += val;
                out += '"';
            }
        } else if constexpr (std::is_class_v<U>) {
            constexpr auto ctx = std::meta::access_context::current();
            template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^U, ctx))) {
                if constexpr (!has_annotation<cairns::skip_print>(mem)) {
                    constexpr std::string_view mname = []() consteval {
                        if constexpr (has_annotation<cairns::print_label>(mem)) {
                            return get_annotation<cairns::print_label>(mem).text;
                        } else {
                            return std::meta::identifier_of(mem);
                        }
                    }();

                    using MemberType = std::remove_cvref_t<typename[:std::meta::type_of(mem):]>;

                    if constexpr (std::is_same_v<MemberType, bool> && has_annotation<cairns::print_bool_when>(mem)) {
                        constexpr bool expected = get_annotation<cairns::print_bool_when>(mem).expected;
                        if (val.[:mem:] == expected) {
                            if (!is_top_level) out += ' ';
                            if (!expected) out += '!';
                            out += mname;
                        }
                    } else {
                        format_field_value(out, mname, val.[:mem:], false);
                    }
                }
            }
        }
    };

    template <size_t N, typename Tuple, size_t I = 1>
    constexpr void format_tuple_elements(fixed_string<N>& out, const Tuple& tup, std::string_view sep = "") noexcept {
        if constexpr (I < std::tuple_size_v<Tuple>) {
            size_t before = out.size();
            format_field_value(out, "", std::get<I>(tup), true);
            if constexpr (I + 1 < std::tuple_size_v<Tuple>) {
                if (!sep.empty() && out.size() > before) {
                    size_t mark = out.size();
                    out += sep;
                    format_tuple_elements<N, Tuple, I + 1>(out, tup, sep);
                    // Remove trailing separator if subsequent elements emitted nothing
                    if (out.size() == mark + sep.size()) {
                        out.length = mark;
                        out.data[mark] = '\0';
                    }
                    return;
                }
            }
            format_tuple_elements<N, Tuple, I + 1>(out, tup, sep);
        }
    }

} // namespace detail

template <size_t AccumulatorSize = 16384, size_t MaxDepth = 64>
struct graph_printer {
    fixed_string<AccumulatorSize> out{};
    fixed_stack<bool, MaxDepth> is_last_stack{};
    fixed_string<128> pending_edge{};

    constexpr void append_indent() noexcept {
        if (is_last_stack.empty()) return;
        for (size_t i = 0; i + 1 < is_last_stack.size(); ++i) {
            out += is_last_stack[i] ? "    " : "│   ";
        }
        out += is_last_stack.back() ? "└── " : "├── ";
    }

    template <typename EdgeTuple>
    constexpr void format_edge_label(fixed_string<128>& buf, const EdgeTuple& e) noexcept {
        buf.length = 0;
        buf.data[0] = '\0';
        if constexpr (requires { std::tuple_size<EdgeTuple>::value; }) {
            // Elements 0 and 1 are (src, dst); elements 2..N-1 are edge payload
            detail::format_tuple_elements<128, EdgeTuple, 2>(buf, e, "|");
        } else if constexpr (std::is_class_v<EdgeTuple>) {
            constexpr auto ctx = std::meta::access_context::current();
            template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^EdgeTuple, ctx))) {
                constexpr std::string_view mname = std::meta::identifier_of(mem);
                if constexpr (mname != "source" && mname != "from" && mname != "src" && mname != "u" &&
                              mname != "target" && mname != "to" && mname != "dst" && mname != "dest" && mname != "v") {
                    detail::format_field_value(buf, mname, e.[:mem:], false);
                }
            }
        }
    }

    template <typename VertexTuple>
    constexpr void on_enter(size_t u, const VertexTuple& v_data, size_t /*child_count*/) noexcept {
        append_indent();
        if (!pending_edge.empty()) {
            out += "--[";
            out += pending_edge.view();
            out += "]--> ";
            pending_edge.length = 0;
            pending_edge.data[0] = '\0';
        }

        out += "(#";
        out.append_uint(u);
        if constexpr (requires { std::tuple_size<VertexTuple>::value; }) {
            detail::format_tuple_elements<AccumulatorSize, VertexTuple, 1>(out, v_data);
        } else {
            detail::format_field_value(out, "", v_data, false);
        }
        out += ")\n";
    }

    template <typename EdgeTuple>
    constexpr dfs_control on_edge(
        size_t /*u*/, size_t /*v*/, const EdgeTuple& e_data,
        size_t edge_idx, size_t total_edges
    ) noexcept {
        if (is_last_stack.size() < MaxDepth) {
            is_last_stack.push(edge_idx + 1 == total_edges);
        }
        format_edge_label(pending_edge, e_data);
        return dfs_control::continue_dfs;
    }

    template <typename EdgeTuple>
    constexpr void on_visited_edge(size_t /*u*/, size_t v, const EdgeTuple& e_data) noexcept {
        append_indent();
        fixed_string<128> edge_buf{};
        format_edge_label(edge_buf, e_data);
        if (!edge_buf.empty()) {
            out += "--[";
            out += edge_buf.view();
            out += "]--> ";
        }
        out += "↺ (#";
        out.append_uint(v);
        out += ")\n";

        pending_edge.length = 0;
        pending_edge.data[0] = '\0';
        if (!is_last_stack.empty()) {
            is_last_stack.pop();
        }
    }

    template <typename VertexTuple>
    constexpr void on_exit(size_t /*u*/, const VertexTuple& /*v_data*/, size_t /*child_count*/) noexcept {
        if (!is_last_stack.empty()) {
            is_last_stack.pop();
        }
    }
};

template <size_t AccumulatorSize = 16384, typename Vertices, typename Edges>
constexpr auto render_graph(const Vertices& vertices, const Edges& edges, size_t start_vertex = 0) noexcept {
    graph_printer<AccumulatorSize> printer{};
    cairns::dfs(vertices, edges, printer, start_vertex);
    return printer.out;
}

} // namespace cairns
