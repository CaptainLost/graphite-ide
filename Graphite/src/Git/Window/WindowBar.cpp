#include "Git/Window/WindowBar.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"
#include "Git/Branches/BranchLookup.h"
#include "Theme/Palettes.h"
#include "UI/FittedText.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>

namespace Graphite {

	namespace {

		constexpr float kMockupFont = 13.0f;
		constexpr float kHalf = 0.5f;

		constexpr float kBarHeight = 36.0f;
		constexpr float kBarSide = 10.0f;
		constexpr float kGap = 8.0f;
		constexpr float kIconGap = 6.0f;
		constexpr float kChipHeight = 24.0f;
		constexpr float kChipSide = 9.0f;
		constexpr float kChipGap = 6.0f;
		constexpr float kChipMaxWidth = 330.0f;
		constexpr float kChipRounding = 6.0f;
		constexpr float kRemotesText = 12.0f;
		constexpr float kHeadDot = 7.0f;
		constexpr float kHeadGlow = 11.0f;
		constexpr float kHeadGlowAlpha = 0.25f;
		constexpr float kSeparatorHeight = 16.0f;
		constexpr float kRepositoryShare = 0.4f;
		constexpr float kRemotesShare = 0.35f;
		constexpr float kLighterShare = 0.4f;
		constexpr float kAskTint = 0.15f;
		constexpr float kKeepFontSize = 0.0f;

		constexpr const char* kSeparator = " \xC2\xB7 ";
		constexpr const char* kUnreadRemotes = "\xE2\x80\xA6";

		ImU32 PackedColour(const Core::Color& colour)
		{
			return IM_COL32(Core::ChannelToByte(colour.R), Core::ChannelToByte(colour.G), Core::ChannelToByte(colour.B),
							Core::ChannelToByte(colour.A));
		}

		ImU32 Mixed(ImU32 from, ImU32 toward, float share)
		{
			return ImGui::ColorConvertFloat4ToU32(
				ImLerp(ImGui::ColorConvertU32ToFloat4(from), ImGui::ColorConvertU32ToFloat4(toward), share));
		}

		std::string FolderName(const std::filesystem::path& folder)
		{
			// libgit2 hands the working directory back with a trailing separator, whose filename is empty.
			const std::filesystem::path name = folder.has_filename() ? folder.filename() : folder.parent_path().filename();
			return Core::ToUtf8(name.empty() ? folder : name);
		}

		std::string PushPullWords(const Core::Branch& branch)
		{
			if (!branch.Upstream.has_value() || !branch.DistanceKnown)
			{
				return {};
			}

			std::string words;

			if (branch.Ahead > 0)
			{
				words = std::to_string(branch.Ahead) + " to push";
			}

			if (branch.Behind > 0)
			{
				words += (words.empty() ? "" : kSeparator) + std::to_string(branch.Behind) + " to pull";
			}

			return words;
		}

		void DescribeHead(const WindowBarInput& input, WindowBarText& text)
		{
			const Core::HeadState& head = *input.Head;

			switch (head.Kind)
			{
			case Core::HeadKind::Branch:
			{
				text.Head = head.BranchName;

				const Core::Branch* branch = input.Branches != nullptr ? CheckedOutBranch(*input.Branches) : nullptr;

				if (branch != nullptr && branch->Upstream.has_value())
				{
					text.HeadTooltip = head.BranchName + " tracks " + *branch->Upstream;
					text.HeadState = PushPullWords(*branch);
				}
				else
				{
					text.HeadTooltip = head.BranchName + " has no upstream branch";
				}

				break;
			}

			case Core::HeadKind::Detached:
				text.Head = "detached at " + (head.Commit ? head.Commit->ShortHex() : std::string{});
				text.HeadTooltip = "HEAD is detached at " + (head.Commit ? head.Commit->ToHex() : std::string{});
				break;

			case Core::HeadKind::Unborn:
				text.Head = head.BranchName + kSeparator + "no commits yet";
				text.HeadTooltip = "No commits yet on " + head.BranchName;
				break;
			}
		}

		void DescribeRemotes(const WindowBarInput& input, WindowBarText& text)
		{
			const std::vector<Core::Remote>* remotes = input.Remotes;

			if (input.RemotesFailure != nullptr)
			{
				text.Remotes = "cannot read remotes";
				text.RemotesTooltip = *input.RemotesFailure;
				return;
			}

			if (remotes == nullptr)
			{
				text.Remotes = kUnreadRemotes;
				return;
			}

			if (remotes->empty())
			{
				text.Remotes = "no remotes";
				return;
			}

			for (const Core::Remote& remote : *remotes)
			{
				if (!text.Remotes.empty())
				{
					text.Remotes += kSeparator;
					text.RemotesTooltip += "\n";
				}

				text.Remotes += remote.Name;
				text.RemotesTooltip += remote.Url.empty() ? remote.Name : remote.Name + ": " + remote.Url;
			}
		}

		float Em(float mockupPixels)
		{
			return WindowBarEm(mockupPixels);
		}

		float WidthOf(const std::string& text)
		{
			return text.empty() ? 0.0f : ImGui::CalcTextSize(text.c_str()).x;
		}

		void ShowTooltipOfLastItem(const std::string& tooltip)
		{
			if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s", tooltip.c_str());
			}
		}

		void DrawHeadDot(ImDrawList* drawList, ImVec2 centre, const WindowBarColours& colours)
		{
			ImVec4 glow = ImGui::ColorConvertU32ToFloat4(colours.Accent);
			glow.w *= kHeadGlowAlpha;

			drawList->AddCircleFilled(centre, Em(kHeadGlow) * kHalf, ImGui::ColorConvertFloat4ToU32(glow));
			drawList->AddCircleFilled(centre, Em(kHeadDot) * kHalf, colours.Accent);
		}

		// What stands in the chip's place while HEAD is unread, cannot be read, or a checkout is under way.
		void DrawInPlaceOfChip(const WindowBarDrawing& drawing, ImVec2 topLeft, float room)
		{
			const WindowBarText& text = drawing.Text;

			ImGui::SetCursorScreenPos(ImVec2(topLeft.x, topLeft.y + (Em(kChipHeight) - ImGui::GetTextLineHeight()) * kHalf));

			if (!drawing.HeadFailure.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, drawing.Colours.Dim);
				ImGui::TextUnformatted(FitToWidth(drawing.HeadFailure, room).c_str());
				ImGui::PopStyleColor();
				return;
			}

			ImGui::PushID(text.Busy ? "##checkingOut" : "##headLoading");

			if (text.Busy)
			{
				// A checkout under way is named here in place of HEAD's branch, with the loader beside it.
				const std::string shown = FitToWidth(text.Head, room - Core::LoadingLineWidth(""));
				Core::LoadingLine(shown.c_str(), Core::WhileLoaderWaits::ShowText);
			}
			else
			{
				Core::LoadingLine("Reading HEAD");
			}

			ImGui::PopID();
		}

		// The chip: the dot, HEAD's name cut to what is left of the chip's width, and its state after it.
		bool DrawHeadChip(const WindowBarDrawing& drawing, ImVec2 topLeft, float room)
		{
			const WindowBarText& text = drawing.Text;
			const WindowBarColours& colours = drawing.Colours;

			if (text.Busy || text.Head.empty())
			{
				DrawInPlaceOfChip(drawing, topLeft, room);
				return false;
			}

			const float height = Em(kChipHeight);
			const float side = Em(kChipSide);
			const float gap = Em(kChipGap);
			const float dot = Em(kHeadDot);
			const float maxWidth = std::min(Em(kChipMaxWidth), room);
			const float stateWidth = text.HeadState.empty() ? 0.0f : gap + WidthOf(text.HeadState);
			const std::string name = FitToWidth(text.Head, maxWidth - 2.0f * side - dot - gap - stateWidth);
			const float width = 2.0f * side + dot + gap + WidthOf(name) + stateWidth;

			ImGui::SetCursorScreenPos(topLeft);
			const bool clicked = ImGui::InvisibleButton("##headChip", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			ShowTooltipOfLastItem(text.HeadTooltip);

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
			drawList->AddRectFilled(topLeft, bottomRight, hovered ? colours.ChipHoveredFill : colours.ChipFill, Em(kChipRounding));
			drawList->AddRect(topLeft, bottomRight, colours.Rule, Em(kChipRounding));

			const float centreY = topLeft.y + height * kHalf;
			const float textTop = centreY - ImGui::GetTextLineHeight() * kHalf;
			float x = topLeft.x + side;

			DrawHeadDot(drawList, ImVec2(x + dot * kHalf, centreY), colours);
			x += dot + gap;

			drawList->AddText(ImVec2(x, textTop), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());

			if (!text.HeadState.empty())
			{
				drawList->AddText(ImVec2(x + WidthOf(name) + gap, textTop), colours.Match, text.HeadState.c_str());
			}

			return clicked;
		}

		// The repository's icon and its folder's name in bold; returns the width it took.
		float DrawRepository(const WindowBarDrawing& drawing, ImVec2 at, float room)
		{
			const float iconWidth = WidthOf(ICON_LC_FOLDER) + Em(kIconGap);

			ImGui::SetCursorScreenPos(at);
			ImGui::TextUnformatted(ICON_LC_FOLDER);
			ImGui::SameLine(0.0f, Em(kIconGap));

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
			ImGui::TextUnformatted(FitToWidth(drawing.Text.Repository, room - iconWidth).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

			const float width = ImGui::GetItemRectMax().x - at.x;

			ImGui::SetCursorScreenPos(at);
			ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
			ShowTooltipOfLastItem(Core::ToUtf8(Core::NormalizeDirectory(drawing.WorkingDirectory)));

			return width;
		}

		// The remotes in a step smaller text, centred on `centreY`; returns the width it took.
		float DrawRemotes(const WindowBarDrawing& drawing, float left, float centreY, float room)
		{
			Core::PushInterfaceFont(ImGui::GetFontSize() * kRemotesText / kMockupFont);

			ImGui::SetCursorScreenPos(ImVec2(left, centreY - ImGui::GetTextLineHeight() * kHalf));
			ImGui::PushStyleColor(ImGuiCol_Text, drawing.Colours.Dim);
			ImGui::TextUnformatted(FitToWidth(drawing.Text.Remotes, room).c_str());
			ImGui::PopStyleColor();
			const float width = ImGui::GetItemRectSize().x;
			ShowTooltipOfLastItem(drawing.Text.RemotesTooltip);

			ImGui::PopFont();

			return width;
		}
	}

	float WindowBarEm(float mockupPixels)
	{
		return ImGui::GetFontSize() * mockupPixels / kMockupFont;
	}

	WindowBarText DescribeWindowBar(const WindowBarInput& input)
	{
		WindowBarText text;
		text.Repository = FolderName(input.WorkingDirectory);
		text.Busy = input.CheckingOut;

		DescribeRemotes(input, text);

		if (input.Head != nullptr && !input.CheckingOut)
		{
			DescribeHead(input, text);
		}

		return text;
	}

	SettledWindowBar SettleWindowBar(const WindowBarInput& input, bool headFailed, const WindowBarText* held)
	{
		const bool headKnown = input.Head != nullptr || headFailed;
		const bool remotesKnown = input.Remotes != nullptr || input.RemotesFailure != nullptr;

		if (headKnown && remotesKnown)
		{
			return SettledWindowBar{DescribeWindowBar(input), true};
		}

		if (held != nullptr)
		{
			return SettledWindowBar{*held, true};
		}

		const WindowBarInput waiting{input.WorkingDirectory, nullptr, nullptr, nullptr, false};
		return SettledWindowBar{DescribeWindowBar(waiting), false};
	}

	WindowBarColours ReadWindowBarColours(const ResolvedScheme& scheme)
	{
		const ImU32 accent = PackedColour(ColourFromScheme(scheme, ChromeColor::Accent));
		const ImU32 background = PackedColour(ColourFromScheme(scheme, ChromeColor::Background));
		const ImU32 text = PackedColour(ColourFromScheme(scheme, ChromeColor::Text));
		const ImU32 warning = ReadEditorPalette(scheme).DiagnosticWarning;

		WindowBarColours colours;
		colours.Dim = PackedColour(ColourFromScheme(scheme, ChromeColor::TextDisabled));
		colours.Match = Mixed(accent, text, kLighterShare);
		colours.Rule = PackedColour(ColourFromScheme(scheme, ChromeColor::Border));
		colours.ChipFill = PackedColour(ColourFromScheme(scheme, ChromeColor::Surface));
		colours.ChipHoveredFill = PackedColour(ColourFromScheme(scheme, ChromeColor::SurfaceHovered));
		colours.Accent = accent;
		colours.OnAccent = background;
		colours.AskFill = Mixed(background, warning, kAskTint);
		colours.AskQuestion = Mixed(warning, text, kLighterShare);
		colours.Automatic = ReadGitPalette(scheme).GutterAdded;

		return colours;
	}

	bool DrawWindowBar(const WindowBarDrawing& drawing)
	{
		const ImVec2 origin(ImGui::GetCursorScreenPos().x - drawing.Bleed.x, ImGui::GetCursorScreenPos().y - drawing.Bleed.y);
		const float width = ImGui::GetContentRegionAvail().x + 2.0f * drawing.Bleed.x;
		const float height = Em(kBarHeight);
		const float side = Em(kBarSide);
		const float gap = Em(kGap);
		const float right = origin.x + width - side;
		const float centreY = origin.y + height * kHalf;
		const float lineHeight = ImGui::GetTextLineHeight();
		const float room = right - (origin.x + side);

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		drawList->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + width, origin.y + height - 1.0f),
						  drawing.Colours.Rule);

		float x = origin.x + side;
		x += DrawRepository(drawing, ImVec2(x, centreY - lineHeight * kHalf), room * kRepositoryShare) + gap;
		x += DrawRemotes(drawing, x, centreY, room * kRemotesShare) + gap;

		const float separatorHalf = Em(kSeparatorHeight) * kHalf;
		drawList->AddLine(ImVec2(x, centreY - separatorHalf), ImVec2(x, centreY + separatorHalf), drawing.Colours.Rule);
		x += 1.0f + gap;

		const bool clicked = DrawHeadChip(drawing, ImVec2(x, centreY - Em(kChipHeight) * kHalf), right - x);

		// The bar is one item high however many it drew, so the cursor lands below it, as far down as without the bleed.
		// WARNING: The item keeps to the content region's width, so the bleed, which only the rule and the room for the
		// items reach into, never widens what the window scrolls; the items start a bar side in, more than the padding.
		ImGui::SetCursorScreenPos(ImVec2(origin.x + drawing.Bleed.x, origin.y));
		ImGui::Dummy(ImVec2(width - 2.0f * drawing.Bleed.x, height + drawing.Bleed.y));

		return clicked;
	}
}
