#include "Git/History/CommitDetailsView.h"

#include "Core/Base/Strings.h"
#include "Core/ImGui/FontSet.h"

#include "Git/History/CommitLabels.h"
#include "UI/SplitResults.h"

#include <imgui.h>

#include <chrono>
#include <format>
#include <string>

namespace Graphite {

	namespace {

		constexpr float kFramePaddingSides = 2.0f;
		constexpr float kWrapAtPaneEdge = 0.0f;
		const char* const kNoPrefix = nullptr;

		// WARNING: The hash is a read-only input box so it can be selected and copied; the buffer is the string's own storage and ReadOnly guarantees it is never written.
		void HashRow(const Core::Oid& commitId)
		{
			std::string hex = commitId.ToHex();

			Core::PushCodeFont(Core::kInterfaceFontSize);
			const std::size_t bufferSizeWithTerminator = hex.size() + 1;

			ImGui::SetNextItemWidth(ImGui::CalcTextSize(hex.c_str()).x +
									ImGui::GetStyle().FramePadding.x * kFramePaddingSides);

			ImGui::InputText("##hash", hex.data(), bufferSizeWithTerminator,
							 ImGuiInputTextFlags_ReadOnly);

			ImGui::PopFont();
		}

		void SignatureRow(const char* prefix, const Core::Signature& signature,
						  std::chrono::sys_seconds now, const std::chrono::time_zone* zone)
		{
			const std::string age = std::format("{}  ·  {}", RelativeAge(signature.When, now),
												FormattedTimestamp(signature.When, zone));

			if (prefix != nullptr)
			{
				ImGui::TextDisabled("%s", prefix);
				ImGui::SameLine();
			}

			ImGui::Text("%s <%s>", signature.Name.c_str(), signature.Email.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", age.c_str());
		}
	}

	bool RenderCommitDetails(const Core::CommitSummary& commit,
							 const std::optional<DiffStats>& stats, bool diffFailed, const std::chrono::time_zone* zone,
							 const GitPalette& palette)
	{
		const std::chrono::sys_seconds now =
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		HashRow(commit.Id);

		const bool closed = DrawPaneClose();

		SignatureRow(kNoPrefix, commit.Author, now, zone);

		if (ShowsCommitter(commit))
		{
			SignatureRow("committed by", commit.Committer, now, zone);
		}

		if (!commit.Parents.empty())
		{
			ImGui::TextDisabled("parents");

			for (const Core::Oid& parent : commit.Parents)
			{
				ImGui::SameLine();
				Core::PushCodeFont(Core::kInterfaceFontSize);
				ImGui::TextUnformatted(parent.ShortHex().c_str());
				ImGui::PopFont();
			}
		}
		else
		{
			ImGui::TextDisabled("no parents — this is a root commit");
		}

		if (stats.has_value())
		{
			ImGui::TextUnformatted(Core::Counted(stats->Files, "file").c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterAdded), "+%zu", stats->Added);
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterRemoved), "-%zu", stats->Removed);
		}
		else
		{
			ImGui::TextDisabled("%s", UncountedChangesText(diffFailed).c_str());
		}

		ImGui::Separator();

		ImGui::PushTextWrapPos(kWrapAtPaneEdge);
		ImGui::TextUnformatted(commit.Summary.c_str());

		if (!commit.Body.empty())
		{
			ImGui::Spacing();
			ImGui::TextUnformatted(commit.Body.c_str());
		}

		ImGui::PopTextWrapPos();

		return closed;
	}
}
