#include "Git/Work/WorkView.h"

#include "Git/Branches/BranchAge.h"
#include "Git/Work/CardText.h"
#include "Git/Work/ChipRow.h"
#include "UI/FittedText.h"
#include "UI/Lists/Chevron.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/Theme.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace Graphite {

	namespace {

		// A card is this many font sizes wide at least; narrower windows drop columns.
		constexpr float kCardWidthInFontSizes = 17.0f;
		constexpr float kCodeLineScale = 1.1f;
		constexpr int kSubjectLines = 2;
		constexpr int kChipLines = 2;
		constexpr float kProgressBarHeight = 4.0f;
		constexpr float kHalf = 0.5f;
		constexpr std::size_t kCardItemCount = 7;

		float CardHeight()
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float line = ImGui::GetTextLineHeight();
			const float chips = static_cast<float>(kChipLines) * ChipHeight() + style.ItemSpacing.y;

			return 2.0f * style.WindowPadding.y + line + line * kCodeLineScale +
				static_cast<float>(kSubjectLines) * line + chips + line + kProgressBarHeight + ImGui::GetFrameHeight() +
				static_cast<float>(kCardItemCount) * style.ItemSpacing.y;
		}

		std::size_t ColumnsFor(float width)
		{
			const float fitting = std::floor(width / (kCardWidthInFontSizes * ImGui::GetFontSize()));
			return static_cast<std::size_t>(std::max(1.0f, fitting));
		}

		std::string TrunkName(const WorkBoard& board)
		{
			return board.Trunk.has_value() ? Core::ShortBranchName(*board.Trunk) : "the trunk";
		}

		std::string InfoText(const WorkInfo& info)
		{
			const std::string name =
				info.Branch.empty() ? WorktreeFolderName(info.Worktree) : Core::ShortBranchName(info.Branch);

			switch (info.Kind)
			{
			case WorkInfoKind::Locked:
				return std::string(ICON_LC_LOCK) + " " + name + " is locked" +
					(info.Detail.empty() ? "" : ": " + info.Detail);
			case WorkInfoKind::FolderMissing:
				return name + " \xC2\xB7 folder is gone";
			case WorkInfoKind::DetachedClean:
				return name + " \xC2\xB7 detached, nothing to keep";
			}

			return name;
		}

		std::string ReasonText(ReadyReason reason, const std::string& trunk)
		{
			return reason == ReadyReason::Merged ? "merged into " + trunk : "same change already in " + trunk;
		}

		bool ShowsBoardLoader(const WorkBoard& board, bool reading)
		{
			return board.FactsPending && reading && board.Cards.size() <= 1;
		}
	}

	void WorkView::ExpandReady()
	{
		m_ReadyExpanded = true;
	}

	void WorkView::Draw(const WorkBoard& board, bool reading, const std::optional<Core::Error>& error,
						std::chrono::sys_seconds now)
	{
		DrawBar(board, reading);

		// WARNING: Without facts the board has only the working copy, so a failure of the read is the whole answer.
		if (error.has_value() && board.FactsPending)
		{
			if (Core::EmptyState(error->Message, "Retry") && OnRefresh)
			{
				OnRefresh();
			}

			return;
		}

		DrawCards(board, now);

		if (ShowsBoardLoader(board, reading))
		{
			Core::LoadingState("Reading worktrees and branches");
		}

		DrawReady(board, now);
	}

	void WorkView::DrawBar(const WorkBoard& board, bool reading)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted(ICON_LC_LAYOUT_GRID " Work");
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::TextDisabled("%s", WorkSummaryText(board).c_str());

		// WARNING: The cards stand for every branch while the facts are read, and the first read of a repository with
		// tens of worktrees takes a while, so the wait is told here; the board's own loader is only for an empty board.
		if (reading && !ShowsBoardLoader(board, reading))
		{
			ImGui::SameLine();
			ImGui::PushID("##workReading");
			Core::LoadingLine("Reading worktrees and branches");
			ImGui::PopID();
		}

		ImGui::SameLine(ImGui::GetContentRegionMax().x - Core::IconButtonSize());

		if (Core::DrawIconButton("##workRefresh", ICON_LC_REFRESH_CW, false, "Refresh") && OnRefresh)
		{
			OnRefresh();
		}
	}

	void WorkView::DrawCards(const WorkBoard& board, std::chrono::sys_seconds now)
	{
		if (board.Cards.empty())
		{
			return;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float available = ImGui::GetContentRegionAvail().x;
		const std::size_t columns = ColumnsFor(available);
		const float spacing = style.ItemSpacing.x;
		const float width = (available - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
		const float height = CardHeight();
		const std::size_t rows = (board.Cards.size() + columns - 1) / columns;

		// Only the card rows on screen are drawn.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows), height + style.ItemSpacing.y);

		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				for (std::size_t column = 0; column < columns; ++column)
				{
					const std::size_t index = static_cast<std::size_t>(row) * columns + column;

					if (index >= board.Cards.size())
					{
						break;
					}

					if (column > 0)
					{
						ImGui::SameLine();
					}

					ImGui::PushID(static_cast<int>(index));
					DrawCard(board, board.Cards[index], width, height, now);
					ImGui::PopID();
				}
			}
		}
	}

	void WorkView::DrawCard(const WorkBoard& board, const WorkCard& card, float width, float height,
							std::chrono::sys_seconds now)
	{
		constexpr ImGuiWindowFlags kCardFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		if (!ImGui::BeginChild("##card", ImVec2(width, height), ImGuiChildFlags_Borders, kCardFlags))
		{
			ImGui::EndChild();
			return;
		}

		const float room = ImGui::GetContentRegionAvail().x;

		// Title.
		const bool fromSession = card.FromSession;
		const std::string title = FitToWidth(card.Title, room - (fromSession ? ImGui::GetFontSize() * 1.5f : 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted((fromSession ? std::string(ICON_LC_BOT " ") + title : title).c_str());
		ImGui::PopStyleColor();

		// Branch and folder.
		Core::PushCodeFont(ImGui::GetFontSize() * kCodeLineScale);
		ImGui::TextDisabled("%s", FitToWidth(CardBranchLine(card), room).c_str());
		ImGui::PopFont();

		// Tip subject, two lines at most.
		const float line = ImGui::GetTextLineHeight();
		if (ImGui::BeginChild("##subject", ImVec2(0.0f, static_cast<float>(kSubjectLines) * line), ImGuiChildFlags_None,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
								  ImGuiWindowFlags_NoBackground))
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(card.TipSubject.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();

		DrawChips(card, TrunkName(board));
		DrawProgress(card);
		DrawButtons(card, now);

		if (DrawBranchMenu && card.Kind != WorkCardKind::DetachedWorktree && !card.Branch.empty() &&
			ImGui::BeginPopupContextWindow("##cardMenu"))
		{
			DrawBranchMenu(card.Branch);
			ImGui::EndPopup();
		}

		ImGui::EndChild();
	}

	void WorkView::DrawChips(const WorkCard& card, const std::string& trunk)
	{
		const float chipsHeight = static_cast<float>(kChipLines) * ChipHeight() + ImGui::GetStyle().ItemSpacing.y;

		if (ImGui::BeginChild("##chips", ImVec2(0.0f, chipsHeight), ImGuiChildFlags_None,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
								  ImGuiWindowFlags_NoBackground))
		{
			DrawChipRow(CardChips(card, trunk));
		}
		ImGui::EndChild();
	}

	void WorkView::DrawProgress(const WorkCard& card)
	{
		if (card.Kind != WorkCardKind::Branch)
		{
			return;
		}

		const std::optional<ViewedCount> viewed = ReviewProgressOf ? ReviewProgressOf(card.Branch) : std::nullopt;

		ImGui::TextDisabled("%s", ReviewProgressText(viewed).c_str());

		// WARNING: The bar is drawn only for a known count; an unknown one is not an empty bar.
		const ImVec2 barSize(ImGui::GetContentRegionAvail().x, kProgressBarHeight);
		ImGui::Dummy(barSize);

		if (viewed.has_value() && viewed->Total > 0)
		{
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			const float fraction = static_cast<float>(viewed->Viewed) / static_cast<float>(viewed->Total);
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), kProgressBarHeight * kHalf);
			drawList->AddRectFilled(min, ImVec2(min.x + (max.x - min.x) * fraction, max.y),
									ImGui::GetColorU32(ImGuiCol_CheckMark), kProgressBarHeight * kHalf);
		}
	}

	void WorkView::DrawButtons(const WorkCard& card, std::chrono::sys_seconds now)
	{
		bool any = false;

		const auto place = [&any]
		{
			if (any)
			{
				ImGui::SameLine();
			}

			any = true;
		};

		if (card.Kind == WorkCardKind::Branch)
		{
			place();

			if (Core::TextButton("Review") && OnReview)
			{
				OnReview(card);
			}
		}

		if (card.Worktree.has_value())
		{
			place();

			if (Core::TextButton(ICON_LC_FOLDER_OPEN " Open folder") && OnOpenFolder)
			{
				OnOpenFolder(*card.Worktree);
			}
		}

		if (card.Kind == WorkCardKind::WorkingCopy)
		{
			place();

			if (Core::TextButton("Changes") && OnChanges)
			{
				OnChanges();
			}
		}

		const std::string age = card.TipTime.has_value() ? FormatBranchAge(*card.TipTime, now) : std::string();

		if (!any)
		{
			// WARNING: A card with no button still ends on an item.
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
		}

		if (!age.empty())
		{
			ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(age.c_str()).x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", age.c_str());
		}
	}

	void WorkView::DrawReady(const WorkBoard& board, std::chrono::sys_seconds now)
	{
		if (board.Ready.empty() && board.Info.empty())
		{
			return;
		}

		ImGui::Spacing();

		ImGui::SetNextItemOpen(m_ReadyExpanded, ImGuiCond_Always);
		m_ReadyExpanded = ChevronTreeNode("##readyGroup", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen,
										  "Ready to remove", ReadySummary(board).c_str());

		if (!m_ReadyExpanded)
		{
			return;
		}

		const std::string trunk = TrunkName(board);
		const std::size_t total = board.Ready.size() + board.Info.size();

		// Only the rows on screen are drawn: a repository can hold thousands of merged branches.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(total), ImGui::GetTextLineHeightWithSpacing());

		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const std::size_t index = static_cast<std::size_t>(row);

				if (index < board.Ready.size())
				{
					const ReadyEntry& entry = board.Ready[index];
					const std::string age = entry.TipTime.has_value() ? FormatBranchAge(*entry.TipTime, now) : std::string();

					ImGui::TextUnformatted(Core::ShortBranchName(entry.Branch).c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%s%s%s", ReasonText(entry.Reason, trunk).c_str(), age.empty() ? "" : " \xC2\xB7 ",
										age.c_str());
				}
				else
				{
					ImGui::TextDisabled("%s", InfoText(board.Info[index - board.Ready.size()]).c_str());
				}
			}
		}
	}
}
