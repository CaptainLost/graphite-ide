#pragma once

namespace Core
{
    /// Blocks copying while leaving move semantics available to derived classes.
    class NonCopyable
    {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;

        NonCopyable(const NonCopyable&) = delete;
        NonCopyable& operator=(const NonCopyable&) = delete;

        NonCopyable(NonCopyable&&) = default;
        NonCopyable& operator=(NonCopyable&&) = default;
    };
}
