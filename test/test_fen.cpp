#undef NDEBUG
#include <cassert>
#include <stdexcept>
#include "board.h"
#include "game.h"

namespace
{
void test_constructor_and_edit_flags()
{
    const std::string fen = "K*R*P*W*k*r*p*w*/QBNUSYCD/qbnusycd/1#2#3";
    board position(fen, 8, 4);
    assert(position.get_fen<true>(8, 4) == fen);
    assert(position.get_fen(8, 4) == "KRPWkrpw/QBNUSYCD/qbnusycd/1#2#3");
    bitboard_t unmoved = 0;
    for(int x = 0; x < BOARD_LENGTH; ++x)
        unmoved |= pmask(ppos(x, 3));
    assert(position.umove() == unmoved);
    for(int y = 4; y < BOARD_LENGTH; ++y)
        for(int x = 0; x < BOARD_LENGTH; ++x)
            assert(position.get_piece(ppos(x,y)) == WALL_PIECE);

    position.set_piece(ppos(1, 3), ROOK_W);
    assert(position.umove() == (unmoved & ~pmask(ppos(1, 3))));
    const auto edited = position.replace_piece(ppos(6, 3), NO_PIECE);
    assert(edited->umove() == (position.umove() & ~pmask(ppos(6, 3))));
    assert(position.get_piece(ppos(6, 3)) == PAWN_B);

    // Preserve standalone parsing's existing whitespace, zero, and short-input rules.
    assert(board(" 1 0 # 2 / 4 ", 4, 3).get_fen(4, 3) == "1#2/4/####");
    for(const auto *invalid : {"N*", "#*", "1*", "Z", "9", "4/4/4/4", "4/4/3K*Q"})
    {
        bool rejected = false;
        try { board position(invalid, 4, 3); }
        catch(const std::runtime_error &) { rejected = true; }
        assert(rejected);
    }
}

void test_text_output()
{
    const board position("k*1#1/####/R*1K*#", 4, 3);
    const std::string wall_row = "################\n";
    const std::string expected = wall_row + wall_row + wall_row + wall_row + wall_row
        + "k . ##. ########\n"
        + wall_row
        + "R . K ##########\n";
    assert(position.to_string() == expected);
}

void test_walls_and_unmoved_pieces()
{
    const std::string fen = "k1#1/####/R*1K*#";
    const board position(fen, 4, 3);
    assert(position.get_fen<true>(4, 3) == fen);
    assert(position.get_fen(4, 3) == "k1#1/####/R1K#");
    const board restored(position.get_fen<true>(4, 3), 4, 3);
    for(int pos = 0; pos < BOARD_SIZE; ++pos)
        assert(position.get_piece(pos) == restored.get_piece(pos));
    assert(position.umove() == restored.umove());
    assert(position.wall() == restored.wall());

    // Short ranks still load, but their implicit walls are printed explicitly.
    assert(board("k/4/K", 4, 3).get_fen(4, 3) == "k###/4/K###");

    const board empty("4/4/4", 4, 3);
    const auto edited = empty.replace_piece(ppos(1, 1), WALL_PIECE);
    assert(edited->get_fen(4, 3) == "4/1#2/4");
    assert(empty.get_fen(4, 3) == "4/4/4");
}

void test_dimensions()
{
    for(int height = 1; height <= BOARD_LENGTH; ++height)
        for(int width = 1; width <= BOARD_LENGTH; ++width)
        {
            std::string fen;
            for(int row = 0; row < height; ++row)
            {
                if(row) fen += '/';
                fen += std::string(width, '#');
            }
            const board position(fen, width, height);
            assert(position.get_fen(width, height) == fen);
        }

    board clipped("k3/4/3K", 4, 3);
    clipped.set_piece(ppos(7, 7), QUEEN_W);
    assert(clipped.get_fen(4, 3) == "k3/4/3K");
    for(const auto &[width, height] : {std::pair{0, 8}, {9, 8}, {8, 0}, {8, 9}})
    {
        bool rejected = false;
        try { clipped.get_fen(width, height); }
        catch(const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
}

void test_game_round_trip()
{
    auto original = game::from_pgn("[Board \"Custom\"] [Size \"4x3\"] [k1#1/####/R*1K*#:0:1:w]");
    assert(std::get<3>(original.get_current_boards().front()) == "k1#1/####/R1K#");
    const auto exported = original.show_pgn();
    assert(exported.find("k1#1/####/R*1K*#") != std::string::npos);
    const auto restored = game::from_pgn(exported);
    assert(original.get_current_boards() == restored.get_current_boards());
    assert(original.get_current_state().get_board(0, 1, false)->umove()
           == restored.get_current_state().get_board(0, 1, false)->umove());
}
} /* anonymous namespace */

int main()
{
    test_constructor_and_edit_flags();
    test_text_output();
    test_walls_and_unmoved_pieces();
    test_dimensions();
    test_game_round_trip();
}
