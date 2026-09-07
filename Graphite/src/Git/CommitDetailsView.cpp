#include "Git/CommitDetailsView.h"

#include "Git/CommitLabels.h"
#include "UI/SplitResults.h"

#include <imgui.h>

#include <chrono>
#include <format>
#include <string>

namespace Graphite {

	namespace {

		// A read-only box rather than plain text: the full hash is the one thing on this
		// pane a reader wants in their clipboard, and a Selectable would give them the row
		// rather than the forty digits. Read-only, so the box cannot be typed into and the
		// value it shows is always the commit's own.
		void HashRow(const Core::Oid& id)
		{
			std::string hex = id.ToHex();

			ImGui::SetNextItemWidth(ImGui::CalcTextSize(hex.c_str()).x +
									ImGui::GetStyle().FramePadding.x * 2.0f);

			// The buffer is the string's own storage and is never written to, which is what
			// ReadOnly guarantees. Size includes the terminator ImGui expects to find.
			ImGui::InputText("##hash", hex.data(), hex.size() + 1,
							 ImGuiInputTextFlags_ReadOnly);
		}

		void SignatureRow(const char* prefix, const Core::Signature& who,
						  std::chrono::sys_seconds now, std::chrono::minutes offset)
		{
			const std::string when = std::format("{}  ·  {}", RelativeAge(who.When, now),
												 FormattedTimestamp(who.When, offset));

			if (prefix != nullptr)
			{
				ImGui::TextDisabled("%s", prefix);
				ImGui::SameLine();
			}

			// The name and address together: two accounts under one display name is
			// ordinary, and the address is what tells them apart.
			ImGui::Text("%s <%s>", who.Name.c_str(), who.Email.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", when.c_str());
		}
	}

	bool RenderCommitDetails(const Core::CommitSummary& commit,
							 const std::optional<DiffStats>& stats, std::chrono::minutes offset,
							 const GitPalette& palette)
	{
		const auto now =
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		HashRow(commit.Id);

		// On the hash's own line, at the pane's right corner — the same place the diff pane
		// puts its close, so one gesture dismisses whichever of the two is open.
		const bool closed = DrawPaneClose();

		SignatureRow(nullptr, commit.Author, now, offset);

		if (ShowsCommitter(commit))
		{
			SignatureRow("committed by", commit.Committer, now, offset);
		}

		if (!commit.Parents.empty())
		{
			ImGui::TextDisabled("parents");

			for (const Core::Oid& parent : commit.Parents)
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(parent.ShortHex().c_str());
			}
		}
		else
		{
			// A root commit has none, and saying so is shorter than leaving the reader to
			// wonder whether the line failed to draw.
			ImGui::TextDisabled("no parents — this is a root commit");
		}

		if (stats.has_value())
		{
			ImGui::Text("%zu %s", stats->Files, stats->Files == 1 ? "file" : "files");
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterAdded), "+%zu", stats->Added);
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterRemoved), "-%zu", stats->Removed);
		}
		else
		{
			// One line tall either way, so the pane does not jump when the diff lands.
			ImGui::TextDisabled("counting changes…");
		}

		ImGui::Separator();

		// Wrapped to the pane rather than clipped: a commit message is prose, and the part
		// worth reading is often past the width of whatever the reader dragged the split to.
		ImGui::PushTextWrapPos(0.0f);
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
