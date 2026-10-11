#include "Settings/Panel/SettingsDirectory.h"

#include <utility>

namespace Graphite {

	SettingsDirectory::SettingsDirectory(std::vector<SettingsGroup> groups) : m_Groups(std::move(groups))
	{
	}

	SettingsPage* SettingsDirectory::Find(std::string_view id) const
	{
		for (const SettingsGroup& group : m_Groups)
		{
			for (const Core::Scope<SettingsPage>& page : group.Pages)
			{
				if (page->GetId() == id)
				{
					return page.get();
				}
			}
		}

		return nullptr;
	}

	SettingsPage* SettingsDirectory::First() const
	{
		for (const SettingsGroup& group : m_Groups)
		{
			if (!group.Pages.empty())
			{
				return group.Pages.front().get();
			}
		}

		return nullptr;
	}

	bool SettingsDirectory::IsEmpty() const
	{
		return First() == nullptr;
	}

	std::vector<SettingsPage*> CollectPages(const SettingsDirectory& directory)
	{
		std::vector<SettingsPage*> pages;

		for (const SettingsGroup& group : directory.GetGroups())
		{
			for (const Core::Scope<SettingsPage>& page : group.Pages)
			{
				pages.push_back(page.get());

				if (const SettingsDirectory* children = page->GetChildren(); children != nullptr)
				{
					const std::vector<SettingsPage*> childPages = CollectPages(*children);
					pages.insert(pages.end(), childPages.begin(), childPages.end());
				}
			}
		}

		return pages;
	}
}
