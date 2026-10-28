#include "Git/Work/WorkView.h"

#include "Git/Branches/BranchAge.h"
#include "Git/Work/CardText.h"
#include "Git/Work/ChipRow.h"
#include "UI/FittedText.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"
#include "Theme/ActiveScheme.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/Theme.h"
#include "Core/Text/Encoding.h"

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
		// The branch line is in the code face a step smaller than the interface's, as the mockup sets it.
		constexpr float kCodeLineScale = 0.9f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kCardRoundingStep = 2.0f;
		constexpr int kSubjectLines = 2;
		constexpr int kChipLines = 2;
		constexpr float kProgressBarHeight = 4.0f;
		constexpr float kHalf = 0.5f;

		// Title, branch line, subject, chips, progress and buttons, with one spacing to spare at the foot.
		constexpr std::size_t kCardItemCount = 6;

		float CardHeight()
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float line = ImGui::GetTextLineHeight();
			const float chips = static_cast<float>(kChipLines) * ChipHeight() + style.ItemSpacing.y;

			return 2.0f * style.WindowPadding.y + line + line * kCodeLineScale +
				static_cast<float>(kSubjectLines) * line + chips + line + ImGui::GetFrameHeight() +
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

		// The name and folder at the left, the reason and the age at the right edge, the name cut first when they
		// do not fit.
		void DrawReadyLine(const ReadyLine& line, std::chrono::sys_seconds now)
		{
			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float textTop = ListRowTextTop(rowStart.y);
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float right = rowStart.x + width;

			const std::string age = line.TipTime.has_value() ? FormatBranchAge(*line.TipTime, now) : std::string();
			const float ageWidth = age.empty() ? 0.0f : ImGui::CalcTextSize(age.c_str()).x;
			const float ageLeft = right - ageWidth;
			const float reasonWidth = ImGui::CalcTextSize(line.Reason.c_str()).x;
			const float reasonLeft = ageLeft - (age.empty() ? 0.0f : spacing * 2.0f) - reasonWidth;

			float left = rowStart.x;

			if (line.Locked)
			{
				ImGui::SetCursorScreenPos(ImVec2(left, textTop));
				ImGui::TextDisabled("%s", ICON_LC_LOCK);
				left = ImGui::GetItemRectMax().x + spacing;
			}

			const float nameRoom = std::max(0.0f, reasonLeft - spacing * 2.0f - left);
			const std::string name = FitToWidth(line.Name, nameRoom);

			ImGui::SetCursorScreenPos(ImVec2(left, textTop));

			if (line.Locked)
			{
				ImGui::TextDisabled("%s", name.c_str());
			}
			else
			{
				ImGui::TextUnformatted(name.c_str());
			}

			const float folderLeft = ImGui::GetItemRectMax().x + spacing;
			const float folderRoom = reasonLeft - spacing * 2.0f - folderLeft;

			if (!line.Folder.empty() && folderRoom > 0.0f)
			{
				ImGui::SetCursorScreenPos(ImVec2(folderLeft, textTop));
				ImGui::TextDisabled("%s", FitToWidth(line.Folder, folderRoom).c_str());
			}

			ImGui::SetCursorScreenPos(ImVec2(std::max(left, reasonLeft), textTop));
			ImGui::TextDisabled("%s", line.Reason.c_str());

			if (!age.empty())
			{
				ImGui::SetCursorScreenPos(ImVec2(ageLeft, textTop));
				ImGui::TextDisabled("%s", age.c_str());
			}

			// WARNING: A row laid out by hand ends on an item the size of the row.
			ImGui::SetCursorScreenPos(rowStart);
			ImGui::Dummy(ImVec2(width, ListRowLineHeight()));
		}

		bool ShowsBoardLoader(const WorkBoard& board, bool reading)
		{
			return board.FactsPending && reading && board.Cards.size() <= 1;
		}
	}

	WorkView::WorkView(const ActiveScheme& scheme) : m_Scheme(scheme)
	{
	}

	void WorkView::ExpandReady()
	{
		m_ReadyExpanded = true;
	}

	void WorkView::Draw(const WorkBoard& board, bool reading, const std::optional<Core::Error>& error,
						std::chrono::sys_seconds now)
	{
		m_ChipColours = ReadChipColours(m_Scheme.Get());

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
		ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted("Work");
		ImGui::PopStyleColor();
		ImGui::PopFont();

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

		// The card is lifted off the window as a field is, and rounded a step more than a control.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + kCardRoundingStep);
		const bool open = ImGui::BeginChild("##card", ImVec2(width, height), ImGuiChildFlags_Borders, kCardFlags);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		if (!open)
		{
			ImGui::EndChild();
			return;
		}

		DrawTitle(card);

		// Branch and folder.
		const float room = ImGui::GetContentRegionAvail().x;
		Core::PushCodeFont(ImGui::GetFontSize() * kCodeLineScale);
		ImGui::TextDisabled("%s", FitToWidth(CardBranchLine(card), room).c_str());
		ImGui::PopFont();

		DrawSubject(card.TipSubject);
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

	void WorkView::DrawTitle(const WorkCard& card)
	{
		// The session's bot, a folder for a worktree, a branch for a branch alone; the working copy says so in a chip.
		const char* const icon = card.FromSession ? ICON_LC_BOT
								 : card.Worktree.has_value() || card.Kind == WorkCardKind::WorkingCopy ? ICON_LC_FOLDER
																										 : ICON_LC_GIT_BRANCH;
		const Chip workingCopy{"working copy"};
		const bool isWorkingCopy = card.Kind == WorkCardKind::WorkingCopy;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;

		ImGui::TextUnformatted(icon);
		ImGui::SameLine();

		const float chipRoom = isWorkingCopy ? ChipWidthOf(workingCopy.Text) + spacing : 0.0f;

		ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted(FitToWidth(card.Title, ImGui::GetContentRegionAvail().x - chipRoom).c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();

		if (isWorkingCopy)
		{
			ImGui::SameLine();
			DrawChipRow({workingCopy}, m_ChipColours);
		}
	}

	void WorkView::DrawSubject(const std::string& subject)
	{
		// Two lines at most, the second cut with an ellipsis where the subject runs on.
		const float room = ImGui::GetContentRegionAvail().x;
		const char* const begin = subject.c_str();
		const char* const end = begin + subject.size();
		const char* firstEnd = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), begin, end, room);

		// WARNING: A word wider than the line leaves no break before it; the line is then cut at the first character.
		if (firstEnd == begin && begin != end)
		{
			firstEnd = begin + Core::NextCharacter(subject, 0);
		}

		const char* rest = firstEnd;

		while (rest != end && (*rest == ' ' || *rest == '\n'))
		{
			++rest;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(begin, firstEnd);

		if (rest != end)
		{
			ImGui::TextUnformatted(FitToWidth(std::string_view(rest, static_cast<std::size_t>(end - rest)), room).c_str());
		}

		ImGui::PopStyleColor();
	}

	void WorkView::DrawChips(const WorkCard& card, const std::string& trunk)
	{
		// The chips take the lines they need, two at most, and what follows comes straight after, as in the mockup:
		// a card is as tall as the fullest one, and the room a shorter one leaves is at its foot.
		DrawChipRow(CardChips(card, trunk), m_ChipColours, kChipLines);
	}

	void WorkView::DrawProgress(const WorkCard& card)
	{
		if (card.Kind != WorkCardKind::Branch)
		{
			return;
		}

		const std::optional<ViewedCount> viewed = ReviewProgressOf ? ReviewProgressOf(card.Branch) : std::nullopt;
		const std::string count = ReviewCountText(viewed);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;

		// "Reviewed", the bar, then the count, on one line.
		ImGui::TextDisabled("Reviewed");
		ImGui::SameLine();

		const float barWidth =
			std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(count.c_str()).x - spacing);
		ImGui::Dummy(ImVec2(barWidth, ImGui::GetTextLineHeight()));

		// WARNING: The bar is drawn only for a known count; an unknown one is not an empty bar.
		if (viewed.has_value() && viewed->Total > 0)
		{
			const ImVec2 min = ImGui::GetItemRectMin();
			const float top = min.y + (ImGui::GetTextLineHeight() - kProgressBarHeight) * kHalf;
			const ImVec2 trackMin(min.x, top);
			const ImVec2 trackMax(min.x + barWidth, top + kProgressBarHeight);
			const float fraction = static_cast<float>(viewed->Viewed) / static_cast<float>(viewed->Total);
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(ImGuiCol_Border), kProgressBarHeight * kHalf);

			// WARNING: Nothing viewed draws no fill: a rounded rectangle of no width still shows as a dot.
			if (viewed->Viewed > 0)
			{
				drawList->AddRectFilled(trackMin, ImVec2(trackMin.x + barWidth * fraction, trackMax.y),
										ImGui::GetColorU32(ImGuiCol_CheckMark), kProgressBarHeight * kHalf);
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("%s", count.c_str());
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

			if (Core::TextButton(ICON_LC_EYE " Review") && OnReview)
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

			if (Core::TextButton(ICON_LC_FILE_DIFF " Changes") && OnChanges)
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

		// WARNING: The group's heading is a list row, which stands in the middle of its row only inside a list body.
		const ListBodyScope listBody;

		ImGui::SetNextItemOpen(m_ReadyExpanded, ImGuiCond_Always);
		const std::string count = std::to_string(board.Ready.size());
		m_ReadyExpanded = ChevronTreeNode("##readyGroup", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen,
										  "Ready to remove", count.c_str());

		// The summary and the rows line up with the group's label.
		const float indent = TreeRowLabelOffset();
		ImGui::Indent(indent);

		if (!m_ReadyExpanded)
		{
			ImGui::TextDisabled("%s", FitToWidth(ReadySummary(board), ImGui::GetContentRegionAvail().x).c_str());
			ImGui::Unindent(indent);
			return;
		}

		// Only the rows on screen are built and drawn: a repository can hold thousands of merged branches.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(ReadyLineCount(board)), ListRowPitch());

		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				ImGui::PushID(row);
				DrawReadyLine(ReadyLineAt(board, static_cast<std::size_t>(row)), now);
				ImGui::PopID();
			}
		}

		ImGui::Unindent(indent);
	}
}
