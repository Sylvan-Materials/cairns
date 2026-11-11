#pragma once

#include <tuple>
#include <array>
#include <cstddef>
#include <utility>
#include <concepts>
#include <meta> // C++26 Reflection
#include <cairns/fixed_stack.h>
#include <cairns/bit_set.h>

namespace cairns {

enum class dfs_control {
    continue_dfs, // Descend into target vertex normally
    prune_branch, // Skip descending into target vertex, continue siblings
    halt_dfs      // Abort the entire DFS immediately
};

struct DFSState {
    size_t vertex_id;
    size_t current_edge_idx;
};

namespace detail {

    // --- C++26 Reflection Edge Extractors ---
    template<typename Edge>
    consteval auto find_member_by_names(std::initializer_list<std::string_view> names) {
        constexpr auto ctx = std::meta::access_context::current();
        for (auto mem : std::meta::nonstatic_data_members_of(^^Edge, ctx)) {
            std::string_view id = std::meta::identifier_of(mem);
            for (auto target_name : names) {
                if (id == target_name) return mem;
            }
        }
        return std::meta::info{};
    }

    template<typename Edge>
    constexpr size_t get_edge_source(const Edge& e) {
        if constexpr (requires { std::get<0>(e); }) {
            return static_cast<size_t>(std::get<0>(e));
        } else {
            constexpr auto mem = find_member_by_names<Edge>({"source", "from", "src", "u", "parent_id"});
            if constexpr (mem != std::meta::info{}) {
                return static_cast<size_t>(e.[:mem:]);
            } else {
                // Fallback to 1st reflected data member
                constexpr auto members = std::meta::nonstatic_data_members_of(^^Edge, std::meta::access_context::current());
                static_assert(members.size() >= 2, "Edge struct must have at least 2 members");
                return static_cast<size_t>(e.[:members[0]:]);
            }
        }
    }

    template<typename Edge>
    constexpr size_t get_edge_target(const Edge& e) {
        if constexpr (requires { std::get<1>(e); }) {
            return static_cast<size_t>(std::get<1>(e));
        } else {
            constexpr auto mem = find_member_by_names<Edge>({"target", "to", "dst", "dest", "v", "child_id", "id"});
            if constexpr (mem != std::meta::info{}) {
                return static_cast<size_t>(e.[:mem:]);
            } else {
                // Fallback to 2nd reflected data member
                constexpr auto members = std::meta::nonstatic_data_members_of(^^Edge, std::meta::access_context::current());
                static_assert(members.size() >= 2, "Edge struct must have at least 2 members");
                return static_cast<size_t>(e.[:members[1]:]);
            }
        }
    }

    // --- Flexible Visitor Hook Dispatchers ---
    template<typename Visitor, typename Vertex>
    constexpr void invoke_on_enter(Visitor& vis, size_t u, const Vertex& v_data, size_t child_count) {
        if constexpr (requires { vis.on_enter(u, v_data, child_count); }) {
            vis.on_enter(u, v_data, child_count);
        } else if constexpr (requires { vis.on_enter(u, v_data); }) {
            vis.on_enter(u, v_data);
        }
    }

    template<typename Visitor, typename Edge>
    constexpr dfs_control invoke_on_edge(Visitor& vis, size_t u, size_t v, const Edge& e_data, size_t edge_idx, size_t total_edges) {
        if constexpr (requires { { vis.on_edge(u, v, e_data, edge_idx, total_edges) } -> std::same_as<dfs_control>; }) {
            return vis.on_edge(u, v, e_data, edge_idx, total_edges);
        } else if constexpr (requires { { vis.on_edge(u, v, e_data) } -> std::same_as<dfs_control>; }) {
            return vis.on_edge(u, v, e_data);
        } else if constexpr (requires { { vis.on_edge(u, v, e_data) } -> std::convertible_to<bool>; }) {
            return vis.on_edge(u, v, e_data) ? dfs_control::continue_dfs : dfs_control::prune_branch;
        } else if constexpr (requires { vis.on_edge(u, v, e_data); }) {
            vis.on_edge(u, v, e_data);
            return dfs_control::continue_dfs;
        }
        return dfs_control::continue_dfs;
    }

    template<typename Visitor, typename Edge>
    constexpr void invoke_on_visited_edge(Visitor& vis, size_t u, size_t v, const Edge& e_data) {
        if constexpr (requires { vis.on_visited_edge(u, v, e_data); }) {
            vis.on_visited_edge(u, v, e_data);
        }
    }

    template<typename Visitor, typename Vertex>
    constexpr void invoke_on_exit(Visitor& vis, size_t u, const Vertex& v_data, size_t child_count) {
        if constexpr (requires { vis.on_exit(u, v_data, child_count); }) {
            vis.on_exit(u, v_data, child_count);
        } else if constexpr (requires { vis.on_exit(u, v_data); }) {
            vis.on_exit(u, v_data);
        }
    }

    // --- The Extended CPO Functor ---
    struct dfs_fn {
        template<
            typename CPOType, size_t MaxNodes,
            typename CPOEdgeType, size_t MaxEdges,
            typename Visitor
        >
        constexpr void operator()(
            const fixed_stack<CPOType, MaxNodes>& vertices,
            const fixed_stack<CPOEdgeType, MaxEdges>& edges,
            Visitor&& visitor,
            size_t start_vertex = 0
        ) const {
            if (vertices.empty() || start_vertex >= vertices.size()) return;

            // 1. O(E) Prefix-Sum Edge Indexing using Reflected Extractors
            std::array<size_t, MaxNodes> child_counts{};
            std::array<size_t, MaxNodes> edge_offsets{};
            std::array<size_t, MaxNodes> edge_start_idx{};
            std::array<size_t, MaxEdges> sorted_edge_indices{};

            for (size_t i = 0; i < edges.size(); ++i) {
                size_t src = get_edge_source(edges[i]);
                if (src < MaxNodes) child_counts[src]++;
            }

            size_t running_sum = 0;
            for (size_t i = 0; i < vertices.size(); ++i) {
                edge_offsets[i] = running_sum;
                edge_start_idx[i] = running_sum;
                running_sum += child_counts[i];
            }

            for (size_t i = 0; i < edges.size(); ++i) {
                size_t src = get_edge_source(edges[i]);
                if (src < MaxNodes) {
                    sorted_edge_indices[edge_offsets[src]++] = i;
                }
            }

            // 2. Non-Recursive Iterative DFS Traversal
            fixed_stack<DFSState, 128> traversal_stack;
            bit_set<MaxNodes> visited;

            traversal_stack.push(DFSState{start_vertex, 0});
            visited[start_vertex] = true;

            while (!traversal_stack.empty()) {
                auto& state = traversal_stack.back();
                size_t u = state.vertex_id;
                const auto& u_data = vertices[u];
                size_t total_children = child_counts[u];

                // Step A: Entering Node
                if (state.current_edge_idx == 0) {
                    invoke_on_enter(visitor, u, u_data, total_children);
                }

                // Step B: Traverse Outgoing Edges
                if (state.current_edge_idx < total_children) {
                    size_t current_idx = state.current_edge_idx;
                    size_t edge_pos = sorted_edge_indices[edge_start_idx[u] + current_idx];
                    const auto& edge_data = edges[edge_pos];
                    size_t v = get_edge_target(edge_data);

                    state.current_edge_idx++;

                    dfs_control ctrl = invoke_on_edge(visitor, u, v, edge_data, current_idx, total_children);

                    if (ctrl == dfs_control::halt_dfs) {
                        return; // Immediate early exit
                    }

                    if (ctrl == dfs_control::continue_dfs && v < vertices.size()) {
                        if (!visited[v]) {
                            visited[v] = true;
                            traversal_stack.push(DFSState{v, 0});
                        } else {
                            // Notify visitor of cycle / DAG diamond convergence
                            invoke_on_visited_edge(visitor, u, v, edge_data);
                        }
                    }
                } 
                // Step C: Exiting Node
                else {
                    invoke_on_exit(visitor, u, u_data, total_children);
                    traversal_stack.pop();
                }
            }
        }
    };

} // namespace detail

inline constexpr detail::dfs_fn dfs{};

} // namespace cairns
