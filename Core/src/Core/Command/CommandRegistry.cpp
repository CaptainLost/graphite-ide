#include "corepch.h"

#include "Core/Command/CommandRegistry.h"

#include "Core/Base/Base.h"

#include <utility>

namespace Core {

	namespace {
		constexpr ShortcutScope kUnregisteredCommandScope = ShortcutScope::Global;
	}

	Command* CommandRegistry::Add(Command command)
	{
		const bool idAlreadyTaken = m_CommandIndexById.contains(command.Id);
		if (idAlreadyTaken)
		{
			return nullptr;
		}

		const std::size_t newCommandIndex = m_Commands.size();
		m_CommandIndexById.emplace(command.Id, newCommandIndex);
		m_Commands.push_back(CreateScope<Command>(std::move(command)));

		return m_Commands.back().get();
	}

	const Command* CommandRegistry::Find(std::string_view id) const
	{
		const std::unordered_map<std::string, std::size_t>::const_iterator entry = m_CommandIndexById.find(std::string(id));

		if (entry == m_CommandIndexById.end())
		{
			return nullptr;
		}

		const auto& [commandId, commandIndex] = *entry;

		return m_Commands[commandIndex].get();
	}

	ShortcutScope CommandRegistry::KeyScopeOf(std::string_view commandId) const
	{
		const Command* command = Find(commandId);

		if (command == nullptr)
		{
			return kUnregisteredCommandScope;
		}

		return command->KeyScope;
	}

	bool CommandRegistry::Run(std::string_view id)
	{
		const Command* command = Find(id);

		if (command == nullptr || !command->Run)
		{
			return false;
		}

		if (!IsCommandEnabled(*command))
		{
			return false;
		}

		command->Run();
		return true;
	}

	bool CommandRegistry::RunFromMenu(std::string_view id)
	{
		const Command* command = Find(id);

		const bool hasMenuVariant = command != nullptr && command->RunFromMenu;
		if (!hasMenuVariant)
		{
			return Run(id);
		}

		if (!IsCommandEnabled(*command))
		{
			return false;
		}

		command->RunFromMenu();
		return true;
	}

	std::vector<const Command*> CommandRegistry::InGroup(std::string_view group) const
	{
		std::vector<const Command*> members;

		for (const Scope<Command>& command : m_Commands)
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
		for (const Scope<Command>& command : m_Commands)
		{
			action(*command);
		}
	}
}
