#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace Graphite {

	class WorkspaceTrust;

	// Folders are trusted from the restricted-mode bar and the File menu, where the folder in question
	// is at hand; this is where trust is reviewed and taken back.
	class TrustedFoldersPage final : public SettingsPage
	{
	public:
		explicit TrustedFoldersPage(WorkspaceTrust& trust) : m_Trust(trust) {}

		std::string_view GetId() const override { return "trustedFolders"; }

		std::string_view GetTitle() const override { return "Trusted Folders"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

	private:
		void DrawRemove(const std::filesystem::path& folder);

		WorkspaceTrust& m_Trust;
	};
}
