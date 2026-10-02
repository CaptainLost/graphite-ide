#pragma once

#include <concepts>
#include <utility>

namespace Core {

	// Runs an action when the scope holding it ends, however it ends - including by a return
	// added later. Neither copyable nor movable: a copy would run the action a second time.
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
