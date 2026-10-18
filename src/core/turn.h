#ifndef TURN_H
#define TURN_H
#include <iostream>
#include <string>
#include <utility>

using turn_t = std::pair<int,bool>;

inline turn_t next_turn(turn_t t) noexcept
{
    auto [n,c] = t;
    int v = (n<<1 | static_cast<int>(c)) + 1;
    return std::make_pair(v>>1, v&1);
}

inline turn_t previous_turn(turn_t t) noexcept
{
    auto [n,c] = t;
    int v = (n<<1 | static_cast<int>(c)) - 1;
    return std::make_pair(v>>1, v&1);
}

inline std::string show_turn(turn_t t)
{
    auto [n,c] = t;
    return std::to_string(n) + (c ? "b": "w");
}

enum class mate_type {NONE, CHECKMATE, SOFTMATE, STALEMATE};

#endif /* TURN_H */
