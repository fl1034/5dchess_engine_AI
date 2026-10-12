#include "rollout_policy.h"
#include "hypercuboid.h"
#include "utils.h"

#include <cmath>
#include <stdexcept>

namespace
{
template<HCOrdering Order>
generator<moveseq> mixed_search(
    const HC_info &hc_info,
    search_space space,
    Order order,
    std::stop_token stop_token
)
{
    constexpr std::size_t iterative_problem_limit = 100;
    std::size_t num_problems = 0;
    while(!space.empty() && num_problems < iterative_problem_limit)
    {
        if(stop_token.stop_requested())
        {
            co_return;
        }

        HC hc = std::move(space.back());
        space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point)
        {
            continue;
        }

        if(auto problem = hc_info.find_problem(*point, hc))
        {
            ++num_problems;
            space.concat(hc.remove_slice(*problem));
        }
        else
        {
            co_yield hc_info.to_action(*point);
            space.concat(hc.remove_point(*point));
        }
    }

    while(!space.empty())
    {
        if(stop_token.stop_requested())
        {
            co_return;
        }

        HC hc = std::move(space.back());
        space.pop_back();
        auto point = hc_info.take_point(hc, order);
        if(!point)
        {
            continue;
        }

        if(auto problem = hc_info.find_problem(*point, hc))
        {
            space.push_back(std::move(hc));
            space.remove_slice_backwards(*problem, true, 10);
        }
        else
        {
            co_yield hc_info.to_action(*point);
            space.concat(hc.remove_point(*point));
        }
    }
}
} /* anonymous namespace */

std::optional<moveseq> random_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    auto order = rng ? random_HC_ordering(info.get_universe(), *rng)
                     : random_HC_ordering(info.get_universe());
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}

std::optional<reward_t<rollout_details>> evaluate_zero_position(
    state s, std::stop_token stop)
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    auto moves = mixed_search(
        info, std::move(space), natural_HC_ordering{}, stop).first();
    if(stop.stop_requested()) return std::nullopt;

    rollout_cutoff_evaluation evaluation;
    if(!moves)
    {
        std::optional<bool> winner;
        if(s.get_mate_type() == mate_type::CHECKMATE)
            winner = !s.get_present().second;
        return evaluation.mate_reward(winner, 0);
    }
    return evaluation.cutoff_reward(std::move(s), 0, stop, nullptr);
}

void weighted_action_selection::set_temperature(float value)
{
    if(!(value > 0.0f) || !std::isfinite(value))
    {
        throw std::invalid_argument(
            "rollout weighting temperature must be positive and finite");
    }
    temperature->store(value);
}

std::optional<moveseq> weighted_action_selection::operator()(
    const state &s, std::stop_token stop, std::mt19937 *rng) const
{
    if(stop.stop_requested()) return std::nullopt;
    auto [info, space] = HC_info::build_HC(s);
    const float weight_temperature = get_temperature();
    move_evaluator evaluator(info, weights);
    std::vector<std::vector<float>> coordinate_weights(info.get_universe().dimension());
    for(index_t axis = 0; axis < info.get_universe().dimension(); ++axis)
    {
        auto &axis_weights = coordinate_weights[axis];
        axis_weights.reserve(info.get_universe()[axis].size());
        for(index_t coordinate : info.get_universe()[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            axis_weights.push_back(std::exp(
                evaluator.score(axis, coordinate) / weight_temperature));
        }
    }
    auto order = rng
        ? weighted_HC_ordering(
            info.get_universe(), std::move(coordinate_weights), *rng)
        : weighted_HC_ordering(
            info.get_universe(), std::move(coordinate_weights));
    return mixed_search(info, std::move(space), std::move(order), stop).first();
}
