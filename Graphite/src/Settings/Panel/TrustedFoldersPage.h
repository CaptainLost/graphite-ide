#pragma once

#include "Settings/Panel/SettingsPage.h"

#include "Core/Platform/AsyncDialog.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace Graphite {

	class WorkspaceTrust;

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
		void DrawAdd();

		WorkspaceTrust& m_Trust;

		// The folder picker Add opens, which answers on a later frame.
		Core::AsyncDialog m_AddDialog;
	};
}
