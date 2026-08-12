#include "corepch.h"
#include "Core/Command/MenuItem.h"

#include <utility>

namespace Core
{
    MenuItem MenuItem::Command(std::string id)
    {
        MenuItem item;
        item.m_kind = Kind::Command;
        item.m_text = std::move(id);

        return item;
    }

    MenuItem MenuItem::Separator()
    {
        MenuItem item;
        item.m_kind = Kind::Separator;

        return item;
    }

    MenuItem MenuItem::Submenu(std::string label, std::vector<MenuItem> children)
    {
        MenuItem item;
        item.m_kind = Kind::Submenu;
        item.m_text = std::move(label);
        item.m_children = std::move(children);

        return item;
    }

    MenuItem MenuItem::Group(std::string group)
    {
        MenuItem item;
        item.m_kind = Kind::Group;
        item.m_text = std::move(group);

        return item;
    }
}
