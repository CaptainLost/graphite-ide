#pragma once

#include <memory>
#include <utility>

#define CORE_EXPAND_MACRO(x) x
#define CORE_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << (x))

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
// Included last, as Hazel's Base.h does: the assertion and logging macros are part of
// what including Base.h is expected to bring in, and neither header needs anything above.
#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
