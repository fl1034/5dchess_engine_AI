#undef NDEBUG
#include <cassert>
#include <type_traits>
#include <utility>
#include "state.h"
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include "gametree.h"
#include "hypercuboid.h"
#include "pgnparser.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>
#include "core/hash.h"
#include "game.h"

namespace multiverse_tests
{
static_assert(!std::is_polymorphic_v<multiverse>);
static_assert(std::is_same_v<decltype(std::declval<const multiverse &>().get_board(0, 1, false)),
                             std::shared_ptr<const board>>);
static_assert(std::is_same_v<decltype(std::declval<const state &>().get_board(0, 1, false)),
                             std::shared_ptr<const board>>);

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

int run()
{
    test_layout();
    test_activation(timeline_layout::odd());
    test_activation(timeline_layout::even());
    test_value_ownership();
    return 0;
}
} /* namespace multiverse_tests */

namespace gametree_tests
{
namespace {

struct counted_info
{
    int value;
    inline static int copies = 0;

    explicit counted_info(int value) : value(value) {}
    counted_info(const counted_info &other) : value(other.value) { ++copies; }
    counted_info(counted_info &&) = default;
};

void test_states_and_attachment(const state &initial, const action &act)
{
    using node_t = gnode<std::string>;
    auto root = node_t::create_root(initial, "root");
    auto *child = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "child"));
    auto expected = initial.can_apply(act);
    assert(expected);
    assert(child->get_state().get_boards() == expected->get_boards());
    assert(child->get_stored_state().get_boards() == expected->get_boards());

    // The black king cannot move onto its own pawn on e7.
    const auto invalid_act = action::from_moveseq(
        {full_move(vec4{4, 7, 1, 0}, vec4{4, 6, 1, 0})}, *expected);
    assert(!expected->can_apply(invalid_act));
    auto *invalid = child->add_child(node_t::create_child(child, std::nullopt, invalid_act, "invalid"));
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        bool rejected = false;
        try { invalid->get_state(); }
        catch(const std::runtime_error &error)
        {
            rejected = std::string(error.what()).find("Stored action is invalid") != std::string::npos;
        }
        assert(rejected);
        bool uncached = false;
        try { invalid->get_stored_state(); }
        catch(const std::bad_optional_access &) { uncached = true; }
        assert(uncached);
    }

    const auto child_count = root->get_children().size();
    bool null_rejected = false;
    try { root->add_child(nullptr); }
    catch(const std::invalid_argument &) { null_rejected = true; }
    assert(null_rejected);
    auto other_root = node_t::create_root(initial, "other");
    bool parent_rejected = false;
    try { root->add_child(node_t::create_child(other_root.get(), std::nullopt, act, "wrong parent")); }
    catch(const std::invalid_argument &) { parent_rejected = true; }
    assert(parent_rejected);
    assert(root->get_children().size() == child_count);

    auto copy = root->clone();
    auto *copied_child = copy->get_children().front().get();
    assert(copied_child != child && copied_child->get_parent() == copy.get());
    assert(copied_child->get_children().front()->get_parent() == copied_child);
    assert(copied_child->get_state().get_boards() == expected->get_boards());
    copied_child->set_info("changed");
    assert(child->get_info() == "child");
}

void test_child_index(const state &initial, const action &act, const action &other)
{
    using node_t = gnode<std::string>;
    auto root = node_t::create_root(initial, "root");
    assert(!root->find_child(act));
    auto *first = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "first"));
    assert(!root->add_child(node_t::create_child(root.get(), std::nullopt, act, "duplicate")));
    assert(root->get_children().size() == 1);
    assert(first->get_info() == "first");
    auto *second = root->add_child(node_t::create_child(root.get(), std::nullopt, other, "second"));
    assert(root->find_child(act) == first);
    assert(std::as_const(*root).find_child(act) == first);
    auto copy = root->clone();
    assert(copy->find_child(act) == copy->get_children().front().get());
    assert(copy->find_child(act) != first);
    assert(root->delete_child(act));
    assert(!root->find_child(act));
    assert(root->find_child(other) == second);
    assert(root->get_children().front().get() == second);
    assert(!root->delete_child(act));
    auto *replacement = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "replacement"));
    assert(root->find_child(act) == replacement);
    assert(copy->find_child(act)->get_info() == "first");
    copy.reset();
    assert(root->find_child(act) == replacement);
}

void test_owned_payload_and_formatting(const state &initial, const action &act, const action &other)
{
    auto owned = gnode<std::unique_ptr<int>>::create_root(initial, std::make_unique<int>(7));
    assert(*owned->get_info() == 7);
    using node_t = gnode<counted_info>;
    auto root = node_t::create_root(initial, counted_info{1});
    auto *child = root->add_child(node_t::create_child(root.get(), std::nullopt, act, counted_info{2}));
    root->add_child(node_t::create_child(root.get(), std::nullopt, other, counted_info{3}));
    const std::function<std::string(const counted_info &)> show =
        [marker = counted_info{0}](const counted_info &info) {
            return "{" + std::to_string(info.value + marker.value) + "}";
        };
    counted_info::copies = 0;
    const auto tree = root->pgn_tree(show, pgn_options::SHOW_NOTHING);
    const auto path = child->pgn_path(show, pgn_options::SHOW_NOTHING);
    assert(tree.find("{1}") != std::string::npos);
    assert(tree.find("{2}") != std::string::npos);
    assert(tree.find("{3}") != std::string::npos);
    assert(path.find("{2}") != std::string::npos);
    assert(counted_info::copies == 0);
}

} /* anonymous namespace */

int run() try
{
    state initial(*pgnparser("[Variant \"Standard\"]").parse_game());
    auto [info, space] = HC_info::build_HC(initial);
    std::vector<action> actions;
    for(const auto &moves : info.search(std::move(space)))
    {
        actions.push_back(action::from_moveseq(moves, initial));
        if(actions.size() == 2) break;
    }
    assert(actions.size() == 2);
    test_states_and_attachment(initial, actions[0]);
    test_owned_payload_and_formatting(initial, actions[0], actions[1]);
    test_child_index(initial, actions[0], actions[1]);
    return 0;
}

catch(const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
} /* namespace gametree_tests */

namespace hash_tests
{
namespace
{
void test_boards()
{
    const board empty("8/8/8/8/8/8/8/8");
    const board copy = empty;
    // Version-1 reference value, evaluated independently with integer
    // arithmetic, fixes the output across platforms and future builds.
    assert(hash_board(empty) == UINT64_C(0x3eda1467921a3b26));
    assert(hash_board(empty) == hash_board(copy));
    assert(hash_board(empty) == hash_board(board("8/8/8/8/8/8/8/8")));

    const board unmoved("8/8/8/8/8/8/8/R*K*P*W*4");
    const board moved("8/8/8/8/8/8/8/RKPW4");
    assert(unmoved.get_fen() == moved.get_fen());
    assert(hash_board(unmoved) != hash_board(moved));

    // Exercise every piece family, color, and square independently. This
    // catches omitted masks and field/coordinate combinations that collapse.
    const std::vector<piece_t> pieces{
        WALL_PIECE, KING_W, COMMON_KING_W, ROYAL_QUEEN_W,
        QUEEN_W, PRINCESS_W, ROOK_W, BISHOP_W,
        UNICORN_W, DRAGON_W, KNIGHT_W, PAWN_W, BRAWN_W,
        KING_B, COMMON_KING_B, ROYAL_QUEEN_B,
        QUEEN_B, PRINCESS_B, ROOK_B, BISHOP_B,
        UNICORN_B, DRAGON_B, KNIGHT_B, PAWN_B, BRAWN_B
    };
    std::set<std::uint64_t> hashes{hash_board(empty)};
    for(piece_t piece : pieces)
    {
        for(int square = 0; square < BOARD_SIZE; ++square)
        {
            board b = empty;
            b.set_piece(square, piece);
            assert(hashes.insert(hash_board(b)).second);
        }
    }

    board mutable_board = empty;
    const auto before = hash_board(mutable_board);
    mutable_board.set_piece(ppos(0, 0), QUEEN_W);
    assert(hash_board(mutable_board) != before);
    mutable_board.set_piece(ppos(0, 0), NO_PIECE);
    assert(hash_board(mutable_board) == before);
}

void test_multiverses()
{
    const std::string empty = "8/8/8/8/8/8/8/8";
    std::vector<boards_info_t> boards{
        {-1, 2, false, empty}, {-1, 2, true, empty},
        {0, 1, true, empty}, {0, 2, false, empty},
        {1, 2, false, empty}
    };
    multiverse original(timeline_layout::odd(), boards);
    const auto original_hash = hash_multiverse(original);
    assert(hash_multiverse(multiverse(original)) == original_hash);
    std::reverse(boards.begin(), boards.end());
    const multiverse reordered(timeline_layout::odd(), boards);
    assert(hash_multiverse(reordered) == original_hash);

    // Equal endpoint boards do not make different histories duplicates.
    auto changed_boards = boards;
    for(auto &[line, time, color, fen] : changed_boards)
        if(line == 0 && time == 1 && color)
            fen = "8/8/8/8/8/8/8/R7";
    const multiverse changed(timeline_layout::odd(), changed_boards);
    assert(hash_board(*changed.get_board(0, 2, false)) ==
           hash_board(*reordered.get_board(0, 2, false)));
    assert(hash_multiverse(changed) != original_hash);
    assert(hash_multiverse(original) == original_hash);

    const multiverse shifted(timeline_layout::odd(), {{0, 3, false, empty}});
    const multiverse earlier(timeline_layout::odd(), {{0, 2, false, empty}});
    const multiverse other_color(timeline_layout::odd(), {{0, 2, true, empty}});
    assert(hash_multiverse(shifted) != hash_multiverse(earlier));
    assert(hash_multiverse(other_color) != hash_multiverse(earlier));

    const multiverse odd(timeline_layout::odd(), boards);
    const multiverse even(timeline_layout::even(), boards);
    assert(hash_multiverse(odd) != hash_multiverse(even));

    // A 4x4 board and an 8x8 board with matching walls have equal contents,
    // irrespective of nominal dimensions.
    const multiverse small(timeline_layout::odd(), {{0, 1, false, "3k/4/4/K3"}}, 4, 4);
    const multiverse square(timeline_layout::odd(),
        {{0, 1, false, "0/0/0/0/3k/4/4/K3"}});
    assert(hash_board(*square.get_board(0, 1, false)) ==
           hash_board(*small.get_board(0, 1, false)));
    assert(hash_multiverse(square) == hash_multiverse(small));
    assert(hash_state(state(square)) == hash_state(state(small)));

    auto extended = earlier;
    extended.append_board(0, std::make_shared<board>(empty));
    assert(hash_multiverse(extended) != hash_multiverse(earlier));

    multiverse inactive(timeline_layout::odd(), {{0, 2, false, empty},
                             {1, 2, false, empty},
                             {2, 2, false, empty}});
    assert(inactive.get_active_range().second == 1);
    const auto inactive_hash = hash_multiverse(inactive);
    const multiverse changed_inactive(timeline_layout::odd(), {
        {0, 2, false, empty}, {1, 2, false, empty},
        {2, 2, false, "7r/8/8/8/8/8/8/8"}
    });
    assert(hash_multiverse(changed_inactive) != inactive_hash);
}

void test_states()
{
    game original = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6");
    game annotated = game::from_pgn(R"(
[Event "Another event"]
[Result "0-1"]
[Variant "Standard - Turn Zero"]
1. Ng1f3 / Ng8f6 {recorded:black wins}
)");
    const state &position = original.get_current_state();
    assert(hash_state(position) == hash_state(annotated.get_current_state()));
    assert(hash_state(position) == hash_state(state(position)));

    multiverse universe(timeline_layout::odd(), {{0, 1, false, "8/8/8/8/8/8/8/8"}});
    const state queen(universe, promotion_options::QUEEN);
    const state knight(universe, promotion_options::KNIGHT);
    assert(hash_state(queen) != hash_state(knight));

    // Before and after submission, the multiverse is identical but the stored
    // present/player changes. Rebuilding before submission uses the apparent
    // present rather than the inherited one, which must affect the state hash.
    const auto source = position.get_board(0, 2, false);
    const auto source_hash = hash_board(*source);
    state temporary = position;
    assert(temporary.apply_move(full_move("(0T2)e2e4")));
    game submitted = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6\n2. e4");
    const state &rebuilt = submitted.get_current_state();
    assert(temporary.get_present() != rebuilt.get_present());
    assert(hash_state(temporary) != hash_state(rebuilt));
    const auto before_submit = hash_state(temporary);
    const auto boards_before_submit = temporary.show_fen();
    assert(temporary.submit());
    assert(temporary.show_fen() == boards_before_submit);
    assert(hash_state(temporary) != before_submit);
    assert(hash_state(temporary) == hash_state(rebuilt));

    // Moves append replacement boards while copied states keep shared history.
    assert(temporary.get_board(0, 2, false) == source);
    assert(hash_board(*source) == source_hash);
    const auto edited = source->replace_piece(ppos(0, 0), QUEEN_W);
    assert(edited != source);
    assert(hash_board(*edited) != source_hash);
    assert(hash_board(*position.get_board(0, 2, false)) == source_hash);

    // Phantom states retain their stored present despite advancing the boards.
    const state phantom = queen.phantom();
    multiverse phantom_universe = universe;
    phantom_universe.append_board(0, queen.get_board(0, 1, false));
    const state apparent(phantom_universe, promotion_options::QUEEN);
    assert(phantom.show_fen() == apparent.show_fen());
    assert(phantom.get_present() != apparent.get_present());
    assert(hash_state(phantom) != hash_state(apparent));
}
} /* anonymous namespace */

int run()
{
    test_boards();
    test_multiverses();
    test_states();
    return 0;
}
} /* namespace hash_tests */

namespace preview_tests
{
int run()
{
    game g = game::from_pgn("[Variant \"Standard - Turn Zero\"]");
    const auto [t, c] = g.get_current_present();
    assert(!c);
    const ext_move first(vec4(4, 1, t, 0), vec4(4, 3, t, 0), NO_PIECE);
    const ext_move second(vec4(3, 1, t, 0), vec4(3, 3, t, 0), NO_PIECE);
    const ext_move invalid(vec4(4, 1, t, 0), vec4(4, 6, t, 0), NO_PIECE);
    const state base = g.get_current_state();
    const auto pgn = g.show_pgn();
    const auto legal = g.gen_move_if_playable(first.get_from());
    const bool submit = g.can_submit();
    assert(g.is_playable(first.get_from()));
    assert(!legal.empty());
    assert(!submit);

    assert(g.preview_move(first));
    const auto expected = base.can_apply(first.fm, first.promote_to);
    assert(expected);
    assert(g.get_current_boards() == expected->get_boards());
    assert(g.get_current_boards() != base.get_boards());
    assert(g.get_cached_moves().empty());
    assert(!g.can_undo() && !g.can_redo());
    assert(expected->can_submit().has_value());
    assert(g.can_submit() == submit);
    // Position queries see the preview, while submission still sees the real draft.
    assert(!g.is_playable(first.get_from()));
    assert(g.gen_move_if_playable(first.get_from()).empty());
    assert(g.get_movable_pieces() == expected->get_movable_pieces());
    assert(g.get_current_present() == expected->get_present());
    assert(g.get_current_timeline_status() == expected->get_timeline_status());
    assert(g.show_pgn() == pgn);
    // Replacement calculates from the real position, not the previous preview.
    assert(g.preview_move(second));
    assert(g.get_current_boards() == base.can_apply(second.fm, second.promote_to)->get_boards());
    assert(!g.preview_move(invalid));
    assert(g.get_current_boards() == base.get_boards());
    assert(g.is_playable(first.get_from()));
    assert(g.gen_move_if_playable(first.get_from()) == legal);
    assert(g.preview_move(first));
    g.discard_preview();
    g.discard_preview();
    assert(g.get_current_boards() == base.get_boards());
    assert(g.gen_move_if_playable(first.get_from()) == legal);

    // Applying a different move discards the preview and starts from the real draft.
    assert(g.preview_move(first));
    assert(g.apply_move(second));
    assert(g.get_current_boards() == base.can_apply(second.fm, second.promote_to)->get_boards());
    assert(g.get_cached_moves() == std::vector<ext_move>{second});
    assert(g.undo());
    assert(g.get_current_boards() == base.get_boards());

    assert(g.apply_move(first));
    const auto applied = g.get_current_boards();
    assert(g.undo());
    assert(g.can_redo());
    assert(g.preview_move(second));
    g.discard_preview();
    assert(g.can_redo());
    assert(g.redo());
    assert(g.get_current_boards() == applied);
    assert(g.undo());
    assert(g.preview_move(second));
    assert(g.redo()); // Redo discards the preview and restores the original continuation.
    assert(g.get_current_boards() == applied);
    assert(g.undo());

    assert(g.preview_move(first));
    assert(!g.undo()); // Failed operations also discard previews.
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    assert(!g.apply_move(invalid));
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    g.visit_parent(); // No parent at the root.
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    assert(g.submit() == submit);
    assert(g.get_current_boards() == base.get_boards());

    assert(g.preview_move(first));
    assert(g.apply_move(first));
    assert(g.get_current_boards() == applied);
    assert(g.get_cached_moves().size() == 1);
    assert(g.submit());
    const action continuation = std::get<0>(g.get_historical_actions().back());
    const auto [black_t, black_c] = g.get_current_present();
    assert(black_c);
    const ext_move black(vec4(4, 6, black_t, 0), vec4(4, 4, black_t, 0), NO_PIECE);
    assert(g.preview_move(black));
    g.visit_parent();
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(second));
    assert(g.visit_child(continuation));
    assert(g.get_current_boards() == applied);

    game branching = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6\n2. Nc3 / Nc6");
    const ext_move travel(vec4(5, 2, 3, 0), vec4(5, 4, 2, 0), NO_PIECE);
    const auto before_branch = branching.get_current_boards();
    assert(branching.preview_move(travel));
    const auto branch_preview = branching.get_current_boards();
    assert(branch_preview.size() > before_branch.size());
    branching.discard_preview();
    assert(branching.get_current_boards() == before_branch);
    assert(branching.apply_move(travel));
    assert(branching.get_current_boards() == branch_preview);

    game promotion = game::from_pgn("[Timeline \"odd\"]\n[Size \"8x8\"]\n[7k/P7/8/8/8/8/8/K7:0:1:w]");
    const ext_move promote(vec4(0, 6, 1, 0), vec4(0, 7, 1, 0), NO_PIECE);
    assert(promotion.preview_move(promote));
    const auto promoted = promotion.get_current_boards();
    assert(promotion.apply_move(promote));
    assert(promotion.get_current_boards() == promoted);
    return 0;
}
} /* namespace preview_tests */

namespace mate_tests
{
int run()
{
    const game stalemate = game::from_pgn(R"(
[Board "Custom"]
[Mode "5D"]
[k7/pn6/K7/8/8/8/6PB/8:0:1:w]
{
Expected result: Stalemate
Thanks @narik for providing this position
}
1. Bg1 / Kb8
2. Bd4 / Kc8
3. Bc3 / (0T3)Nb7>>(0T1)c7~
4. (0T4)Bc3>>(0T1)c6~ / (1T1)Ka8>>(0T1)b8~
5. (-2T2)Bxa7 (-1T2)Ka6>>(0T3)b5~ (1T2)Bc6>>(0T1)c6~
)");
    assert(stalemate.get_current_state().get_mate_type() == mate_type::STALEMATE);
    return 0;
}
} /* namespace mate_tests */

namespace import_deduplication_tests
{
int run()
{
    const std::string header = "[Variant \"Standard - Turn Zero\"]\n";
    game g = game::from_pgn(header + R"(
(1. Nf3 {old branch} / Nf6 {old reply} 2. Nc3 {old descendant})
(1. e4 {unrelated} / e5 {unrelated reply})
(1. Ng1f3 {new branch} / d5 {new reply})
1. d4 {main}
)");
    assert(g.get_comments() == std::vector<std::string>{"main"});
    g.visit_parent();
    const auto children = g.get_child_actions();
    assert(children.size() == 3);
    const auto e4 = std::get<0>(game::from_pgn(header + "1. e4").get_historical_actions().front());
    const auto nf3 = std::get<0>(game::from_pgn(header + "1. Nf3").get_historical_actions().front());
    const auto d4 = std::get<0>(game::from_pgn(header + "1. d4").get_historical_actions().front());
    assert(std::get<0>(children[0]) == e4);
    assert(std::get<0>(children[1]) == nf3);
    assert(std::get<0>(children[2]) == d4);
    assert(!g.add_child(nf3));
    assert(g.visit_child(nf3));
    assert(g.get_comments() == std::vector<std::string>{"new branch"});
    assert(g.get_child_actions().size() == 1);
    assert(g.visit_child(std::get<0>(g.get_child_actions().front())));
    assert(g.get_comments() == std::vector<std::string>{"new reply"});
    assert(g.get_child_actions().empty());
    const auto exported = g.show_pgn();
    assert(exported.find("old branch") == std::string::npos);
    assert(exported.find("old reply") == std::string::npos);
    assert(exported.find("old descendant") == std::string::npos);
    assert(exported.find("unrelated reply") != std::string::npos);
    game roundtrip = game::from_pgn(exported);
    while(roundtrip.has_parent()) roundtrip.visit_parent();
    assert(roundtrip.get_child_actions().size() == 3);
    assert(roundtrip.visit_child(nf3));
    assert(roundtrip.get_comments() == std::vector<std::string>{"new branch"});

    // A final replacement with no descendants must become the loaded endpoint,
    // rather than retaining a pointer to the discarded branch's deepest node.
    game final = game::from_pgn(header + R"(
(1. Nf3 {discarded} / Nf6 2. Nc3)
1. Ng1f3 {survivor}
)");
    assert(final.get_historical_actions().size() == 1);
    assert(final.get_comments() == std::vector<std::string>{"survivor"});
    assert(final.get_child_actions().empty());
    final.visit_parent();
    assert(final.get_child_actions().size() == 1);
    assert(final.visit_child(nf3));

    game nested = game::from_pgn(header + R"(
1. Nf3
(1b. Nf6 {discarded reply} 2. Nc3)
1b. Ng8f6 {surviving reply}
)");
    assert(nested.get_comments() == std::vector<std::string>{"surviving reply"});
    assert(nested.get_historical_actions().size() == 2);
    assert(nested.get_child_actions().empty());
    nested.visit_parent();
    assert(nested.get_child_actions().size() == 1);

    // Reordered moves on independent timelines canonicalize to the same action.
    game reordered = game::from_pgn(R"(
[Timeline "Even"]
[Size "8x8"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:+0:1:w]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:-0:1:w]
(1. (+0T1)e2e3 (-0T1)Ng1f3 {earlier order})
1. (-0T1)Ng1f3 (+0T1)e2e3 {later order}
)");
    assert(reordered.get_comments() == std::vector<std::string>{"later order"});
    reordered.visit_parent();
    assert(reordered.get_child_actions().size() == 1);

    const auto before_failed_load = final.show_pgn();
    bool rejected = false;
    try
    {
        final = game::from_pgn(header + "(1. Nf3 / Nf6) 1. Nf3 / e7e3");
    }
    catch(const std::runtime_error &) { rejected = true; }
    assert(rejected);
    assert(final.show_pgn() == before_failed_load);
    return 0;
}
} /* namespace import_deduplication_tests */

namespace deletion_tests
{
int run()
{
    game g = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 {branch} / Nf6 {descendant}");
    const auto branch = std::get<0>(g.get_historical_actions().front());
    g.visit_parent();
    g.visit_parent();
    game alternative = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. e4");
    const auto other = std::get<0>(alternative.get_historical_actions().front());
    assert(g.has_child(branch));
    assert(!g.has_child(other));
    assert(!g.visit_child(other));
    assert(g.add_child(other));
    assert(g.has_child(other));
    assert(!g.add_child(other));
    assert(g.get_historical_actions().empty());
    const auto invalid = action::from_vector(
        {ext_move(full_move("(0T1)e2e8"), NO_PIECE)}, g.get_unmoved_state());
    assert(!g.has_child(invalid));
    assert(!g.visit_child(invalid));
    assert(!g.add_child(invalid));
    const auto before = g.get_current_boards();
    const auto status = g.get_match_status();
    const auto move = other.get_moves_view().front();
    assert(g.apply_move(move));
    assert(g.get_submit_action() == other);
    assert(g.undo());
    assert(g.can_redo());
    assert(!g.get_submit_action()); // The redo tail is not part of the submitted action.
    assert(g.preview_move(move));
    assert(g.delete_child(branch));
    assert(g.get_current_boards() == before);
    assert(g.get_cached_moves().empty());
    assert(g.can_redo());
    assert(g.get_match_status() == status);
    assert(!g.delete_child(branch));
    assert(!g.has_child(branch));
    assert(!g.visit_child(branch));
    assert(g.get_child_actions().size() == 1);
    const auto exported = g.show_pgn();
    assert(exported.find("branch") == std::string::npos);
    assert(exported.find("descendant") == std::string::npos);
    assert(g.redo());
    const auto draft = g.get_cached_moves();
    assert(g.delete_child(other));
    assert(g.get_cached_moves() == draft && g.can_undo());
    assert(g.get_child_actions().empty());
    assert(g.submit()); // Deletion does not prevent creating the move again later.
    g.set_comments({"preserved"});
    g.visit_parent();
    assert(g.apply_move(move));
    assert(g.submit());
    assert(g.get_comments() == std::vector<std::string>{"preserved"});

    // Owning subtree removal releases descendant payloads and retains sibling order.
    using node = gnode<std::shared_ptr<int>>;
    auto root = node::create_root(g.get_unmoved_state(), nullptr);
    auto payload = std::make_shared<int>(7);
    std::weak_ptr<int> lifetime = payload;
    auto *child = root->add_child(node::create_child(root.get(), std::nullopt, branch, nullptr));
    child->add_child(node::create_child(child, std::nullopt, other, std::move(payload)));
    auto *sibling = root->add_child(node::create_child(root.get(), std::nullopt, other, nullptr));
    assert(root->delete_child(branch));
    assert(lifetime.expired());
    assert(root->get_children().size() == 1 && root->get_children().front().get() == sibling);
    assert(!root->delete_child(branch));
    assert(root->delete_child(other) && root->get_children().empty());
    return 0;
}
} /* namespace deletion_tests */

int main()
{
    std::cerr << "Running multiverse tests\n";
    if(const int result = multiverse_tests::run(); result != 0)
        return result;
    std::cerr << "Running gametree tests\n";
    if(const int result = gametree_tests::run(); result != 0)
        return result;
    std::cerr << "Running hash tests\n";
    if(const int result = hash_tests::run(); result != 0)
        return result;
    if(const int result = import_deduplication_tests::run(); result != 0) return result;
    if(const int result = deletion_tests::run(); result != 0) return result;
    std::cerr << "Running preview tests\n";
    if(const int result = preview_tests::run(); result != 0)
        return result;
    std::cerr << "Running mate tests\n";
    if(const int result = mate_tests::run(); result != 0)
        return result;
    return 0;
}
