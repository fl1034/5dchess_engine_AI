#ifndef TIMELINE_LAYOUT_H
#define TIMELINE_LAYOUT_H

#include <string>
#include <utility>

// Odd timelines have one central line (0); even timelines have -0 and +0.
class timeline_layout
{
    enum { ODD, EVEN } type;

    constexpr explicit timeline_layout(decltype(type) type) noexcept : type(type) {}

public:
    constexpr static timeline_layout odd() noexcept { return timeline_layout(ODD); }
    constexpr static timeline_layout even() noexcept { return timeline_layout(EVEN); }

    constexpr bool is_even() const noexcept { return type == EVEN; }

    // Reflect a timeline across the center, or convert a negative label's magnitude.
    constexpr int mirror(int l) const noexcept { return -l - static_cast<int>(is_even()); }

    constexpr std::pair<int, int> initial_range() const noexcept { return {mirror(0), 0}; }

    std::string pretty_l(int l) const
    {
        if(!is_even())
            return std::to_string(l);
        return std::string(l >= 0 ? "+" : "-") + std::to_string(l >= 0 ? l : mirror(l));
    }
};

#endif /* TIMELINE_LAYOUT_H */
