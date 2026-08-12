#pragma once

#include <memory>
#include <utility>

#define CORE_EXPAND_MACRO(x) x
#define CORE_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << (x))

/// Binds a member function as a callable that forwards whatever it is handed. Used
/// where an interface wants a free-standing callback and the handler is a method:
/// the lambda captures `this` rather than the caller having to carry it separately.
#define CORE_BIND_EVENT_FN(fn)                                                                     \
    [this](auto&&... args) -> decltype(auto)                                                       \
    {                                                                                              \
        return this->fn(std::forward<decltype(args)>(args)...);                                    \
    }

namespace Core
{
    /// Sole ownership. The engine never says `new`: a Scope is created through
    /// CreateScope and released when it goes out of scope.
    template<typename T>
    using Scope = std::unique_ptr<T>;

    template<typename T, typename... Args>
    constexpr Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    /// Shared ownership, for the few things whose lifetime genuinely has more than
    /// one owner — a log sink read by a panel and written by the logger, say.
    template<typename T>
    using Ref = std::shared_ptr<T>;

    template<typename T, typename... Args>
    constexpr Ref<T> CreateRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}

// Included last, as Hazel's Base.h does: the assertion and logging macros are part of
// what including Base.h is expected to bring in, and neither header needs anything above.
#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
