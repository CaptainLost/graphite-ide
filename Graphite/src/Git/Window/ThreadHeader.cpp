#include "Git/Window/ThreadHeader.h"

#include "Git/Work/CardText.h"
#include "Git/Work/ChipRow.h"
#include "UI/FittedText.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/Theme.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kOpenInOwnTab = "Open in own tab";

		// As on the card.
		constexpr float kCodeLineScale = 0.9f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr bool kNotToggled = false;

		// The branch line takes at most this much of the line it shares with the chips.
		constexpr float kBranchLineShare = 0.5f;

		std::string BranchAndFolder(const ThreadHeaderInput& input)
		{
			return input.Card != nullptr ? CardBranchLine(*input.Card) : Core::ShortBranchName(std::string(input.Branch));
		}

		std::vector<Chip> HeaderChips(const ThreadHeaderInput& input)
		{
			std::vector<Chip> chips;

			if (input.Card != nullptr)
			{
				chips = CardChips(*input.Card, input.TrunkName);
			}

			chips.push_back({ReviewProgressText(input.Viewed)});

			if (input.AgainstBase.has_value())
			{
				chips.push_back({"against " + *input.AgainstBase});
			}

			return chips;
		}
	}

	ThreadHeaderClick DrawThreadHeader(const ThreadHeaderInput& input)
	{
		const WorkCard* const card = input.Card;
		const std::string title =
			card != nullptr && !card->Title.empty() ? card->Title : Core::ShortBranchName(input.Branch);
		const char* const icon = card != nullptr && card->FromSession     ? ICON_LC_BOT
								 : card != nullptr && card->Worktree.has_value() ? ICON_LC_FOLDER
																				 : ICON_LC_GIT_BRANCH;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttonWidth = ImGui::CalcTextSize(kOpenInOwnTab).x + 2.0f * style.FramePadding.x;
		ThreadHeaderClick click = ThreadHeaderClick::None;

		// Back to Work, the icon and the bold title, with Open in own tab at the right.
		if (Core::DrawIconButton("##threadBack", ICON_LC_ARROW_LEFT, kNotToggled, "Back to Work"))
		{
			click = ThreadHeaderClick::BackToWork;
		}

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(icon);
		ImGui::SameLine();

		ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted(
			FitToWidth(title, ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x).c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();

		// WARNING: Placed from the cursor, not with SameLine's offset: drawn straight into a table column, the header
		// would have the offset counted from the column's left edge and GetContentRegionMax from the window's.
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttonWidth));

		if (Core::TextButton(kOpenInOwnTab))
		{
			click = ThreadHeaderClick::OpenInOwnTab;
		}

		// The branch and folder, then the chips on the same line, wrapping as they need.
		Core::PushCodeFont(ImGui::GetFontSize() * kCodeLineScale);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s",
							FitToWidth(BranchAndFolder(input), ImGui::GetContentRegionAvail().x * kBranchLineShare).c_str());
		ImGui::PopFont();

		ImGui::SameLine();
		DrawChipRow(HeaderChips(input), input.Colours);
		ImGui::Separator();

		return click;
	}
}
