#pragma once

#include "Core/Base/PlatformDetection.h"

#include <memory>
#include <utility>

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
