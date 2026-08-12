#pragma once

#include <cstdint>

namespace core
{
    /// What produces a chord. The wheel is here rather than in a separate type because
    /// Ctrl+wheel is already one of the ways the editor zooms, and a binding that could
    /// not hold it would leave that gesture outside every rebinding the user can make.
    enum class Trigger : std::uint8_t
    {
        Key,
        WheelUp,
        WheelDown
    };
}
