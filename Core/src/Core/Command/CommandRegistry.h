#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Command/Command.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core {

	class CommandRegistry final : NonCopyable
	{
	public:
		Command* Add(Command command);

		const Command* Find(std::string_view id) const;

		bool Run(std::string_view id);

		// Run as a menu item runs it: the command's RunFromMenu when it has one, else Run.
		// Gated by Enabled the same way.
		bool RunFromMenu(std::string_view id);

		std::vector<const Command*> InGroup(std::string_view group) const;

		void ForEach(const std::function<void(const Command&)>& action) const;

	private:
		std::vector<Scope<Command>> m_Commands;
		std::unordered_map<std::string, std::size_t> m_ById;
	};
}