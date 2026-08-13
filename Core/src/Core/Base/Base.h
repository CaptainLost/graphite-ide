#pragma once

#include "Core/Base/PlatformDetection.h"

#include <memory>
#include <utility>

#define BIT(x) (1 << (x))

// Binds a member function as an event handler without spelling out the signature at the
// call site, which is what lets EventDispatcher deduce the event type from the handler.
#define CORE_BIND_EVENT_FN(fn)                                  \
	[this](auto&&... args) -> decltype(auto) {                  \
		return this->fn(std::forward<decltype(args)>(args)...); \
	}

namespace Core {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}
// Included last, and it has to be: Log.h uses Ref, so the alias must already be declared
// when it arrives. Both are included at all because the assertion and logging macros are
// part of what including this header is expected to bring in.
#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
