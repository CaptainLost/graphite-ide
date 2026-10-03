#pragma once

#include <concepts>
#include <memory>
#include <utility>

namespace Core {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
		requires std::constructible_from<T, Args...>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
		requires std::constructible_from<T, Args...>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}
