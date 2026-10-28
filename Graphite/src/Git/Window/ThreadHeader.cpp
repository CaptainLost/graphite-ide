#include "Git/Window/ThreadHeader.h"

#include "Git/Work/CardText.h"
#include "Git/Work/ChipRow.h"
#include "UI/FittedText.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/FontSet.h"
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
		constexpr float kCodeLineScale = 1.1f;
		constexpr float kBotRoomInFontSizes = 1.5f;

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

	bool DrawThreadHeader(const ThreadHeaderInput& input)
	{
		const WorkCard* const card = input.Card;
		const bool fromSession = card != nullptr && card->FromSession;
		const std::string title =
			card != nullptr && !card->Title.empty() ? card->Title : Core::ShortBranchName(input.Branch);

		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttonWidth = ImGui::CalcTextSize(kOpenInOwnTab).x + 2.0f * style.FramePadding.x;
		const float room = ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x -
						   (fromSession ? ImGui::GetFontSize() * kBotRoomInFontSizes : 0.0f);
		const std::string fitted = FitToWidth(title, room);

		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
		ImGui::TextUnformatted((fromSession ? std::string(ICON_LC_BOT " ") + fitted : fitted).c_str());
		ImGui::PopStyleColor();

		// WARNING: Placed from the cursor, not with SameLine's offset: drawn straight into a table column, the header
		// would have the offset counted from the column's left edge and GetContentRegionMax from the window's.
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttonWidth));
		const bool openInOwnTab = Core::TextButton(kOpenInOwnTab);

		Core::PushCodeFont(ImGui::GetFontSize() * kCodeLineScale);
		ImGui::TextDisabled("%s", FitToWidth(BranchAndFolder(input), ImGui::GetContentRegionAvail().x).c_str());
		ImGui::PopFont();

		DrawChipRow(HeaderChips(input), input.Colours);
		ImGui::Separator();

		return openInOwnTab;
	}
}
