
#include "hypercuboid.h"
#include "check_position.h"
#include "move_feature.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <random>

//#define DEBUGMSG
#include "debug.h"

semimove HC_info::to_semimove(const entry &loc)
{
    return std::visit([](const auto &move) -> semimove {
        using T = std::decay_t<decltype(move)>;
        if constexpr (std::is_same_v<T, physical_entry>) return physical_move{move.m};
        else if constexpr (std::is_same_v<T, arriving_entry>) return arriving_move{move.m};
        else if constexpr (std::is_same_v<T, departing_entry>) return departing_move{move.from};
        else return null_move{};
    }, loc);
}

const board &HC_info::extract_board(const entry &loc)
{
    return std::visit([](const auto &move) -> const board & {
        using T = std::decay_t<decltype(move)>;
        if constexpr (std::is_same_v<T, physical_entry>
                   || std::is_same_v<T, arriving_entry>
                   || std::is_same_v<T, departing_entry>) return move.b;
        else {
            assert(false && "shouldn't extract a board from a null entry");
            throw std::logic_error("cannot extract a board from a null HC entry");
        }
    }, loc);
}

std::pair<int, int> HC_info::extract_tl(const entry &loc)
{
    const vec4 p = to_semimove(loc).hotspot();
    return {p.t(), p.l()};
}

semimove HC_info::get_semimove(index_t n, index_t i) const
{
    return to_semimove(axis_coords.at(n).at(i));
}

std::optional<HC_info::move_boards> HC_info::get_move_boards(index_t axis, index_t coordinate) const
{
    const auto &loc = axis_coords.at(axis).at(coordinate);
    if (const auto *physical = std::get_if<physical_entry>(&loc))
        return move_boards{physical->m, &physical->b, nullptr};
    if (const auto *arrival = std::get_if<arriving_entry>(&loc)) {
        const auto source_axis = line_axes.at(arrival->m.from.l());
        const auto &departure = std::get<departing_entry>(axis_coords[source_axis].at(arrival->idx));
        return move_boards{arrival->m, &arrival->b, &departure.b};
    }
    return std::nullopt;
}

/*
 gen_move_path: in state s, find the checking path
 return the checking path and sliding_type, where
 - sliding_type = 0 : non-sliding
 - sliding_type = 1 : rook move
 - sliding_type = 2 : bishop move
 - sliding_type = 3 : unicorn move
 - sliding_type = 4 : dragon move
 the endpoints are excluded in checking path
 */
std::tuple<std::vector<vec4>, int> get_move_path(const check_position &s, full_move fm, int c)
{
    const vec4 p = fm.from, q = fm.to, d = q - p;
    const board *b_ptr = s.get_board_ptr(p.l(), p.t(), c);
    if(b_ptr->sliding() & pmask(p.xy()))
    {
        // this piece is sliding, makes sense to talk about path
        std::vector<vec4> path;
        vec4 c = vec4(signum(d.x()), signum(d.y()), signum(d.t()), signum(d.l()));
        int sliding_type = c.dot(c);
#ifndef NDEBUG
        {
            assert(p!=q && sliding_type > 0 && sliding_type <= 4);
            std::set tester = {abs(d.l()), abs(d.t()), abs(d.x()), abs(d.y())};
            tester.erase(0);
            assert(tester.size() == 1 && "This doesn't look like a good sliding move");
        }
#endif
        for(vec4 r = p + c; r != q; r = r + c)
        {
            path.push_back(r);
        }
        return std::make_tuple(path, sliding_type);
    }
    else
    {
        return std::make_tuple(std::vector<vec4>(), 0);
    }
}

std::pair<HC_info, search_space> HC_info::build_HC(const state &s)
{
    dprint("HC_info::build_HC()");
    std::map<int, index_t> line_axes; // map from timeline index to axis index
    std::vector<std::vector<entry>> axis_coords; // axis_coords[i] is the set of all moves on i-th playable board
    std::vector<integer_set> universe_axes;
    index_t new_axis, dimension;
    std::vector<integer_set> nonbranching_axes, branching_axes;
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = s.get_timeline_status();
    auto playable_timelines = concat_vectors(mandatory_timelines, optional_timelines);
    assert(!s.test_present());
    const bool player = s.get_present().c();

    for(index_t axis = 0; axis < static_cast<index_t>(playable_timelines.size()); axis++)
    {
        line_axes.emplace(playable_timelines[axis], axis);
    }

    std::array<std::byte, 32 * 1024> temporary_storage;
    std::pmr::monotonic_buffer_resource temporary_resource(
        temporary_storage.data(), temporary_storage.size());

    // generate all moves, then split them into cases
    // for departing moves, we merge the moves that depart from the same coordinate
    struct arrival_bucket
    {
        int line;
        std::pmr::vector<full_move> moves;

        arrival_bucket(int line, std::pmr::memory_resource *resource)
            : line(line), moves(resource) {}
    };
    struct axis_moves
    {
        std::pmr::vector<full_move> stays;
        std::pmr::vector<vec4> departures;

        explicit axis_moves(std::pmr::memory_resource *resource)
            : stays(resource), departures(resource) {}
    };

    std::pmr::vector<arrival_bucket> arrivals(&temporary_resource);
    arrivals.reserve(
        mandatory_timelines.size()
        + optional_timelines.size()
        + unplayable_timelines.size());
    std::pmr::vector<axis_moves> moves_by_axis(&temporary_resource);
    moves_by_axis.reserve(playable_timelines.size());
    for(size_t i = 0; i < playable_timelines.size(); i++)
    {
        moves_by_axis.emplace_back(&temporary_resource);
    }

    auto add_arrival = [&arrivals, &temporary_resource](full_move move)
    {
        const int line = move.to.l();
        auto bucket = std::find_if(
            arrivals.begin(), arrivals.end(),
            [line](const arrival_bucket &candidate) { return candidate.line == line; });
        if(bucket == arrivals.end())
        {
            arrivals.emplace_back(line, &temporary_resource);
            bucket = std::prev(arrivals.end());
        }
        bucket->moves.push_back(move);
    };

    // to track the corresponding departing moves for each arriving move
    struct jump_index
    {
        vec4 from;
        index_t entry;
    };
    std::pmr::vector<jump_index> jump_indices(&temporary_resource);

    auto find_jump_index = [&jump_indices](vec4 from) -> const index_t *
    {
        auto result = std::find_if(
            jump_indices.begin(), jump_indices.end(),
            [from](const jump_index &candidate) { return candidate.from == from; });
        return result == jump_indices.end() ? nullptr : &result->entry;
    };
    
    const auto [size_x, size_y] = s.get_board_size();
    
    for(vec4 from : s.get_all_pieces(playable_timelines))
    {
        const index_t source_axis = line_axes.at(from.l());
        bool has_depart = false;
        for(const vec4 &to : s.gen_piece_move(from))
        {
            full_move m(from, to);
            if(from.tl() != to.tl())
            {
                if(!has_depart)
                {
                    moves_by_axis[source_axis].departures.push_back(m.from);
                    has_depart = true;
                }
                add_arrival(m);
            }
            else
            {
                moves_by_axis[source_axis].stays.push_back(m);
            }
        }
    }

    std::sort(
        arrivals.begin(),
        arrivals.end(),
        [](const arrival_bucket &lhs, const arrival_bucket &rhs)
        {
            return lhs.line < rhs.line;
        }
    );
    auto find_arrivals = [&arrivals](int line) -> const std::pmr::vector<full_move> *
    {
        auto bucket = std::lower_bound(
            arrivals.begin(), arrivals.end(), line,
            [](const arrival_bucket &candidate, int value)
            {
                return candidate.line < value;
            });
        return bucket == arrivals.end() || bucket->line != line
            ? nullptr : &bucket->moves;
    };

    index_t max_branch = 0;
    size_t departure_count = 0;
    for(const auto &moves : moves_by_axis)
    {
        departure_count += moves.departures.size();
        if(!moves.departures.empty()) max_branch++;
    }
    jump_indices.reserve(departure_count);
    axis_coords.reserve(playable_timelines.size() + max_branch);

    // build nonbranching axes
    for(index_t axis = 0; axis < static_cast<index_t>(playable_timelines.size()); axis++)
    {
        const int l = playable_timelines[axis];
        const auto *arriving_moves = find_arrivals(l);
        std::vector<entry> locs;
        locs.reserve(
            1 + moves_by_axis[axis].stays.size()
            + moves_by_axis[axis].departures.size()
            + (arriving_moves ? arriving_moves->size() : 0));
        locs.push_back(null_entry{});
        for(full_move m : moves_by_axis[axis].stays)
        {
            vec4 p = m.from, q = m.to;
            vec4 d = q - p;
            const board &source_board = *s.get_board_ptr(p.l(), p.t(), player);
            dprint(locs.size(), "physical", m);
            locs.emplace_back(std::in_place_type<physical_entry>, m, source_board);
            board &newboard = std::get<physical_entry>(locs.back()).b;
            bitboard_t z = pmask(p.xy());
            // en passant
            if((source_board.lpawn()&z) && d.x()!=0
               && source_board.get_piece(q.xy()) == NO_PIECE)
            {
                dprint(" ... en passant");
                newboard.set_piece(ppos(q.x(),p.y()), NO_PIECE);
                piece_t piece = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), piece);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // castling
            else if((source_board.king()&z) && abs(d.x()) > 1)
            {
                dprint(" ... castling");
                const bitboard_t side = d.x() < 0 ? z - 1 : ~(z - 1);
                int rook_x2 = q.x() + (d.x() < 0 ? 1 : -1); //rook's new x coordinate
                const int rook_from = bb_get_pos(source_board.castling_rooks(p.xy(), player) & side);
                const int rook_to = ppos(rook_x2, q.y());
                piece_t rook = newboard.get_piece(rook_from);
                newboard.set_piece(rook_to, rook);
                newboard.set_piece(rook_from, NO_PIECE);
                piece_t king = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), king);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // normal move
            else
            {
                dprint(" ... normal move/capture");
                piece_t piece = newboard.get_piece(p.xy());
                newboard.set_piece(q.xy(), piece);
                newboard.set_piece(p.xy(), NO_PIECE);
            }
            // Legality checking only needed occupancy here. Outgoing-check scoring
            // also needs the promoted piece's attacks, so cache the default promotion.
            if ((source_board.lpawn() & z) && (q.y() == (player ? 0 : size_y - 1))) {
                const piece_t promotion = ext_move(m,s).promote_to;
                if (promotion != NO_PIECE)
                    newboard.set_piece(q.xy(), player ? to_black(promotion) : promotion);
            }
            // filter physical checks in the very begining
            if(has_physical_check(newboard, player))
            {
                locs.pop_back();
            }
        }
        for(vec4 p : moves_by_axis[axis].departures)
        {
            assert(find_jump_index(p) == nullptr);
            // store the departing board after move is made
            const index_t entry_index = static_cast<index_t>(locs.size());
            locs.emplace_back(
                std::in_place_type<departing_entry>, p,
                *s.get_board_ptr(p.l(), p.t(), player));
            board &newboard = std::get<departing_entry>(locs.back()).b;
            newboard.set_piece(p.xy(), NO_PIECE);
            dprint(entry_index, "depart", p);
            if(has_physical_check(newboard, player))
            {
                locs.pop_back();
            }
            else
            {
                jump_indices.push_back(jump_index{p, entry_index});
            }
        }
        if(arriving_moves)
        {
            for(full_move m : *arriving_moves)
            {
                // only store (possible) non-branching jump arrives
                auto [last_t, last_c] = s.get_timeline_end(m.to.l()).get();
                if(m.to.t() == last_t &&player == last_c)
                {
                    assert(m.from.tl()!=m.to.tl());
                    // store the arriving board after move is made
                    vec4 p = m.from, q = m.to;
                    piece_t pic = s.get_piece(p, player);
                    if (to_white(piece_name(pic)) == BRAWN_W && (q.y() == (player ? 0 : size_y - 1))) {
                        const piece_t promotion = ext_move(m,s).promote_to;
                        if (promotion != NO_PIECE) pic = player ? to_black(promotion) : promotion;
                    }
                    const board &destination_board = *s.get_board_ptr(q.l(), q.t(), player);

                    dprint(" ... nonbranching jump");
                    dprint(locs.size(), "arrive", m);
                    locs.emplace_back(
                        std::in_place_type<arriving_entry>, m, destination_board,
                        std::numeric_limits<index_t>::max());
                    board &newboard = std::get<arriving_entry>(locs.back()).b;
                    newboard.set_piece(q.xy(), pic);

                    // use a temporary idx of -1, will be filled later
                    if(has_physical_check(newboard, player))
                    {
                        locs.pop_back();
                    }
                }
            }
        }
        // save this axis
        assert(line_axes.at(l) == axis_coords.size());
        dprint("above in axis", line_axes.at(l));
        axis_coords.push_back(std::move(locs));
    }
    
    new_axis = static_cast<int>(axis_coords.size());

    // build branching axes
    // collect all branching moves
    std::vector<entry> locs;
    size_t arrival_count = 0;
    for(const auto &bucket : arrivals) arrival_count += bucket.moves.size();
    locs.reserve(1 + arrival_count);
    locs.push_back(null_entry{});
    for(const auto &bucket : arrivals)
    {
        for(full_move m : bucket.moves)
        {
            vec4 p = m.from, q = m.to;
            piece_t pic = s.get_piece(p, player);
            if (to_white(piece_name(pic)) == BRAWN_W && (q.y() == (player ? 0 : size_y - 1))) {
                const piece_t promotion = ext_move(m,s).promote_to;
                if (promotion != NO_PIECE) pic = player ? to_black(promotion) : promotion;
            }
            const board &destination_board = *s.get_board_ptr(q.l(), q.t(), player);
            
            dprint(" ... branching jump");
            if(const index_t *departure = find_jump_index(m.from))
            {
                /* only add this arriving move when the corresponding departing move
                 is legal (Otherwise, it shouldn't have been registered in jump_map) */
                dprint(locs.size(), "branching:", m);
                locs.emplace_back(
                    std::in_place_type<arriving_entry>, m, destination_board, *departure);
                board &newboard = std::get<arriving_entry>(locs.back()).b;
                newboard.set_piece(q.xy(), pic);
                if(has_physical_check(newboard, player))
                {
                    locs.pop_back();
                }
            }
        }
    }
    // replicate this axis max_branch times
    const int new_l = s.new_line();
    const int sign = signum(s.new_line()); // sign for the new lines
    for(index_t i = 0; i < max_branch; i++)
    {
        assert(!line_axes.contains(new_l+sign*i));
        line_axes[new_l+sign*i] = new_axis + i;
        axis_coords.push_back(locs);
    }
    dimension = static_cast<index_t>(axis_coords.size());
    
    // build the whole space
    universe_axes.reserve(dimension);
    for(index_t n = 0; n < dimension; n++)
    {
        integer_set coords;
        // on nth dimension, the hypercube has coordinates 0, 1, ..., m avialible
        // which corresponds to axis_coords[n][0], axis_coords[n][1], ...
        for(index_t i = 0; i < static_cast<index_t>(axis_coords[n].size()); i++)
        {
            coords.insert(i);
        }
        universe_axes.push_back(std::move(coords));
    }
    HC universe(std::move(universe_axes));
    
    // fill the idx of arriving moves
    for(index_t n = 0; n < static_cast<index_t>(axis_coords.size()); n++)
    {
        for(index_t i = 0; i < static_cast<index_t>(axis_coords[n].size()); i++)
        {
            entry &loc = axis_coords[n][i];
            if(auto *p = std::get_if<arriving_entry>(&loc))
            {
                if(const index_t *departure = find_jump_index(p->m.from))
                {
                    p->idx = *departure;
#ifndef NDEBUG
                    assert(line_axes.contains(p->m.from.l()));
                    index_t nfrom = line_axes[p->m.from.l()];
                    assert(p->m.from == std::get<departing_entry>(axis_coords[nfrom][p->idx]).from);
#endif
                }
                else
                {
                    dprint("ghost arrive at axis:", n, ";no:", i,";move pruned:", p->m);
                    [[maybe_unused]] auto flag = universe[n].erase(i);
                    assert(flag);
                }
            }
        }
    }
    
#ifdef DEBUGMSG
    for(const auto [k, v]: line_axes)
    {
        dprint("line", k, "=>", "axis", v);
    }
#endif
    
    // split the search space by number of branches
    HC hc_n_lines = universe;
    integer_set singleton = {0}, non_null;
    if(new_axis < dimension)
    {
        for(index_t i = 1; i < static_cast<index_t>(axis_coords[new_axis].size()); i++)
        {
            non_null.insert(i);
        }
        // non_null = {1,2,...,number of branching moves}
        for(index_t n = new_axis; n < dimension; n++)
        {
            hc_n_lines[n] = singleton;
        }
    }
    search_space ss{{hc_n_lines}};
    for(index_t n = new_axis; n < dimension; n++)
    {
        hc_n_lines[n] = non_null;
        ss.push_front(hc_n_lines); // prefer lesser branching moves
    }
    return {
        HC_info(s, std::move(line_axes), std::move(axis_coords),
                std::move(universe), new_axis, std::move(mandatory_timelines)),
        std::move(ss)
    };
}


std::optional<point> HC_info::take_point(HC &hc) const
{
    return take_point(hc, natural_HC_ordering{});
}



std::optional<slice> HC_info::find_problem(const point &p, const HC &hc) const
{
    std::optional<slice> problem = jump_order_consistent(p, hc).or_else([this, &hc, &p]() {
        return test_present(p, hc).or_else([this, &hc, &p]() {
            return find_checks(p, hc);
        });
    });
    return problem;
}

std::optional<slice> HC_info::jump_order_consistent(const point &p, const HC &hc) const
{
    dprint("jump_order_consistent()");
    /* throughout the search, maintain the jump_map of (l, t) => new_l
    so p[new_l] is a arriving move from (l0, t0) that jumps to (l, t) which create a branch
    remember t, l are stored as the higher part of a vec4
     */
    std::map<vec4, index_t> jump_map;
    const bool c = s.get_present().c();
    
    for(index_t n = new_axis; n < dimension(); n++)
    {
        // within this loop, n is the axis for new_l
        const index_t in = p[n];
        const entry &loc = axis_coords[n][in];
        if(std::holds_alternative<null_entry>(loc))
        {
            break;
            // assumed: search space is always separated by number of branches
        }
        assert(std::holds_alternative<arriving_entry>(loc));
        const arriving_entry arr = std::get<arriving_entry>(loc);
        const vec4 from = arr.m.from, to = arr.m.to;
        /* case one: there is a branching move (l0,t0) >> (l',t') ~> new_l, but
         (l',t') is a playable board in which the corresponding played move
         is a pass */
        if(line_axes.contains(to.l()))
        {
            const index_t m = line_axes.at(to.l());
            const index_t im = p[m];
            const entry &loc2 = axis_coords[m][im];
            if(std::holds_alternative<null_entry>(loc2)
               && s.get_timeline_end(to.l()) == turn{to.t(), c})
            {
                /* ban these combinations:
                 -[m,xm] the null_entry on (l', t'); with
                 -[s] any branching move on axis n to (l', t') (which is a null_entry)
                 i.e. all moves >> (l',t') then creates branch new_l
                 */
                integer_set s;
                for(index_t i : hc[n])
                {
                    const entry &loc3 = axis_coords[n][i];
                    if(std::holds_alternative<arriving_entry>(loc3))
                    {
                        vec4 to3 = std::get<arriving_entry>(loc3).m.to;
                        if(to3.tl() == to.tl())
                        {
                            s.insert(i);
                        }
                    }
                }
                std::map<index_t, integer_set> fixed_axes {{n, s}, {m, integer_set{im}}};
                slice problem(fixed_axes);
                dprint("case one; point:", range_to_string(p));
                dprint("problem", problem.to_string());
                assert(problem.contains(p));
                return problem;
            }
        }
        /* case two: there is a branching move (l,t) -> (l',t')
         but the source (l,t) is already registered in jump_map, i.e.
         there was a jump (l0,t0) -> (l,t) ~> (new_l0,t)
         where new_l0 happens latter than new_l
         */
        vec4 critical_tl = from.tl();
        if(jump_map.contains(critical_tl))
        {
            /* ban these combinations:
                -[s1] on axis n, any move starts from (l,t); with
                -[s2] on axis for new_l0, any move goes to (l,t)
            */
            index_t axis_branch = jump_map[critical_tl];
            integer_set s1, s2;
            for(index_t i : hc[n])
            {
                const entry &l1 = axis_coords[n][i];
                if(std::holds_alternative<arriving_entry>(l1))
                {
                    vec4 from1 = std::get<arriving_entry>(l1).m.from;
                    if(from1.tl() == critical_tl)
                    {
                        s1.insert(i);
                    }
                }
            }
            for(index_t i : hc[axis_branch])
            {
                const entry &l2 = axis_coords[axis_branch][i];
                if(std::holds_alternative<arriving_entry>(l2))
                {
                    vec4 to2 = std::get<arriving_entry>(l2).m.to;
                    if(to2.tl() == critical_tl)
                    {
                        s2.insert(i);
                    }
                }
            }
            std::map<index_t, integer_set> fixed_axes {{n, s1}, {axis_branch, s2}};
            slice problem(fixed_axes);
            dprint("case two; point:", range_to_string(p));
            dprint("problem", problem.to_string());
            assert(problem.contains(p));
            return problem;
        }
        // maintain jump_map
        jump_map[to.tl()] = n;
    }
    dprint("no problem");
    return std::nullopt;
}

std::optional<slice> HC_info::test_present(const point &p, const HC &hc) const
{
    const bool c = s.get_present().c();
    const auto [l0_min, l0_max] = s.get_initial_lines_range();
    const auto [l_min, l_max] = s.get_lines_range();
    index_t branches = 0;
    while(new_axis + branches < dimension()
          && !std::holds_alternative<null_entry>(axis_coords[new_axis + branches][p[new_axis + branches]]))
    {
        ++branches;
    }
    const auto active_range = [&](index_t count) {
        const int lo = l_min - (c ? static_cast<int>(count) : 0);
        const int hi = l_max + (c ? 0 : static_cast<int>(count));
        // Extents from the initial range handle both odd and even layouts,
        // matching multiverse::calculate_active_range().
        const int radius = std::min(l0_min - lo, hi - l0_max) + 1;
        return std::pair{std::max(lo, l0_min - radius), std::min(hi, l0_max + radius)};
    };
    const auto [active_min, active_max] = active_range(branches);
    const auto branch_line = [&](index_t n) {
        const int offset = static_cast<int>(n - new_axis) + 1;
        return c ? l_min - offset : l_max + offset;
    };
    const auto branch_end = [&](const entry &loc) {
        return turn{std::get<arriving_entry>(loc).m.to.t(), c}.next_turn();
    };

    turn present = turn::max;
    std::optional<index_t> pass_axis;
    int witness_line = 0;
    for(int l = std::max(l_min, active_min); l <= std::min(l_max, active_max); ++l)
    {
        turn end = s.get_timeline_end(l);
        const auto axis = line_axes.find(l);
        const bool moved = axis != line_axes.end()
            && !std::holds_alternative<null_entry>(axis_coords[axis->second][p[axis->second]]);
        if(moved) end = end.next_turn();
        if(end <= present)
        {
            witness_line = l;
            present = end;
            pass_axis = axis != line_axes.end() && !moved
                ? std::optional<index_t>{axis->second} : std::nullopt;
        }
    }
    for(index_t n = new_axis; n < new_axis + branches; ++n)
    {
        const int l = branch_line(n);
        if(l < active_min || l > active_max) continue;
        const turn end = branch_end(axis_coords[n][p[n]]);
        if(end < present)
        {
            present = end;
            pass_axis = std::nullopt;
        }
    }
    if(present.c() != c)
    {
        return std::nullopt;
    }

    // A current-player endpoint must be an unchanged playable board.
    assert(pass_axis);
    slice problem;
    problem.fix_axis(*pass_axis, integer_set{p[*pass_axis]});
    // Other playable endpoints already start at or after this current-player
    // present: advancing one half-turn cannot cross another current-player
    // endpoint. Their move/pass choices therefore need no restriction.
    // Bound branch counts by the witness's activation and the first possible
    // activation of an earlier existing endpoint. The valid counts form an
    // interval because active ranges only grow as branches are added.
    index_t min_branches = branches, max_branches = branches;
    int widest_min = active_min, widest_max = active_max;
    for(index_t count = 0; count <= dimension() - new_axis; ++count)
    {
        const auto [active_lo, active_hi] = active_range(count);
        if(witness_line < active_lo || witness_line > active_hi) continue;
        bool safe = true;
        for(int l = std::max(l_min, active_lo); l <= std::min(l_max, active_hi); ++l)
            if(s.get_timeline_end(l) < present) safe = false;
        if(!safe) continue;
        min_branches = std::min(min_branches, count);
        max_branches = std::max(max_branches, count);
        widest_min = std::min(widest_min, active_lo);
        widest_max = std::max(widest_max, active_hi);
    }
    for(index_t n = new_axis; n < dimension(); ++n)
    {
        const index_t offset = n - new_axis;
        const int l = branch_line(n);
        integer_set allowed;
        // Identify genuine restrictions against the universe, then clip each
        // restricted axis to hc to preserve the remove_slice() contract.
        for(index_t i : universe[n])
        {
            const entry &loc = axis_coords[n][i];
            const bool passed = std::holds_alternative<null_entry>(loc);
            if(passed)
            {
                if(min_branches == 0 || offset != min_branches - 1) allowed.insert(i);
            }
            else if(offset != max_branches
                    && (l < widest_min || l > widest_max || branch_end(loc) >= present))
            {
                allowed.insert(i);
            }
        }
        // Branches form a prefix, so only the two boundary axes are needed
        // to preserve the count interval. Omit every unrestricted axis.
        if(!universe[n].is_subset_of(allowed))
        {
            allowed &= hc[n];
            problem.fix_axis(n, allowed);
        }
    }
    assert(problem.contains(p));
    return problem;
}

std::optional<slice> HC_info::find_checks(const point &p, const HC &hc) const
{
    dprint("HC_info::find_checks()");
    const bool c = s.get_present().c();
    auto [lo, hi] = s.get_lines_range();
    if (!line_axes.empty()) {
        lo = std::min(lo, line_axes.begin()->first);
        hi = std::max(hi, line_axes.rbegin()->first);
    }
    check_position newstate(s, lo, hi);
    for (const auto &[l, axis] : line_axes) {
        const entry &loc = axis_coords[axis][p[axis]];
        if (std::holds_alternative<null_entry>(loc)) continue;
        const auto [old_t, old_l] = extract_tl(loc);
        (void)old_l;
        // Branch arrivals use the axis's new line, not their old hotspot line.
        newstate.add_board(l, turn{old_t,c}.next_turn(), extract_board(loc));
    }
    // HC construction has already filtered physical checks on resulting boards.
    if(auto maybe_check = newstate.first_check(!c, false))
    {
        // there is a check
        // the slice to remove is a product of coordinates on certain axes
        full_move check = maybe_check.value();
        dprint("found check: ", check);
        assert(check.from.tl() != check.to.tl() && "physical checks should have already removed");
        auto [path, sliding_type] = get_move_path(newstate, check, !c);
        auto is_next = c ? [](int t1, int t2){
            return t1 + 1 == t2;
        }:[](int t1, int t2){
            return t1 == t2;
        };
        /* on axis for check.from.l():
         ban all moves that allows the same piece hostile piece remain in that position
         */
        slice problem;
        if(line_axes.contains(check.from.l()))
        {
            index_t n1 = line_axes.at(check.from.l());
            integer_set not_taking;
            for(index_t i : hc[n1])
            {
                const entry &loc = axis_coords[n1][i];
                /* if there isn't a new board on the same place, it won't create the same check*/
                if(std::holds_alternative<null_entry>(loc) || !is_next(extract_tl(loc).first,check.from.t()))
                {
                    continue;
                }
                const board &newboard = extract_board(loc);
                if(sliding_type)
                {
                    bitboard_t bb = c ? newboard.white() : newboard.black();
                    switch(sliding_type)
                    {
                        case 1:
                            bb &= newboard.lrook();
                            break;
                        case 2:
                            bb &= newboard.lbishop();
                            break;
                        case 3:
                            bb &= newboard.lunicorn();
                            break;
                        case 4:
                            bb &= newboard.ldragon();
                            break;
                        default:
                            assert(false && "wrong sliding type infered");
                    }
                    if(pmask(check.from.xy()) & bb)
                    {
                        dprint(n1, i, sliding_type, to_semimove(loc).lan(s));
                        dprint("axis", n1, "not taking (sliding)", i);
                        not_taking.insert(i);
                    }
                }
                else if(newboard.get_piece(check.from.xy()) == newstate.get_piece(check.from, !c))
                {
                    // non sliding pieces remains in same position
                    dprint(n1, i, sliding_type, to_semimove(loc).lan(s));
                    dprint("axis", n1, "not taking (untouched)", i);
                    not_taking.insert(i);
                }
            }
                problem.fix_axis(n1, not_taking);
        }
        /* on axis for check.to.l():
         if the board of checks.to is what just played, ban all moves that leave
         a royal piece on check.to.
         Otherwise, don't alter that axis futher.
         */
        int l2 = check.to.l();
        if(line_axes.contains(l2))
        {
            index_t n2 = line_axes.at(l2);
            const entry &loc0 = axis_coords[n2][p[n2]];
            if(std::holds_alternative<null_entry>(loc0) || !is_next(extract_tl(loc0).first, check.to.t()))
            {
                // pass to ban everything
            }
            else
            {
                integer_set expose_royal;
                for(index_t i : hc[n2])
                {
                    const entry &loc = axis_coords[n2][i];
                    const board *newboard = nullptr;
                    /* if there isn't a new board on the same place, do nothing*/
                    if(std::holds_alternative<null_entry>(loc) || !is_next(extract_tl(loc).first, check.to.t()))
                    {
                        continue;
                    }
                    else
                    {
                        newboard = &extract_board(loc);
                    }
                    bitboard_t friendly = c ? newboard->black() : newboard->white();
                    bool is_royal = pmask(check.to.xy()) & newboard->royal() & friendly;
                    if(is_royal)
                    {
                        dprint(to_semimove(loc).lan(s));
                        dprint("axis", n2, "expose royal", i);
                        expose_royal.insert(i);
                    }
                }
                problem.fix_axis(n2, expose_royal);
            }
        }
        /* on axes for checking path crossings:
         if passing board is prone to change, then ban all moves, except for
         those (physical or arriving) brings a non-royal piece blocking the
         crossed square. Otherwise, do nothing as before
         */
        for(vec4 crossed : path)
        {
//            dprint("should cross", crossed.l());
            if(line_axes.contains(crossed.l()))
            {
                index_t n = line_axes.at(crossed.l());
                const entry &loc0 = axis_coords[n][p[n]];
                if(std::holds_alternative<null_entry>(loc0) || !is_next(extract_tl(loc0).first, crossed.t()))
                {
                    // pass to ban everything
                }
                else
                {
                    bitboard_t z = pmask(crossed.xy());
                    integer_set not_blocking;
                    for(index_t i : hc[n])
                    {
                        const entry &loc = axis_coords[n][i];
                        /* if there isn't a board, then nothing pass through it*/
                        if(std::holds_alternative<null_entry>(loc) || !is_next(extract_tl(loc).first, crossed.t()))
                        {
                            continue;
                        }
                        const board &newboard = extract_board(loc);
                        /* if the very place is empty, then it is clearly not blocking*/
                        if(!(z & newboard.occupied()))
                        {
                            dprint(n, i, sliding_type, to_semimove(loc).lan(s));
                            dprint("axis", n, "not blocking (empty)", i);
                            not_blocking.insert(i);
                            continue;
                        }
                        /* on the crossed point, if a hostile piece with the same sliding type
                         of the attacking piece is placed here, it doesn't completely resolve the check
                         */
                        if(sliding_type)
                        {
                            bitboard_t bb = c ? newboard.white() : newboard.black();
                            switch(sliding_type)
                            {
                                case 1:
                                    bb &= newboard.lrook();
                                    break;
                                case 2:
                                    bb &= newboard.lbishop();
                                    break;
                                case 3:
                                    bb &= newboard.lunicorn();
                                    break;
                                case 4:
                                    bb &= newboard.ldragon();
                                    break;
                                default:
                                    assert(false && "wrong sliding type infered");
                            }
                            if(z & bb)
                            {
                                dprint(n, i, sliding_type, to_semimove(loc).lan(s));
                                dprint("axis", n, "not blocking (sliding)", i);
                                not_blocking.insert(i);
                                continue;
                            }
                        }
                        /* on the crossed point, if a friendly royal piece is there
                         it is still checking despite the path is technically blocked
                         */
                        bitboard_t friendly = c ? newboard.black() : newboard.white();
                        if(z & newboard.royal() & friendly)
                        {
                            dprint(n, i, sliding_type, to_semimove(loc).lan(s));
                            dprint("axis", n, "not blocking (expose royal)", i);
                            not_blocking.insert(i);
                            continue;
                        }
                    }
                    problem.fix_axis(n, not_blocking);
                }
            }
        }
        dprint("point:", range_to_string(p));
        dprint("problem", problem.to_string());
        assert(problem.contains(p));
        return problem;
    }
    dprint("no checks found");
    return std::nullopt;
}

// ------------------------------------------------------------



generator<moveseq> HC_info::iterative_search(search_space ss) const &
{
    return iterative_search(std::move(ss), natural_HC_ordering{});
}

// /* for debugging only (this version is more friendly with stacktracing) */
//std::vector<moveseq> HC_info::search1(search_space ss) const
//{
//    std::vector<moveseq> result;
//    adprint("begining search: ", ss.to_string());
//    while(!ss.empty())
//    {
//        HC hc = ss.back();
//        adprint("searching ", hc.to_string());
//        ss.pop_back();
//        auto pt_opt = take_point(hc);
//        if(pt_opt)
//        {
//            point pt = pt_opt.value();
//            print_range("got point: ", pt);
//            auto problem = find_problem(pt, hc);
//            if(problem)
//            {
//                adprint("found problem:", problem.value().to_string());
//                // remove the problematic slice from hc, and add the remaining to ss
//                search_space new_ss = hc.remove_slice(problem.value());
//                // make sure when a leave is removed, so is the corresponding arrive
//                adprint("removed problem, continue search:", new_ss.to_string());
//                ss.concat(std::move(new_ss));
//            }
//            else
//            {
//                adprint("point is okay, removing it from this hc");
//                result.push_back(to_action(pt));
//                search_space new_ss = hc.remove_point(pt);
//                adprint("removed point, continue search:", new_ss.to_string());
//                ss.concat(std::move(new_ss));
//            }
//        }
//        else
//        {
//            dprint("didn't secure any point in the first hypercuboid;");
//            dprint("continue searching the remaining part");
//        }
//    }
//    dprint("search space is empty; finish.");
//    return result;
//}



moveseq HC_info::to_action(const point &p) const
{
    std::vector<full_move> mvs;
    for(const auto &[l,i] : line_axes)
    {
        const entry &loc = axis_coords[i][p[i]];
        if(std::holds_alternative<physical_entry>(loc))
        {
            mvs.push_back(std::get<physical_entry>(loc).m);
        }
        else if(std::holds_alternative<arriving_entry>(loc))
        {
            mvs.push_back(std::get<arriving_entry>(loc).m);
        }
    }
    const bool c = s.get_present().c();
    if(c)
    {
        std::reverse(mvs.begin(), mvs.end());
    }
#ifdef DEBUGMSG
    dprint("HC_info::to_action: returning", mvs.size(), "moves:");
    for(const auto &mv : mvs)
    {
        dprint(" ", mv.to_string());
    }
#endif
    return mvs;
}

generator<moveseq> HC_info::stable_search(search_space ss) const &
{
    dprint("begining psearch: ", ss.to_string());
    while(!ss.empty())
    {
        HC hc = std::move(ss.back());
        dprint("searching ", hc.to_string());
        ss.pop_back();
        auto pt_opt = take_point(hc);
        if(pt_opt)
        {
            const point &pt = *pt_opt;
            dprint("got point: ", range_to_string(pt));
            auto problem = find_problem(pt, hc);
            if(problem)
            {
                const slice &problem_slice = problem.value();
                dprint("found problem:", problem.value().to_string());
                // Remove this slice from every remaining hypercuboid and re-join all pieces.
                search_space adjoined;
                for(const HC &other_hc : ss)
                {
                    search_space sstemp = other_hc.remove_slice_carefully(problem_slice);
                    adjoined.concat(std::move(sstemp));
                }
                // Keep the selected HC's pieces at the back, where search
                // will consume them before the earlier HCs.
                adjoined.concat(hc.remove_slice(problem_slice));
                // make sure when a leave is removed, so is the corresponding arrive
                dprint("removed problem from all hcs, continue search:", adjoined.to_string());
                ss = std::move(adjoined);
            }
            else
            {
                dprint("point is okay, removing it from this hc");
                co_yield to_action(pt);
                search_space new_ss = hc.remove_point(pt);
                dprint("removed point, continue search:", new_ss.to_string());
                ss.concat(std::move(new_ss));
            }
        }
        else
        {
            dprint("didn't secure any point in the first hypercuboid;");
            dprint("continue searching the remaining part");
        }
    }
    dprint("search space is empty; finish.");
    co_return;
}

generator<moveseq> HC_info::search(search_space ss) const &
{
    return search(std::move(ss), natural_HC_ordering{});
}
