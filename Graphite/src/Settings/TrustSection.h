#pragma once

#include "Settings/SettingsSection.h"

namespace Graphite {

	class WorkspaceTrust;

	// Lists the trusted folders, each with a way to stop trusting it. Folders are trusted from
	// the restricted-mode bar and the File menu, where the folder in question is at hand; this
	// is where trust is reviewed and taken back.
	class TrustSection final : public SettingsSection
	{
	public:
		explicit TrustSection(WorkspaceTrust& trust) : m_Trust(trust) {}

		std::string_view GetTitle() const override { return "Trusted Folders"; }

		void OnImGuiRender() override;

	private:
		WorkspaceTrust& m_Trust;
	};
}
