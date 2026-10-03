#pragma once

#include "Settings/SettingsSection.h"

#include <string_view>

namespace Graphite {

	class AppSettings;

	class InterfaceSection final : public SettingsSection
	{
	public:
		explicit InterfaceSection(AppSettings& settings);

		std::string_view GetTitle() const override { return "Interface"; }

		void OnImGuiRender() override;

	private:
		AppSettings& m_Settings;
	};
}
