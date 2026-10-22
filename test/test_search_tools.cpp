#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "finetree.h"
#include "search_tools.h"

namespace
{
std::string print_action(state s, const moveseq &moves)
{
    std::ostringstream out;
    for(full_move move : moves)
    {
        out << move.pgn(s, QUEEN_W, pgn_options::SHOW_CAPTURE) << ' ';
        assert(s.apply_move<true>(move));
    }
    return out.str();
}

void compare_searches(state s, bool shared_prefixes = false)
{
    constexpr int limit = 100000;
    auto [info, space] = HC_info::build_HC(s);
    std::set<std::string> expected;
    for(const auto &moves : info.search(space))
    {
        assert(s.can_apply(action::from_moveseq(moves, s)));
        assert(expected.insert(print_action(s, moves)).second);
        assert(expected.size() < limit);
    }

    auto witness = find_first_action(s, search_mode::fine);
    assert(witness.has_value() == !expected.empty());
    if(witness)
    {
        assert(s.can_apply(action::from_moveseq(*witness, s)));
        assert(expected.contains(print_action(s, *witness)));
    }
    assert(count_fine(s, limit) == expected.size());
    assert(count_naive(s, limit) == expected.size());
    assert(count_fine(s, 1) == std::min<std::size_t>(1, expected.size()));
    assert(count_fine(s, 3) == std::min<std::size_t>(3, expected.size()));

    std::ostringstream printed;
    auto *previous = std::cout.rdbuf(printed.rdbuf());
    const auto count = count_fine<true>(s, limit);
    std::cout.rdbuf(previous);
    std::istringstream lines(printed.str());
    std::set<std::string> actual;
    for(std::string line; std::getline(lines, line);)
        assert(actual.insert(line).second);
    assert(count == expected.size());
    assert(actual == expected);

    if(shared_prefixes)
    {
        auto root = fine_node<>::make_root(s);
        std::size_t children = 0;
        for([[maybe_unused]] index_t i : root->search(natural_HC_ordering{}))
            ++children;
        assert(children > 0 && children < expected.size());
    }
}
}

int main()
{
    const char *args[] = {"count", "fine", "3"};
    const auto [mode, limit] = parse_search_args(3, args);
    assert(mode == search_mode::fine && limit == 3);

    compare_searches(state::from_pgn("[Board \"Very Small - Open\"]"));
    compare_searches(state::from_pgn(R"(
[Board "Very Small - Open"]
1. Bb2+ / Nxb2
2. N>>xd3 / (1T1)Bc3+
3. Bb2
)"));
    compare_searches(state::from_pgn(R"(
[Size "4x4"]
[Board "Custom"]
[3k/4/4/K3:0:1:w]
[3k/4/4/K3:1:1:w]
)"), true);

    state stalemate(multiverse(timeline_layout::odd, {
        {0, 1, true, "k7/2Q5/2K5/8/8/8/8/8"}
    }));
    assert(!stalemate.has_phantom_check());
    compare_searches(stalemate);
    state checkmate(multiverse(timeline_layout::odd, {
        {0, 1, true, "k7/1Q6/2K5/8/8/8/8/8"}
    }));
    assert(checkmate.has_phantom_check());
    compare_searches(checkmate);
}
