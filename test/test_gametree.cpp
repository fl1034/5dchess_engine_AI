#undef NDEBUG
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "gametree.h"
#include "hypercuboid.h"
#include "pgnparser.h"

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

void test_owned_payload_and_formatting(const state &initial, const action &act)
{
    auto owned = gnode<std::unique_ptr<int>>::create_root(initial, std::make_unique<int>(7));
    assert(*owned->get_info() == 7);
    using node_t = gnode<counted_info>;
    auto root = node_t::create_root(initial, counted_info{1});
    auto *child = root->add_child(node_t::create_child(root.get(), std::nullopt, act, counted_info{2}));
    root->add_child(node_t::create_child(root.get(), std::nullopt, act, counted_info{3}));
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

int main() try
{
    state initial(*pgnparser("[Variant \"Standard\"]").parse_game());
    auto [info, space] = HC_info::build_HC(initial);
    auto moves = info.search(std::move(space)).first();
    assert(moves);
    const auto act = action::from_moveseq(*moves, initial);
    test_states_and_attachment(initial, act);
    test_owned_payload_and_formatting(initial, act);
}

catch(const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
