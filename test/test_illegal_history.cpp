#undef NDEBUG
#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "game.h"
#include "gametree.h"
#include "pgnparser.h"
#include "state.h"

namespace
{
const std::string standard = "[Board \"Standard - Turn Zero\"]\n";
const std::string mate = "1. f3 / e5 2. g4 / Qh4";
const std::string illegal_setup = R"([Board "Custom"]
[Size "8x8"]
[Timeline "odd"]
[7k/8/8/8/8/8/8/K7:0:1:w]
[7k/3r4/8/8/3K4/8/8/8:1:1:b]
)";

template<class Load>
void rejects(Load load, const std::string &reason)
{
    try { load(); }
    catch(const pgn_validation_error &error) {
        assert(std::string(error.what()).find(reason) != std::string::npos);
        return;
    }
    throw std::logic_error("Expected PGN loading to fail");
}

void test_strict_state_loading()
{
    const state legal = state::from_pgn(standard + "1. e4 / e5");
    assert(legal.show_fen() == state::from_pgn<true>(standard + "1. e4 / e5").show_fen());
    const state checked = state::from_pgn(standard + mate);
    assert(checked.has_phantom_check());
    rejects([] { state::from_pgn(illegal_setup); }, "Illegal initial setup");
    rejects([] { game::from_pgn(illegal_setup); }, "Illegal initial setup");
    const state unchecked = state::from_pgn<true>(illegal_setup);
    assert(unchecked.find_royal_exposure(true));
    const auto setup_ast = pgnparser(illegal_setup).parse_game();
    rejects([&] { state::from_ast(*setup_ast); }, "Illegal initial setup");
    assert(state::from_ast<true>(*setup_ast).show_fen() == unchecked.show_fen());

    // The final position is safe, but an earlier white action was illegal.
    const std::string recovered = standard + mate + " 3. a3 / a6";
    const state final = state::from_pgn<true>(recovered);
    assert(!final.find_royal_exposure(false));
    const auto recovered_ast = pgnparser(recovered).parse_game();
    assert(state::from_ast<true>(*recovered_ast).show_fen() == final.show_fen());
    rejects([&] { state::from_pgn(recovered); }, "Illegal action");
    rejects([&] { game::from_pgn(recovered); }, "Illegal action");
    rejects([&] { state::from_ast(*pgnparser(recovered).parse_game()); }, "Illegal action");
    rejects([] {
        state::from_pgn("[Size \"8x8\"] [7k/8/8/8/8/8/8/K7:0:1:w]"
            " [7k/8/8/8/8/8/8/K7:1:1:w] 1. (0T1)a1b1");
    }, "Cannot submit");
}

void test_unsafe_validation()
{
    // UNSAFE relaxes royal safety, not move resolution or completed turns.
    for(const std::string pgn : {
        standard + "1. e2e5",
        std::string("[Promotions \"q\"]") + standard,
        std::string("[Timeline \"sideways\"]") + standard,
        std::string("[Size \"8x8\"] [7k/8/8/8/8/8/8/K7:0:1:w]"
            " [7k/8/8/8/8/8/8/K7:1:1:w] 1. (0T1)a1b1")}) {
        const auto ast = pgnparser(pgn).parse_game();
        rejects([&] { state::from_pgn<true>(pgn); }, "");
        rejects([&] { state::from_ast<true>(*ast); }, "");
    }
    const std::string bad_fen = "[Board \"Custom\"] [7k/8/8/8/8/8/8/K6:0:1:w]";
    const auto ast = pgnparser(bad_fen).parse_game();
    for(bool from_text : {false, true}) {
        bool rejected = false;
        try {
            if(from_text) state::from_pgn<true>(bad_fen);
            else state::from_ast<true>(*ast);
        } catch(const board_fen_error &error) {
            rejected = true;
            assert(error.get_pgn_source() && *error.get_pgn_source() == bad_fen);
            assert(error.get_pgn_range());
        }
        assert(rejected);
    }
    bool rejected_syntax = false;
    try { state::from_pgn<true>(standard + "1. {"); }
    catch(const parse_error &) { rejected_syntax = true; }
    assert(rejected_syntax);
}

void test_illegal_roots()
{
    auto g = game::from_pgn(illegal_setup, {.allow_illegal_history = true});
    assert(g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::ILLEGAL_HISTORY);
    assert(!g.suggest_action());
    assert(!g.has_parent());
    assert(g.apply_move(ext_move("(0T1)a1b2")));
    assert(g.has_illegal_history());
    assert(g.submit());
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    const auto exported = g.show_pgn(pgn_options::SHOW_ALL);
    auto restored = game::from_pgn(exported, {.allow_illegal_history = true});
    assert(restored.has_illegal_history());
    assert(restored.get_current_boards() == g.get_current_boards());
    rejects([&] { game::from_pgn(exported); }, "Illegal initial setup");

    // An inactive opponent endpoint is covered by the same loading policy.
    const std::string inactive = R"([Board "Custom"]
[7k/8/8/8/8/8/8/K7:0:1:w]
[8/8/8/8/8/8/8/8:1:1:b]
[7k/3r4/8/8/3K4/8/8/8:2:1:b]
)";
    rejects([&] { state::from_pgn(inactive); }, "Illegal initial setup");
    assert(game::from_pgn(inactive, {.allow_illegal_history = true}).has_illegal_history());
}

void test_manual_submission_and_navigation()
{
    auto g = game::from_pgn(standard + mate);
    assert(!g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::BLACK_WINS);
    assert(g.apply_move(ext_move("(0T3)a2a3")));
    assert(!g.has_illegal_history()); // Drafts do not change submitted history.
    const auto unsafe_action = *g.get_submit_action();
    assert(g.submit());
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    assert(g.get_match_status() == game::match_status_t::ILLEGAL_HISTORY);
    assert(g.apply_move(ext_move("(0T3)a7a6")));
    assert(g.submit());
    assert(!g.get_current_state().find_royal_exposure(false));
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    g.visit_parent();
    assert(g.has_illegal_history());
    assert(g.get_child_actions().front().is_illegal);
    g.visit_parent();
    assert(!g.has_illegal_history());
    assert(g.get_child_actions().front().act == unsafe_action);
    assert(g.get_child_actions().front().is_illegal);
    assert(g.get_match_status() == game::match_status_t::BLACK_WINS);
    const auto before = g.show_pgn();
    assert(!g.visit_child(action{}));
    assert(g.show_pgn() == before);
    assert(g.visit_child(unsafe_action));
    assert(g.has_illegal_history());
    g.visit_parent();
    assert(g.apply_move(ext_move("(0T3)a2a3")));
    assert(g.submit()); // Reusing the existing child preserves its flag.
    assert(g.has_illegal_history());
}

void test_variations_and_export()
{
    const std::string pgn = standard + "1. f3 / e5 (2. g4 / Qh4 3. a3 / a6) 2. g3";
    rejects([&] { game::from_pgn(pgn); }, "Illegal action");
    auto g = game::from_pgn(pgn, {.allow_illegal_history = true});
    assert(!g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::PLAYING);
    const auto flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION
        | pgn_options::SHOW_MATE | pgn_options::SHOW_OUTCOME | pgn_options::SHOW_SHORT;
    const auto exported = g.show_pgn(flags);
    // The illegal child must not act as a witness against Qh4 being checkmate.
    assert(exported.find("Qh4#") != std::string::npos);
    auto restored = game::from_pgn(exported, {.allow_illegal_history = true});
    assert(!restored.has_illegal_history());
    g.visit_parent(); // After e5, both the legal and illegal variations start legally.
    for(const auto &summary : g.get_child_actions()) {
        assert(!summary.is_illegal);
    }

    auto branch = game::from_pgn(standard + mate + " 3. a3 / a6", {.allow_illegal_history = true});
    const auto path = branch.show_pgn(flags, false);
    assert(path.find("Qh4#") != std::string::npos);
    assert(path.find("a3+") == std::string::npos);
    assert(path.find("a6+") == std::string::npos);
    auto roundtrip = game::from_pgn(path, {.allow_illegal_history = true});
    assert(roundtrip.has_illegal_history());
    assert(roundtrip.get_current_boards() == branch.get_current_boards());

    branch.visit_parent();
    branch.visit_parent();
    const auto unsafe = branch.get_child_actions().front();
    assert(branch.get_child_actions(flags).front().pgn.find('+') == std::string::npos);
    assert(unsafe.is_illegal);
    assert(branch.delete_child(unsafe.act));
    assert(!branch.has_illegal_history());
    assert(branch.get_match_status() == game::match_status_t::BLACK_WINS);
}

void test_clone_and_inheritance()
{
    const state initial = state::from_pgn(standard);
    auto root = gnode<>::create_root(initial, {});
    assert(!root->has_illegal_history());
    auto illegal = gnode<>::create_child(root.get(), initial, action{}, {}, true);
    auto descendant = gnode<>::create_child(illegal.get(), initial, action{}, {});
    assert(descendant->has_illegal_history());
    illegal->add_child(std::move(descendant));
    root->add_child(std::move(illegal));
    const auto clone = root->clone();
    assert(!clone->has_illegal_history());
    assert(clone->get_children().front()->has_illegal_history());
    assert(clone->get_children().front()->get_children().front()->has_illegal_history());
    std::ostringstream status;
    status << game::match_status_t::ILLEGAL_HISTORY;
    assert(status.str() == "ILLEGAL_HISTORY");
}
} /* anonymous namespace */

int main()
{
    try {
        test_strict_state_loading();
        test_unsafe_validation();
        test_illegal_roots();
        test_manual_submission_and_navigation();
        test_variations_and_export();
        test_clone_and_inheritance();
    }
    catch(const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
