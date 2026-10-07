#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <vector>
#include "core/hash.h"
#include "game.h"

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
    multiverse_odd original(boards);
    const auto original_hash = hash_multiverse(original);
    assert(hash_multiverse(*original.clone()) == original_hash);
    std::reverse(boards.begin(), boards.end());
    const multiverse_odd reordered(boards);
    assert(hash_multiverse(reordered) == original_hash);

    // Equal endpoint boards do not make different histories duplicates.
    original.get_board(0, 1, true)->set_piece(ppos(0, 0), ROOK_W);
    assert(hash_board(*original.get_board(0, 2, false)) ==
           hash_board(*reordered.get_board(0, 2, false)));
    assert(hash_multiverse(original) != original_hash);
    original.get_board(0, 1, true)->set_piece(ppos(0, 0), NO_PIECE);
    assert(hash_multiverse(original) == original_hash);

    const multiverse_odd shifted({{0, 3, false, empty}});
    const multiverse_odd earlier({{0, 2, false, empty}});
    const multiverse_odd other_color({{0, 2, true, empty}});
    assert(hash_multiverse(shifted) != hash_multiverse(earlier));
    assert(hash_multiverse(other_color) != hash_multiverse(earlier));

    const multiverse_odd odd(boards);
    const multiverse_even even(boards);
    assert(hash_multiverse(odd) != hash_multiverse(even));

    // A 4x4 board and an 8x8 board with matching walls have equal contents,
    // irrespective of nominal dimensions.
    multiverse_odd square({{0, 1, false, empty}});
    multiverse_odd small({{0, 1, false, "3k/4/4/K3"}}, 4, 4);
    for(int square_index = 0; square_index < BOARD_SIZE; ++square_index)
    {
        square.get_board(0, 1, false)->set_piece(
            square_index, small.get_board(0, 1, false)->get_piece(square_index));
    }
    assert(hash_board(*square.get_board(0, 1, false)) ==
           hash_board(*small.get_board(0, 1, false)));
    assert(hash_multiverse(square) == hash_multiverse(small));
    assert(hash_state(state(square)) == hash_state(state(small)));

    auto extended = earlier.clone();
    extended->append_board(0, std::make_shared<board>(empty));
    assert(hash_multiverse(*extended) != hash_multiverse(earlier));

    multiverse_odd inactive({{0, 2, false, empty},
                             {1, 2, false, empty},
                             {2, 2, false, empty}});
    assert(inactive.get_active_range().second == 1);
    const auto inactive_hash = hash_multiverse(inactive);
    inactive.get_board(2, 2, false)->set_piece(ppos(7, 7), ROOK_B);
    assert(hash_multiverse(inactive) != inactive_hash);
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

    multiverse_odd universe({{0, 1, false, "8/8/8/8/8/8/8/8"}});
    const state queen(universe, promotion_options::QUEEN);
    const state knight(universe, promotion_options::KNIGHT);
    assert(hash_state(queen) != hash_state(knight));

    // Before and after submission, the multiverse is identical but the stored
    // present/player changes. Rebuilding before submission uses the apparent
    // present rather than the inherited one, which must affect the state hash.
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

    // Phantom states retain their stored present. Advancing both colors
    // changes its time while retaining the same player and board contents.
    const state phantom = queen.phantom(false).phantom(true);
    multiverse_odd phantom_universe(phantom.get_boards());
    const state apparent(phantom_universe, promotion_options::QUEEN);
    assert(phantom.show_fen() == apparent.show_fen());
    assert(phantom.get_present().second == apparent.get_present().second);
    assert(phantom.get_present().first != apparent.get_present().first);
    assert(hash_state(phantom) != hash_state(apparent));
}
} // namespace

int main()
{
    test_boards();
    test_multiverses();
    test_states();
}
