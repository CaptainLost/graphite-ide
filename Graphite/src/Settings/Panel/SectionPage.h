#pragma once

#include "Settings/Panel/SettingsPage.h"
#include "Settings/SettingsSection.h"

#include "Core/Base/Base.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	// Shows a settings section that is not a page yet as one block filling its page.
	class SectionPage final : public SettingsPage
	{
	public:
		SectionPage(std::string id, const char* icon, Core::Scope<SettingsSection> section)
			: m_Id(std::move(id)), m_Icon(icon), m_Section(std::move(section))
		{
		}

		std::string_view GetId() const override { return m_Id; }

		std::string_view GetTitle() const override { return m_Section->GetTitle(); }

		const char* GetIcon() const override { return m_Icon; }

		std::vector<SettingsCard> Describe() override
		{
			SettingsCard card;
			card.Rows.push_back(SettingsRow{.Title = std::string{GetTitle()}, .DrawBlock = [this] { m_Section->OnImGuiRender(); }, .FillsHeight = true});
			return {card};
		}

		void OnEvent(Core::Event& event) override { m_Section->OnEvent(event); }

		void OnDeactivated() override { m_Section->OnDeactivated(); }

	private:
		std::string m_Id;
		const char* m_Icon;
		Core::Scope<SettingsSection> m_Section;
	};
}
