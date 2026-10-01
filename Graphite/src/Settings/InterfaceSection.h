#pragma once

#include "Settings/SettingsSection.h"

namespace Graphite {

	class InterfaceSection final : public SettingsSection
	{
	public:
		InterfaceSection();

		std::string_view GetTitle() const override { return "Interface"; }

		void OnImGuiRender() override;

	private:
		// Read once rather than every frame: graphite.json is the record, this is what the
		// combo shows, and the two only part when the file is edited behind the IDE's back.
		int m_ScalePercent;
	};
}
