#include "Git/History/CommitDetailsView.h"

#include "Core/Base/Strings.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/LoadingState.h"

#include "Git/History/CommitLabels.h"
#include "Git/History/CommitRows.h"
#include "Git/Window/WindowBar.h"
#include "UI/SplitResults.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

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

		constexpr float kLabelWidthPixels = 62.0f;
		constexpr float kChipGapPixels = 4.0f;
		constexpr ImU32 kChipFillAlphaByte = 0x38u;
		const char* const kPendingText = "…";
		const char* const kBroughtFailed = "Cannot read the commits this merge brought: ";

		// Pieces laid out left to right in the room that is left, each on the next line when it does not fit.
		class ChipFlow
		{
		public:
			ChipFlow()
				: m_IndentX(ImGui::GetCursorPosX()),
				  m_Right(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x)
			{
			}

			void Place(float width)
			{
				if (!m_First)
				{
					const float gap = WindowBarEm(kChipGapPixels);

					if (m_PreviousRight + gap + width <= m_Right)
					{
						ImGui::SameLine(0.0f, gap);
					}
					else
					{
						ImGui::SetCursorPosX(m_IndentX);
					}
				}

				m_First = false;
			}

			void Placed() { m_PreviousRight = ImGui::GetItemRectMax().x; }

		private:
			float m_IndentX;
			float m_Right;
			float m_PreviousRight = 0.0f;
			bool m_First = true;
		};

		void DrawChip(ChipFlow& flow, const std::string& text, ImU32 ink)
		{
			const float padding = ImGui::GetStyle().ItemInnerSpacing.x;
			const ImVec2 size(ImGui::CalcTextSize(text.c_str()).x + padding * 2.0f, ImGui::GetTextLineHeight());

			flow.Place(size.x);

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
									Core::WithAlphaReplaced(ink, kChipFillAlphaByte), ImGui::GetStyle().FrameRounding);
			drawList->AddText(ImVec2(origin.x + padding, origin.y), ink, text.c_str());
			ImGui::Dummy(size);
			flow.Placed();
		}

		// WARNING: The line is drawn whatever the answer, so the pane does not change height when it lands.
		void InBasesLine(const InBases& bases, const GitPalette& palette)
		{
			const float labelStartX = ImGui::GetCursorPosX();

			ImGui::TextDisabled("In bases");
			ImGui::SameLine(labelStartX + WindowBarEm(kLabelWidthPixels));

			if (bases.Pending)
			{
				ImGui::TextDisabled("%s", kPendingText);
				return;
			}

			if (bases.Unknown.has_value())
			{
				ImGui::TextDisabled("unknown");

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", bases.Unknown->c_str());
				}

				return;
			}

			if (bases.Holding.empty())
			{
				ImGui::TextDisabled("in no base yet");
				return;
			}

			ChipFlow flow;

			for (const std::string& base : bases.Holding)
			{
				DrawChip(flow, std::string(ICON_LC_CHECK " ") + base, palette.GutterAdded);
			}
		}

		void RefsLine(const std::vector<CommitRef>& refs, const GitPalette& palette)
		{
			if (refs.empty())
			{
				return;
			}

			ChipFlow flow;

			for (const CommitRef& ref : refs)
			{
				flow.Place(RefPillWidth(ref));
				DrawRefPill(ref, palette);
				flow.Placed();
			}
		}

		std::optional<Core::Oid> BroughtList(const BroughtDetails& brought, std::chrono::sys_seconds now)
		{
			std::optional<Core::Oid> chosen;

			ImGui::Spacing();

			if (brought.Failure.has_value())
			{
				ImGui::PushTextWrapPos(kWrapAtPaneEdge);
				ImGui::TextDisabled("%s%s", kBroughtFailed, brought.Failure->c_str());
				ImGui::PopTextWrapPos();
				return chosen;
			}

			if (!brought.Page)
			{
				Core::LoadingLine("Reading the commits this merge brought");
				return chosen;
			}

			ImGui::TextDisabled("Brought %s", DescribeBrought(*brought.Page).c_str());

			for (const Core::CommitSummary& commit : brought.Page->Commits)
			{
				ImGui::PushID(commit.Id.ToHex().c_str());

				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				const float height = ImGui::GetTextLineHeight();

				if (ImGui::Selectable("##brought", false, ImGuiSelectableFlags_None, ImVec2(0.0f, height)))
				{
					chosen = commit.Id;
				}

				ImDrawList* const drawList = ImGui::GetWindowDrawList();
				const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
				const std::string age = RelativeAge(commit.Author.When, now);
				const float ageWidth = ImGui::CalcTextSize(age.c_str()).x;
				const float gap = ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;

				Core::PushCodeFont(Core::kInterfaceFontSize);
				const std::string shortHash = commit.Id.ShortHex();
				const float hashWidth = ImGui::CalcTextSize(shortHash.c_str()).x;
				drawList->AddText(origin, dim, shortHash.c_str());
				ImGui::PopFont();

				const float subjectLeft = origin.x + hashWidth + gap;
				const float subjectRight = origin.x + width - ageWidth - gap;

				if (subjectRight > subjectLeft)
				{
					ImGui::RenderTextClipped(ImVec2(subjectLeft, origin.y), ImVec2(subjectRight, origin.y + height),
											 commit.Summary.c_str(), nullptr, nullptr);
				}

				drawList->AddText(ImVec2(origin.x + width - ageWidth, origin.y), dim, age.c_str());

				ImGui::PopID();
			}

			return chosen;
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

	CommitDetailsOutcome RenderCommitDetails(const Core::CommitSummary& commit,
											 const std::optional<DiffStats>& stats, bool diffFailed, const std::chrono::time_zone* zone,
											 const GitPalette& palette, const CommitDetailsContent& content)
	{
		CommitDetailsOutcome outcome;
		const std::chrono::sys_seconds now =
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		HashRow(commit.Id);

		outcome.Closed = DrawPaneClose();

		SignatureRow(kNoPrefix, commit.Author, now, zone);

		if (ShowsCommitter(commit))
		{
			SignatureRow("committed by", commit.Committer, now, zone);
		}

		InBasesLine(content.Bases, palette);
		RefsLine(content.Refs, palette);

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

		if (content.Brought.has_value())
		{
			outcome.ChosenBrought = BroughtList(*content.Brought, now);
		}

		return outcome;
	}
}
