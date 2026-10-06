#pragma once

#include <concepts>
#include <utility>

namespace Core {

	template<std::invocable Action>
	class ScopeExit
	{
	public:
		explicit ScopeExit(Action action) : m_Action(std::move(action)) {}

		~ScopeExit() { m_Action(); }

		ScopeExit(const ScopeExit&) = delete;
		ScopeExit& operator=(const ScopeExit&) = delete;
		ScopeExit(ScopeExit&&) = delete;
		ScopeExit& operator=(ScopeExit&&) = delete;

	private:
		Action m_Action;
	};
}
