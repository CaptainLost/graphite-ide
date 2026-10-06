#include "corepch.h"
#include "Core/Command/MenuItem.h"

#include <utility>

namespace Core {

	MenuItem MenuItem::Command(std::string commandId)
	{
		MenuItem item;
		item.m_Kind = Kind::Command;
		item.m_Text = std::move(commandId);

		return item;
	}

	MenuItem MenuItem::Separator()
	{
		MenuItem item;
		item.m_Kind = Kind::Separator;

		return item;
	}

	MenuItem MenuItem::Section(std::string heading)
	{
		MenuItem item;
		item.m_Kind = Kind::Section;
		item.m_Text = std::move(heading);

		return item;
	}

	MenuItem MenuItem::Submenu(std::string label, std::vector<MenuItem> children)
	{
		MenuItem item;
		item.m_Kind = Kind::Submenu;
		item.m_Text = std::move(label);
		item.m_Children = std::move(children);

		return item;
	}

	MenuItem MenuItem::Group(std::string commandGroup)
	{
		MenuItem item;
		item.m_Kind = Kind::Group;
		item.m_Text = std::move(commandGroup);

		return item;
	}
}
