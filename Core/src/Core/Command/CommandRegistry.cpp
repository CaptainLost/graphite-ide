#include "corepch.h"
#include "Core/Command/CommandRegistry.h"

#include <utility>

namespace Core {

	Command* CommandRegistry::Add(Command command)
	{
		if (m_ById.find(command.id) != m_ById.end())
		{
			return nullptr;
		}

		m_ById.emplace(command.id, m_Commands.size());
		m_Commands.push_back(std::make_unique<Command>(std::move(command)));

		return m_Commands.back().get();
	}

	const Command* CommandRegistry::Find(std::string_view id) const
	{
		// Constructing a std::string to look up a std::string_view is the price of an
		// unordered_map keyed by string until this codebase has a heterogeneous hash.
		// These lookups happen when a menu is drawn, not in a loop over text.
		const auto found = m_ById.find(std::string(id));

		if (found == m_ById.end())
		{
			return nullptr;
		}

		return m_Commands[found->second].get();
	}

	bool CommandRegistry::Run(std::string_view id)
	{
		const Command* command = Find(id);

		if (command == nullptr || !command->run)
		{
			return false;
		}

		if (command->enabled && !command->enabled())
		{
			return false;
		}

		command->run();
		return true;
	}

	std::vector<const Command*> CommandRegistry::InGroup(std::string_view group) const
	{
		std::vector<const Command*> members;

		for (const auto& command : m_Commands)
		{
			if (command->group == group)
			{
				members.push_back(command.get());
			}
		}

		return members;
	}

	void CommandRegistry::ForEach(const std::function<void(const Command&)>& action) const
	{
		for (const auto& command : m_Commands)
		{
			action(*command);
		}
	}
}