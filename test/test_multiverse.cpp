#undef NDEBUG
#include <cassert>
#include <type_traits>
#include <utility>
#include "state.h"

static_assert(!std::is_polymorphic_v<multiverse>);
static_assert(std::is_nothrow_move_constructible_v<multiverse>);
static_assert(std::is_nothrow_move_assignable_v<multiverse>);
static_assert(std::is_nothrow_swappable_v<multiverse>);
static_assert(std::is_nothrow_constructible_v<state, multiverse &&>);
static_assert(!std::is_nothrow_constructible_v<state, const multiverse &>);
static_assert(std::is_nothrow_move_constructible_v<state>);
static_assert(std::is_nothrow_swappable_v<state>);

namespace
{
const std::string empty = "8/8/8/8/8/8/8/8";

void test_layout()
{
    const auto odd = timeline_layout::odd();
    const auto even = timeline_layout::even();
    assert(!odd.is_even() && even.is_even());
    assert((odd.initial_range() == std::pair{0, 0}));
    assert((even.initial_range() == std::pair{-1, 0}));
    assert(odd.pretty_l(-1) == "-1");
    assert(odd.pretty_l(0) == "0");
    assert(even.pretty_l(-1) == "-0");
    assert(even.pretty_l(0) == "+0");
    assert(even.pretty_l(-2) == "-1");
    assert(even.pretty_l(1) == "+1");
    for(int l = vec4::L_MIN; l <= vec4::L_MAX; ++l)
    {
        assert(odd.mirror(odd.mirror(l)) == l);
        assert(even.mirror(even.mirror(l)) == l);
    }
}

void test_activation(timeline_layout layout)
{
    std::vector<boards_info_t> initial{{0, 3, false, empty}};
    if(layout.is_even())
        initial.emplace_back(-1, 3, false, empty);
    multiverse m(layout, initial);
    const auto central = layout.initial_range();
    assert(m.get_active_range() == central);
    assert((m.get_present() == turn_t{3, false}));
    assert(m.pretty_lt(vec4(0, 0, 3, central.first))
           == (layout.is_even() ? "(-0T3)" : "(0T3)"));

    m.insert_board(1, 2, true, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{central.first, 1}));
    assert((m.get_present() == turn_t{2, true}));
    m.insert_board(2, 1, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{central.first, 1}));
    // An inactive historical board must not move the present yet.
    assert((m.get_present() == turn_t{2, true}));
    m.insert_board(layout.mirror(1), 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(1), 2}));
    assert((m.get_present() == turn_t{1, false}));
    m.insert_board(3, 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(1), 2}));
    m.insert_board(layout.mirror(2), 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(2), 3}));

    // Construction from the same boards must initialize the same cached ranges.
    const multiverse rebuilt(layout, m.get_boards<true>());
    assert(rebuilt.get_lines_range() == m.get_lines_range());
    assert(rebuilt.get_active_range() == m.get_active_range());
    assert(rebuilt.get_present() == m.get_present());
}

void test_value_ownership()
{
    const multiverse original(timeline_layout::odd(), {{0, 1, false, "k3/4/4/K*3"}}, 4, 4);
    state copied(original, promotion_options::NONE);
    assert(copied.get_board_ptr(0, 1, false) == original.get_board_ptr(0, 1, false));
    state independent = copied;
    const auto original_boards = copied.get_boards();
    assert(independent.apply_move<true>(full_move(vec4(0, 0, 1, 0), vec4(1, 0, 1, 0))));
    assert(copied.get_boards() == original_boards);
    assert((original.get_timeline_end(0) == turn_t{1, false}));
    assert((independent.get_timeline_end(0) == turn_t{1, true}));

    multiverse movable = original;
    const board *shared = movable.get_board_ptr(0, 1, false);
    state moved(std::move(movable), promotion_options::NONE);
    assert(moved.get_boards() == copied.get_boards());
    assert(moved.get_board_ptr(0, 1, false) == shared);
    assert(moved.get_promotion_options() == promotion_options::NONE);

    state other(multiverse(timeline_layout::even(),
                          {{-1, 2, true, empty}, {0, 2, true, empty}}));
    const auto other_boards = other.get_boards();
    swap(moved, other);
    assert((moved.get_board_size() == std::pair{8, 8}));
    assert((moved.get_initial_lines_range() == std::pair{-1, 0}));
    assert((moved.get_present() == turn_t{2, true}));
    assert(moved.get_boards() == other_boards);
    assert((other.get_board_size() == std::pair{4, 4}));
    assert((other.get_initial_lines_range() == std::pair{0, 0}));
    assert(other.get_boards() == original_boards);
    assert(other.get_promotion_options() == promotion_options::NONE);

    state transferred(std::move(other));
    moved = std::move(transferred);
    assert(moved.get_boards() == original_boards);
    assert((moved.get_board_size() == std::pair{4, 4}));
    assert((moved.get_initial_lines_range() == std::pair{0, 0}));
}
} /* anonymous namespace */

int main()
{
    test_layout();
    test_activation(timeline_layout::odd());
    test_activation(timeline_layout::even());
    test_value_ownership();
}
