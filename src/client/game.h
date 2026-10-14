// game.h
// interface for python library
#ifndef GAME_H
#define GAME_H

#include <vector>
#include <tuple>
#include <string>
#include <map>
#include <set>
#include <optional>
#include "state.h"
#include "gametree.h"

class game
{
    using comments_t = std::vector<std::string>;
    std::unique_ptr<gnode<comments_t>> gametree;
    gnode<comments_t> *current_node;
    using cache_t = std::pair<state,std::optional<ext_move>>;
    std::vector<cache_t> cached;
    std::vector<cache_t>::iterator now;
    std::optional<state> preview;
    
    game(std::unique_ptr<gnode<comments_t>> gt);
    mutable std::optional<bool> nonterminal;
    void fresh();
public:
    /* Metadata edits do not change the loaded game’s rules or position. During PGN export, headers 
    describing game configuration are generated from the actual configuration and override conflicting 
    metadata values. */
    std::map<std::string, std::string> metadata;
    
    static game from_pgn(std::string str);
    
    const state &get_current_state() const;
    const state &get_unmoved_state() const;
    std::pair<int, bool> get_current_present() const;
    std::vector<ext_move> get_cached_moves() const;
    std::vector<boards_info_t> get_current_boards() const;
    std::pair<std::vector<boards_info_t>, std::vector<full_move>> get_phantom_boards_and_checks() const;
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_current_timeline_status() const;
    std::vector<vec4> gen_move_if_playable(vec4 p) const;
    
    match_status_t get_match_status() const;
    std::vector<vec4> get_movable_pieces() const;
    
    bool is_playable(vec4 p) const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_submit() const;
    bool undo();
    bool redo();
    bool apply_move(ext_move m);
    bool preview_move(ext_move m);
    void discard_preview();
    bool submit();
    std::optional<action> get_submit_action() const;
    bool currently_check() const;
    std::vector<std::pair<vec4,vec4>> get_current_checks() const;
    std::pair<int, int> get_board_size() const;
    promotion_options get_promotion_options() const;
    
    std::optional<action> suggest_action();
    // Insert a validated continuation without navigating or clearing draft moves.
    bool add_child(action act);
    bool has_child(const action &act) const;
    // Remove a direct continuation without changing the current draft or history.
    bool delete_child(const action &act);

    comments_t get_comments() const;
    void set_comments(const comments_t &c) const;
    bool has_parent() const;
    void visit_parent();
    std::vector<std::tuple<action, std::string>> get_child_actions(
        pgn_options show_flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION) const;
    std::vector<std::tuple<action, std::string>> get_historical_actions() const;
    // Navigate only to an existing child; return false without mutation if absent.
    bool visit_child(const action &act);
    
    std::string show_pgn(
        pgn_options show_flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION | pgn_options::SHOW_MATE,
        bool complete_game_tree = true,
        bool standard_metadata_only = false
    );
};



#endif // GAME_H
