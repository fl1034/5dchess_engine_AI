#undef NDEBUG
#include <cassert>
#include "client/game.h"

int main()
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

    assert(g.preview_move(first));
    const auto expected = base.can_apply(first.fm, first.promote_to);
    assert(expected);
    assert(g.get_current_boards() == expected->get_boards());
    assert(g.get_current_boards() != base.get_boards());
    assert(g.get_cached_moves().empty());
    assert(!g.can_undo() && !g.can_redo());
    assert(g.can_submit() == submit);
    assert(g.gen_move_if_playable(first.get_from()) == legal);
    assert(g.show_pgn() == pgn);
    // Replacement calculates from the real position, not the previous preview.
    assert(g.preview_move(second));
    assert(g.get_current_boards() == base.can_apply(second.fm, second.promote_to)->get_boards());
    assert(!g.preview_move(invalid));
    assert(g.get_current_boards() == base.get_boards());
    g.discard_preview();
    g.discard_preview();
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
}
