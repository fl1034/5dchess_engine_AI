#undef NDEBUG
#include <iostream>
#include <cassert>
#include "hypercuboid.h"
#include "pgnparser.h"
#include "graph.h"
#include <cstdlib>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace hcutils_tests
{
void test_hc()
{
    HC hc {{{1,2}, {2,3}, {1,2,3}}};
    std::cout << hc.to_string() << "\n";
    point a {1,2,3};
    std::cout << hc.contains(a) << "\n";
    search_space ss1 = hc.remove_point(a);
    std::cout << ss1.to_string() << "\n";
    
    std::map<index_t, integer_set> fixed_axes = {{0,{1}},{2,{1,2}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_remove()
{
    HC hc {integer_set{0}, integer_set{2}};
    std::cout << hc.to_string() << "\n";
    std::map<index_t, integer_set> fixed_axes = {{0,{0}}};
    slice sl {fixed_axes};
    search_space ss2 = hc.remove_slice(sl);
    std::cout << ss2.to_string() << "\n";
}

void test_backward_slice_removal()
{
    const slice problem{{{0, {0}}}};

    // The back HC intersects, the preceding HC does not, so the dynamic
    // density rule stops before reaching the intersecting front HC.
    search_space local{
        HC{{0, 1}},
        HC{{2}},
        HC{{0, 3}}
    };
    local.remove_slice_backwards(problem, false);
    assert(local.size() == 3);
    assert(local.contains(point{0}));
    assert(local.contains(point{2}));
    assert(local.contains(point{3}));

    // With the fixed weight of 2, three hits provide enough evidence to cross
    // one miss and reach the intersecting front HC.
    search_space dense_enough{
        HC{{0, 4}},
        HC{{5}},
        HC{{0, 6}},
        HC{{0, 3}},
        HC{{0, 2}}
    };
    dense_enough.remove_slice_backwards(problem, false);
    assert(!dense_enough.contains(point{0}));
    assert(dense_enough.contains(point{2}));
    assert(dense_enough.contains(point{3}));
    assert(dense_enough.contains(point{4}));
    assert(dense_enough.contains(point{5}));
    assert(dense_enough.contains(point{6}));

    // HC search uses a stricter weight of 10, so the same miss stops
    // propagation before the front HC is reached.
    search_space sparse_for_search{
        HC{{0, 4}}, HC{{5}}, HC{{0, 6}}, HC{{0, 3}}, HC{{0, 2}}
    };
    sparse_for_search.remove_slice_backwards(problem, false, 10);
    assert(sparse_for_search.contains(point{0}));
    assert(sparse_for_search.back().contains(point{2}));

    // A slice may only partially intersect an HC on several axes. Backward
    // removal must not introduce slice coordinates that were absent from the
    // original HC.
    search_space rejected_as_too_fragmented{
        HC{{0, 1}, {0, 1}}
    };
    const slice broad_problem{{
        {0, {0, 9}},
        {1, {0, 9}}
    }};
    assert(!rejected_as_too_fragmented.back()
                .is_slice_good(broad_problem));
    assert(rejected_as_too_fragmented.back()
               .is_slice_good(broad_problem, 2));
    rejected_as_too_fragmented.remove_slice_backwards(
        broad_problem, false);
    assert(rejected_as_too_fragmented.size() == 1);
    assert(rejected_as_too_fragmented.contains(point{0, 0}));

    // Raising the limit permits this two-axis cut without introducing
    // coordinates outside the original HC.
    rejected_as_too_fragmented.remove_slice_backwards(
        broad_problem, false, 2, 2);
    assert(!rejected_as_too_fragmented.contains(point{0, 0}));
    assert(rejected_as_too_fragmented.volume() == 3);
    assert(rejected_as_too_fragmented.contains(point{0, 1}));
    assert(rejected_as_too_fragmented.contains(point{1, 0}));
    assert(rejected_as_too_fragmented.contains(point{1, 1}));

    // The originating HC must be cut even when the same operation would be
    // rejected for a secondary HC, or exploration could repeat forever.
    search_space partial{
        HC{{0, 1}, {0, 1}}
    };
    partial.remove_slice_backwards(broad_problem, true);
    assert(!partial.contains(point{0, 0}));
    assert(partial.contains(point{0, 1}));
    assert(partial.contains(point{1, 0}));
    assert(partial.contains(point{1, 1}));
    assert(!partial.contains(point{9, 1}));
    assert(!partial.contains(point{1, 9}));

    // With no intervening miss, all intersecting HCs are pruned.
    search_space dense{
        HC{{0, 1}},
        HC{{0, 2}}
    };
    dense.remove_slice_backwards(problem, false);
    assert(dense.size() == 2);
    assert(!dense.contains(point{0}));
    assert(dense.contains(point{1}));
    assert(dense.contains(point{2}));

}

void test_slice_good()
{
    static_assert(noexcept(std::declval<const HC &>().is_slice_good(
        std::declval<const slice &>())));
    const HC hc{{1, 64, 4096}, {2, 65}};
    const slice whole{{{0, {1, 64, 4096, 8192}}, {1, {2, 65}}}};
    const slice partial{{{0, {64}}, {1, {2}}}};
    const slice disjoint{{{0, {128}}}};
    assert(hc.is_slice_good(whole, 0));
    assert(!hc.is_slice_good(partial, 0));
    assert(!hc.is_slice_good(partial, 1));
    assert(hc.is_slice_good(partial, 2));
    assert(!hc.is_slice_good(disjoint, 2));
    assert(hc.is_slice_good(slice{}, 0));
    assert(hc.volume() == 6);
}

void test_backward_slice_order()
{
    const slice problem{{{0, {0}}, {1, {0}}}};
    const HC h1{{9}, {9}};
    const HC h2{{0, 2}, {0, 2}};
    const HC h3{{3}, {3}};
    const HC h4{{0, 4}, {0, 4}};
    search_space space{h1, h2, h3, h4};
    // Scan across h3 to cut h2 as well. The pieces must retain both their
    // own order and their original HC's position in the list. A limit of
    // two permits both h2 and h4 to produce two pieces.
    space.remove_slice_backwards(problem, true, 0, 2);
    search_space expected{h1};
    expected.concat(h2.remove_slice(problem));
    expected.push_back(h3);
    expected.concat(h4.remove_slice(problem));
    assert(space.to_string() == expected.to_string());
}

void test_search_branch_order()
{
    auto parsed = pgnparser(R"(
[Board "Standard - Turn Zero"]
1. e3 / e6
2. Nf3 / Nc6
)").parse_game();
    assert(parsed);
    const state s(*parsed);
    for(bool stable : {false, true})
    {
        auto [info, space] = HC_info::build_HC(s);
        auto actions = stable ? info.stable_search(std::move(space))
                              : info.search(std::move(space));
        int count = 0;
        for(const auto &moves : actions)
        {
            std::vector<ext_move> extended;
            for(const auto &move : moves)
            {
                extended.emplace_back(move, s);
            }
            const action act = action::from_vector(extended, s);
            assert(act.get_branching_index() == static_cast<int>(moves.size()));
            assert(s.can_apply(act));
            if(++count == 10) break;
        }
        assert(count == 10);
    }
}

int run()
{
    test_hc();
    test_slice_good();
    test_backward_slice_removal();
    test_backward_slice_order();
    test_search_branch_order();
    //test_remove();
//    HC h2 = {{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}}};
//    point b = {20};
//    std::cout << h2.contains(b) << "\n";
//    search_space ss3 = h2.remove_point(b);
//    std::cout << ss3.to_string() << "\n";
    std::cerr << "---= test_hcutils.cpp: all passed =---" << std::endl;
    return 0;
}
} /* namespace hcutils_tests */

namespace graph_tests
{
namespace
{
using adjacency_matrix = std::vector<std::vector<bool>>;
using matching = std::vector<std::pair<index_t, index_t>>;

void require(bool condition)
{
    if(!condition)
    {
        std::abort();
    }
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<bool> &required,
    std::vector<bool> &matched)
{
    std::size_t vertex = 0;
    while(vertex < required.size()
          && (!required[vertex] || matched[vertex]))
    {
        ++vertex;
    }
    if(vertex == required.size())
    {
        return true;
    }

    matched[vertex] = true;
    for(std::size_t other = 0; other < adjacency.size(); ++other)
    {
        if(adjacency[vertex][other] && !matched[other])
        {
            matched[other] = true;
            if(brute_force_match(adjacency, required, matched))
            {
                matched[vertex] = false;
                matched[other] = false;
                return true;
            }
            matched[other] = false;
        }
    }
    matched[vertex] = false;
    return false;
}

bool brute_force_match(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required_vertices)
{
    std::vector<bool> required(adjacency.size(), false);
    for(index_t vertex : required_vertices)
    {
        required[vertex] = true;
    }
    std::vector<bool> matched(adjacency.size(), false);
    return brute_force_match(adjacency, required, matched);
}

void validate_matching(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required,
    const matching &result)
{
    std::vector<bool> matched(adjacency.size(), false);
    for(const auto [u, v] : result)
    {
        require(u < adjacency.size());
        require(v < adjacency.size());
        require(u != v);
        require(adjacency[u][v]);
        require(!matched[u]);
        require(!matched[v]);
        matched[u] = true;
        matched[v] = true;
    }
    for(index_t vertex : required)
    {
        require(matched[vertex]);
    }
}

void compare_with_brute_force(
    const adjacency_matrix &adjacency,
    const std::vector<index_t> &required)
{
    graph tested(static_cast<index_t>(adjacency.size()));
    for(index_t u = 0; u < adjacency.size(); ++u)
    {
        for(index_t v = 0; v < u; ++v)
        {
            if(adjacency[u][v])
            {
                tested.add_edge(u, v);
            }
        }
    }

    const bool expected = brute_force_match(adjacency, required);
    const std::optional<matching> actual = tested.find_matching(required);
    if(actual.has_value() != expected)
    {
        std::cerr << "matching disagreement: expected=" << expected
                  << ", actual=" << actual.has_value() << "\nrequired:";
        for(index_t vertex : required)
        {
            std::cerr << ' ' << vertex;
        }
        std::cerr << "\nedges:\n";
        for(index_t u = 0; u < adjacency.size(); ++u)
        {
            for(index_t v = 0; v < u; ++v)
            {
                if(adjacency[u][v])
                {
                    std::cerr << u << " -- " << v << '\n';
                }
            }
        }
        std::abort();
    }
    if(actual)
    {
        validate_matching(adjacency, required, *actual);
    }
}

adjacency_matrix make_graph(
    std::size_t size,
    std::initializer_list<std::pair<index_t, index_t>> edges)
{
    adjacency_matrix adjacency(size, std::vector<bool>(size, false));
    for(const auto [u, v] : edges)
    {
        adjacency[u][v] = adjacency[v][u] = true;
    }
    return adjacency;
}

void test_existing_examples()
{
    compare_with_brute_force(make_graph(7, {
        {0, 1}, {1, 2}, {3, 4}, {4, 5}, {5, 6}, {6, 0}
    }), {0, 2, 5, 6});

    compare_with_brute_force(make_graph(10, {
        {2, 1}, {3, 2}, {4, 2}, {5, 0}, {5, 1}, {5, 2}, {5, 3},
        {5, 4}, {6, 3}, {7, 0}, {7, 4}, {8, 0}, {8, 3}, {8, 4}
    }), {0, 1, 2, 5, 6, 7, 8});
}

void test_blossom_regression()
{
    compare_with_brute_force(make_graph(10, {
        {3, 0}, {4, 2}, {5, 0}, {5, 1}, {5, 3}, {6, 2}, {6, 5},
        {7, 0}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}, {8, 3},
        {9, 3}, {9, 4}
    }), {0, 1, 2, 3, 4, 5, 6, 7, 9});
}

void test_257_vertices()
{
    adjacency_matrix adjacency(257, std::vector<bool>(257, false));
    adjacency[255][256] = adjacency[256][255] = true;
    compare_with_brute_force(adjacency, {255, 256});
}

void test_random_graphs()
{
    std::mt19937 random(0x5dca55u);
    std::bernoulli_distribution has_edge(0.35);
    std::bernoulli_distribution is_required(0.55);

    for(index_t dimension = 1; dimension <= 10; ++dimension)
    {
        for(int sample = 0; sample < 200; ++sample)
        {
            adjacency_matrix adjacency(
                dimension, std::vector<bool>(dimension, false));
            for(index_t u = 0; u < dimension; ++u)
            {
                for(index_t v = 0; v < u; ++v)
                {
                    adjacency[u][v] = adjacency[v][u] = has_edge(random);
                }
            }

            std::vector<index_t> required;
            for(index_t vertex = 0; vertex < dimension; ++vertex)
            {
                if(is_required(random))
                {
                    required.push_back(vertex);
                }
            }
            compare_with_brute_force(adjacency, required);
        }
    }
}
}

int run()
{
    test_existing_examples();
    test_blossom_regression();
    test_257_vertices();
    test_random_graphs();
    std::cerr << "---= test_graph.cpp: all passed =---\n";
    return 0;
}
} /* namespace graph_tests */

int main()
{
    std::cerr << "Running hcutils tests\n";
    if(const int result = hcutils_tests::run(); result != 0)
        return result;
    std::cerr << "Running graph tests\n";
    if(const int result = graph_tests::run(); result != 0)
        return result;
    return 0;
}
