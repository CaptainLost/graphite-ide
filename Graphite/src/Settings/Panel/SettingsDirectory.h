#pragma once

#include "Settings/Panel/SettingsPage.h"

#include "Core/Base/Base.h"

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct SettingsGroup
	{
		std::string Title;
		std::vector<Core::Scope<SettingsPage>> Pages;
	};

	// The pages of one navigation list, in groups. A group with no title is drawn as a divider.
	class SettingsDirectory
	{
	public:
		SettingsDirectory() = default;

		explicit SettingsDirectory(std::vector<SettingsGroup> groups);

		const std::vector<SettingsGroup>& GetGroups() const { return m_Groups; }

		SettingsPage* Find(std::string_view id) const;

		SettingsPage* First() const;

		bool IsEmpty() const;

	private:
		std::vector<SettingsGroup> m_Groups;
	};

	// Every page in the order the lists show them, each page's own pages straight after it.
	std::vector<SettingsPage*> CollectPages(const SettingsDirectory& directory);
}
