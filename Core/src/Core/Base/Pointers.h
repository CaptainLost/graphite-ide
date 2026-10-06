#pragma once

#include <concepts>
#include <memory>
#include <utility>

namespace Core {

	template<typename Object>
	using Scope = std::unique_ptr<Object>;

	template<typename Object, typename... Arguments>
		requires std::constructible_from<Object, Arguments...>
	constexpr Scope<Object> CreateScope(Arguments&&... arguments)
	{
		return std::make_unique<Object>(std::forward<Arguments>(arguments)...);
	}

	template<typename Object>
	using Ref = std::shared_ptr<Object>;

	template<typename Object, typename... Arguments>
		requires std::constructible_from<Object, Arguments...>
	constexpr Ref<Object> CreateRef(Arguments&&... arguments)
	{
		return std::make_shared<Object>(std::forward<Arguments>(arguments)...);
	}
}
