#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Command/CommandRegistry.h"

#include <utility>

namespace Core {

	Command* CommandRegistry::Add(Command command)
	{
		if (m_ById.find(command.Id) != m_ById.end())
		{
			return nullptr;
		}

		m_ById.emplace(command.Id, m_Commands.size());
		m_Commands.push_back(CreateScope<Command>(std::move(command)));

		return m_Commands.back().get();
	}

	const Command* CommandRegistry::Find(std::string_view id) const
	{
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

		if (command == nullptr || !command->Run)
		{
			return false;
		}

		if (command->Enabled && !command->Enabled())
		{
			return false;
		}

		command->Run();
		return true;
	}

	std::vector<const Command*> CommandRegistry::InGroup(std::string_view group) const
	{
		std::vector<const Command*> members;

		for (const auto& command : m_Commands)
		{
			if (command->Group == group)
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