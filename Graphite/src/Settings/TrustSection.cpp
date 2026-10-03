#include "Settings/TrustSection.h"

#include "Workspace/WorkspaceTrust.h"

#include "Core/FileSystem/FileSystem.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	void TrustSection::OnImGuiRender()
	{
		ImGui::SeparatorText("Trusted folders");

		ImGui::TextWrapped("The language server and the background analysis run only in these folders and "
						   "the folders inside them. Any other folder opens in restricted mode.");
		ImGui::Spacing();

		if (m_Trust.GetEntries().empty())
		{
			ImGui::TextDisabled("No folder is trusted yet.");
			return;
		}

		std::optional<std::filesystem::path> revoked;

		for (const std::filesystem::path& entry : m_Trust.GetEntries())
		{
			const std::string text = Core::ToUtf8(entry);

			ImGui::PushID(text.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text.c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Remove"))
			{
				revoked = entry;
			}

			ImGui::PopID();
		}

		// After the loop: Revoke changes the list being walked.
		if (revoked)
		{
			m_Trust.Revoke(*revoked);
		}
	}
}
