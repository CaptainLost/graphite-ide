#include "corepch.h"
#include "Core/Command/MenuItem.h"

#include <utility>

namespace Core
{
    MenuItem MenuItem::Command(std::string id)
    {
        MenuItem item;
        item.m_Kind = Kind::Command;
        item.m_Text = std::move(id);

        return item;
    }

    MenuItem MenuItem::Separator()
    {
        MenuItem item;
        item.m_Kind = Kind::Separator;

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

    MenuItem MenuItem::Group(std::string group)
    {
        MenuItem item;
        item.m_Kind = Kind::Group;
        item.m_Text = std::move(group);

        return item;
    }
}
