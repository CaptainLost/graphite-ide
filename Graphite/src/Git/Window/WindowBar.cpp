#include "Git/Window/WindowBar.h"

#include "Core/Base/Count.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"
#include "Git/Branches/BranchAge.h"
#include "Git/Branches/BranchLookup.h"
#include "Git/Network/NetworkText.h"
#include "Theme/Palettes.h"
#include "UI/FittedText.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
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
		constexpr float kRepositoryMinimum = 90.0f; // the icon and the start of the name
		constexpr float kMeasuringSlack = 1.0f; // CalcTextSize rounds its widths up, so a width measured twice can differ by a pixel
		constexpr float kChipPlaceholder = 120.0f;  // what the chip's place holds while HEAD is unread
		constexpr float kLighterShare = 0.4f;
		constexpr float kAskTint = 0.15f;

		// How far the network buttons' icons stand from the text toward the dim: the mockup's #c3c8cf between its text,
		// #d6dbe2, and its dim, #7b828d.
		constexpr float kIconShare = 0.21f;

		constexpr float kKeepFontSize = 0.0f;

		constexpr float kButtonSide = 8.0f;
		constexpr float kButtonRounding = 5.0f;
		constexpr float kButtonGap = 2.0f;
		constexpr float kButtonInnerGap = 5.0f;
		constexpr float kWhenText = 12.0f;
		constexpr float kWhenGap = 8.0f;

		// The widest age the bar says, so the buttons keep their place as the age changes.
		constexpr const char* kWidestWhen = "fetched 99mo ago";
		constexpr const char* kJustNow = "fetched just now";

		constexpr const char* kGitMissing =
			"Fetch, Pull and Push need git, which was not found on PATH or where Git for Windows installs it.";
		constexpr const char* kClickToCancel = "Click to cancel";
		constexpr const char* kLastStepTooltip = "Pulling\xE2\x80\xA6 the last step cannot be cancelled";

		// The widest the count slot reads, so a button is as wide with no count as with one.
		constexpr const char* kWidestCount = "999+";
		constexpr const char* kWidestPercent = "100%";
		constexpr std::size_t kLargestCountShown = 999;

		constexpr const char* kSeparator = " \xC2\xB7 ";
		constexpr const char* kUnreadRemotes = "\xE2\x80\xA6";
		constexpr const char* kChipClickHint = "Click to show it in the list";

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
				words = Core::FormatCount(branch.Ahead) + " to push";
			}

			if (branch.Behind > 0)
			{
				words += (words.empty() ? "" : kSeparator) + Core::FormatCount(branch.Behind) + " to pull";
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
				text.OnBranch = true;

				// WARNING: Looked up by HEAD's name, not as the listing's checked-out branch: after a checkout HEAD is read at once
				// while the branches wait for the refresh, and the listing's checked-out branch is the one checked out before.
				const Core::Branch* branch =
					input.Branches != nullptr ? BranchWithFullName(*input.Branches, Core::LocalBranchReference(head.BranchName)) : nullptr;

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

		// The count slot's text: 999+ for a thousand or more.
		std::string SlotCount(std::size_t count)
		{
			return count > kLargestCountShown ? kWidestCount : Core::FormatCount(count);
		}

		std::string CommitsText(std::size_t count)
		{
			return Core::FormatCount(count) + (count == 1 ? " commit" : " commits");
		}

		// The remote an upstream lives on: the longest remote name that begins its full name, as a remote's name may hold a
		// slash. Null for an upstream that is a local branch, which has no remote.
		std::optional<std::string> RemoteOfUpstream(const Core::Branch& branch, const WindowBarInput& input)
		{
			if (!branch.Upstream.has_value())
			{
				return std::nullopt;
			}

			const Core::Branch* const upstream = RemoteBranchNamed(*input.Branches, *branch.Upstream);

			if (upstream == nullptr)
			{
				return std::nullopt;
			}

			std::optional<std::string> longest;

			for (const Core::Remote& remote : *input.Remotes)
			{
				const bool holdsBranch = upstream->FullName.starts_with("refs/remotes/" + remote.Name + "/");

				if (holdsBranch && (!longest.has_value() || remote.Name.size() > longest->size()))
				{
					longest = remote.Name;
				}
			}

			return longest;
		}

		std::string FetchedText(std::chrono::sys_seconds lastFetch, std::chrono::sys_seconds now)
		{
			const std::string age = FormatBranchAge(lastFetch, now);

			return age == "now" ? "fetched just now" : "fetched " + age + " ago";
		}

		void DescribeFetch(const WindowBarInput& input, ButtonText& fetch)
		{
			if (input.Remotes->empty())
			{
				fetch.Tooltip = NoRemoteText();
				return;
			}

			std::string names;

			for (const Core::Remote& remote : *input.Remotes)
			{
				names += (names.empty() ? "" : ", ") + remote.Name;
			}

			fetch.Enabled = true;
			fetch.Tooltip = "Fetch every remote: " + names;
		}

		void DescribePull(const Core::Branch& branch, ButtonText& pull)
		{
			if (!branch.Upstream.has_value())
			{
				pull.Tooltip = NoUpstreamText(branch.Name);
				return;
			}

			pull.Enabled = true;
			pull.Tooltip = "Pull " + *branch.Upstream + " into " + branch.Name;

			if (branch.DistanceKnown && branch.Behind > 0)
			{
				pull.Count = SlotCount(branch.Behind);
			}
		}

		void DescribePush(const Core::Branch& branch, const WindowBarInput& input, ButtonText& push)
		{
			const std::vector<Core::Remote>& remotes = *input.Remotes;

			if (remotes.empty())
			{
				push.Tooltip = NoRemoteText();
				return;
			}

			push.Enabled = true;
			const std::optional<std::string> upstreamRemote = RemoteOfUpstream(branch, input);

			// WARNING: As Push does: a branch goes to an upstream of its own name alone, and is published under its own name
			// otherwise. The count is still the distance to the upstream, as the chip says it.
			const bool upstreamOfAnotherName = upstreamRemote.has_value() && *branch.Upstream != *upstreamRemote + "/" + branch.Name;

			if (upstreamOfAnotherName)
			{
				push.Count = branch.DistanceKnown && branch.Ahead > 0 ? SlotCount(branch.Ahead) : std::string{};
				push.Tooltip = PublishText(branch.Name, *upstreamRemote, branch.Upstream);
				return;
			}

			if (upstreamRemote.has_value() && branch.DistanceKnown && branch.Ahead > 0)
			{
				push.Count = SlotCount(branch.Ahead);
				push.Tooltip = "Push " + CommitsText(branch.Ahead) + " to " + *upstreamRemote;
				return;
			}

			if (upstreamRemote.has_value() || remotes.size() == 1)
			{
				push.Tooltip = "Push " + branch.Name + " to " + (upstreamRemote.has_value() ? *upstreamRemote : remotes.front().Name);
				return;
			}

			// WARNING: A branch whose upstream is a local branch has none on a remote, so it asks too.
			push.AsksWhichRemote = true;
			push.Tooltip = "Push " + branch.Name + " to…";
		}

		// What the branch checked out lets Pull and Push do; both stay disabled, saying why, when no branch is checked out.
		void DescribeCheckedOut(const WindowBarInput& input, NetworkButtons& buttons)
		{
			const Core::HeadState& head = *input.Head;

			if (head.Kind == Core::HeadKind::Detached)
			{
				buttons.Pull.Tooltip = NoBranchText();
				buttons.Push.Tooltip = NoBranchText();
				return;
			}

			if (head.Kind == Core::HeadKind::Unborn)
			{
				buttons.Pull.Tooltip = NoUpstreamText(head.BranchName);
				buttons.Push.Tooltip = head.BranchName + " has no commits yet.";
				return;
			}

			const Core::Branch* const branch = BranchWithFullName(*input.Branches, Core::LocalBranchReference(head.BranchName));

			if (branch == nullptr)
			{
				return;
			}

			DescribePull(*branch, buttons.Pull);
			DescribePush(*branch, input, buttons.Push);
		}

		ButtonText& ButtonOf(NetworkButtons& buttons, NetworkOperation operation)
		{
			switch (operation)
			{
			case NetworkOperation::Pull:
				return buttons.Pull;
			case NetworkOperation::Push:
				return buttons.Push;
			case NetworkOperation::Fetch:
			case NetworkOperation::FullHistory:
				break;
			}

			return buttons.Fetch;
		}

		// Laid over the buttons as described with nothing running. The others are disabled and keep their counts, dim in
		// their slots: the running state changes only its own button.
		void ApplyRunning(const NetworkState& state, NetworkButtons& buttons)
		{
			for (ButtonText* const button : {&buttons.Fetch, &buttons.Pull, &buttons.Push})
			{
				button->Enabled = false;
				button->Tooltip.clear();
				button->AsksWhichRemote = false;
			}

			// The full history's fetch has no button: all three wait for it.
			if (*state.Running == NetworkOperation::FullHistory)
			{
				return;
			}

			// The label stays; the slot reads the percent, empty until git counts, and the tooltip says the rest.
			ButtonText& running = ButtonOf(buttons, *state.Running);
			running.Running = true;
			running.Enabled = state.Cancellable;
			running.Count = state.Percent.has_value() ? std::to_string(*state.Percent) + "%" : std::string{};

			if (state.Cancellable)
			{
				running.Tooltip = RunningText(state) + "\n" + kClickToCancel;
			}
			else
			{
				running.Tooltip = *state.Running == NetworkOperation::Pull ? kLastStepTooltip : RunningText(state);
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
		struct ChipDrawn
		{
			bool Clicked = false;

			// Where the chip, or what stands in its place, ends; its start when nothing was drawn.
			float Right = 0.0f;
		};

		float ChipChrome()
		{
			return 2.0f * Em(kChipSide) + Em(kHeadDot) + Em(kChipGap);
		}

		// The width the chip takes with room to spare: its chrome, HEAD's name and its state.
		float ChipNaturalWidth(const WindowBarText& text)
		{
			if (text.Busy || text.Head.empty())
			{
				return Em(kChipPlaceholder);
			}

			const float state = text.HeadState.empty() ? 0.0f : Em(kChipGap) + WidthOf(text.HeadState);

			return std::min(Em(kChipMaxWidth), ChipChrome() + WidthOf(text.Head) + state + kMeasuringSlack);
		}

		ChipDrawn DrawHeadChip(const WindowBarDrawing& drawing, ImVec2 topLeft, float room)
		{
			const WindowBarText& text = drawing.Text;
			const WindowBarColours& colours = drawing.Colours;

			if (text.Busy || text.Head.empty())
			{
				DrawInPlaceOfChip(drawing, topLeft, room);
				return ChipDrawn{false, std::max(topLeft.x, ImGui::GetItemRectMax().x)};
			}

			const float height = Em(kChipHeight);
			const float side = Em(kChipSide);
			const float gap = Em(kChipGap);
			const float dot = Em(kHeadDot);
			const float maxWidth = std::min(Em(kChipMaxWidth), room);

			// Narrower than its dot and padding the chip is left out, so it never reaches into the network part.
			if (maxWidth < ChipChrome())
			{
				return ChipDrawn{false, topLeft.x};
			}

			float stateWidth = text.HeadState.empty() ? 0.0f : gap + WidthOf(text.HeadState);

			// WARNING: The state goes before the name is cut: it is shown only when the whole name and the state both fit.
			const bool showsState = maxWidth - ChipChrome() - WidthOf(text.Head) - stateWidth >= -kMeasuringSlack;
			stateWidth = showsState ? stateWidth : 0.0f;
			const std::string name = FitToWidth(text.Head, maxWidth - 2.0f * side - dot - gap - stateWidth);
			const float width = 2.0f * side + dot + gap + WidthOf(name) + stateWidth;

			ImGui::SetCursorScreenPos(topLeft);
			const bool clicked = ImGui::InvisibleButton("##headChip", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			ShowTooltipOfLastItem(HeadChipTooltip(text));

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
			drawList->AddRectFilled(topLeft, bottomRight, hovered ? colours.ChipHoveredFill : colours.ChipFill, Em(kChipRounding));
			drawList->AddRect(topLeft, bottomRight, colours.Rule, Em(kChipRounding));

			if (drawing.Places != nullptr)
			{
				drawing.Places->Chip = Place{topLeft, bottomRight};
			}

			const float centreY = topLeft.y + height * kHalf;
			const float textTop = centreY - ImGui::GetTextLineHeight() * kHalf;
			float x = topLeft.x + side;

			DrawHeadDot(drawList, ImVec2(x + dot * kHalf, centreY), colours);
			x += dot + gap;

			drawList->AddText(ImVec2(x, textTop), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());

			if (!text.HeadState.empty() && showsState)
			{
				drawList->AddText(ImVec2(x + WidthOf(name) + gap, textTop), colours.Match, text.HeadState.c_str());
			}

			return ChipDrawn{clicked, bottomRight.x};
		}

		// The repository's icon and its folder's name in bold; returns the width it took.
		float DrawRepository(const WindowBarDrawing& drawing, ImVec2 at, float room)
		{
			const float iconWidth = WidthOf(ICON_LC_FOLDER) + Em(kIconGap);

			// A bar too tight for the icon has no room for the repository: the network part keeps its place instead.
			if (room < iconWidth)
			{
				return 0.0f;
			}

			ImGui::SetCursorScreenPos(at);
			ImGui::TextUnformatted(ICON_LC_FOLDER);
			ImGui::SameLine(0.0f, Em(kIconGap));

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
			ImGui::TextUnformatted(FitToWidth(drawing.Text.Repository, room - iconWidth).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

			const float width = ImGui::GetItemRectMax().x - at.x;

			if (drawing.Places != nullptr)
			{
				drawing.Places->Repository = Place{at, ImVec2(at.x + width, at.y + ImGui::GetTextLineHeight())};
			}

			ImGui::SetCursorScreenPos(at);
			ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
			ShowTooltipOfLastItem(Core::ToUtf8(Core::NormalizeDirectory(drawing.WorkingDirectory)));

			return width;
		}

		float RepositoryNaturalWidth(const WindowBarDrawing& drawing)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const float name = WidthOf(drawing.Text.Repository);
			ImGui::PopFont();

			return WidthOf(ICON_LC_FOLDER) + Em(kIconGap) + name;
		}

		float RemotesNaturalWidth(const WindowBarDrawing& drawing)
		{
			Core::PushInterfaceFont(ImGui::GetFontSize() * kRemotesText / kMockupFont);
			const float width = WidthOf(drawing.Text.Remotes);
			ImGui::PopFont();

			return width;
		}

		// How the room left of the network part is shared as it shrinks. The bar gives space up in this order: the remotes'
		// text, down to nothing; the repository's name, down to its minimum; the chip's state words; the chip's name.
		struct BarRooms
		{
			float Repository = 0.0f;
			float Remotes = 0.0f;
			float Chip = 0.0f;
		};

		// `available` is the room less the gaps between the parts.
		BarRooms ShareBar(const WindowBarDrawing& drawing, float available)
		{
			const float repository = RepositoryNaturalWidth(drawing);
			const float repositoryMinimum = std::min(repository, Em(kRepositoryMinimum));
			const float chip = ChipNaturalWidth(drawing.Text);

			BarRooms rooms;
			rooms.Remotes = std::clamp(available - repository - chip, 0.0f, RemotesNaturalWidth(drawing));
			rooms.Repository = std::clamp(available - chip, repositoryMinimum, repository);

			// Too tight even for the repository's minimum and the chip's chrome: the repository keeps what there is.
			if (available < repositoryMinimum + ChipChrome())
			{
				rooms.Repository = available;
				return rooms;
			}

			rooms.Chip = available - rooms.Repository - rooms.Remotes;
			return rooms;
		}

		// A failed read, right-aligned between `left` and `right`, cut to fit, with the whole message in the tooltip.
		void DrawProblem(const WindowBarDrawing& drawing, float left, float right, float centreY)
		{
			const float room = right - left;
			const std::string shown = FitToWidth(drawing.Problem, room);

			if (shown.empty())
			{
				return;
			}

			const float width = WidthOf(shown);
			const ImVec2 at(right - width, centreY - ImGui::GetTextLineHeight() * kHalf);

			ImGui::GetWindowDrawList()->AddText(at, drawing.Colours.Problem, shown.c_str());

			if (drawing.Places != nullptr)
			{
				drawing.Places->Problem = Place{at, ImVec2(at.x + width, at.y + ImGui::GetTextLineHeight())};
			}

			ImGui::SetCursorScreenPos(at);
			ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
			ShowTooltipOfLastItem(std::string{drawing.Problem});
		}

		struct ButtonLook
		{
			const ButtonText* Text;
			const char* Icon;
			const char* Id;
			Place* Where;
		};

		// WARNING: Whatever the button says, never its count or its running state: a button is as wide on the first frame as
		// on any other, so nothing beside it moves when a count or a percent changes.
		float CountSlotWidth()
		{
			return std::max(WidthOf(kWidestCount), WidthOf(kWidestPercent));
		}

		float ButtonWidth(const ButtonLook& look)
		{
			return 2.0f * Em(kButtonSide) + WidthOf(look.Icon) + Em(kButtonInnerGap) + WidthOf(look.Text->Label) + Em(kButtonInnerGap) +
				   CountSlotWidth();
		}

		float WhenWidth()
		{
			Core::PushInterfaceFont(ImGui::GetFontSize() * kWhenText / kMockupFont);
			const float width = std::max(WidthOf(kWidestWhen), WidthOf(kJustNow));
			ImGui::PopFont();

			return width;
		}

		// How far left the network part reaches when it ends at `right`.
		float NetworkLeft(const std::array<ButtonLook, 3>& looks, float right)
		{
			float width = WhenWidth() + Em(kWhenGap);

			for (const ButtonLook& look : looks)
			{
				width += ButtonWidth(look) + Em(kButtonGap);
			}

			return right - width;
		}

		// WARNING: Drawn by hand on an InvisibleButton, as the HEAD chip is, and not through Core::TextButton, which draws a
		// label alone: this one has an icon that becomes the busy mark and a count in the match colour.
		// A button: its icon a little dimmer than its label, and its count in the match colour; a disabled one is all dim.
		NetworkClick DrawNetworkButton(const WindowBarDrawing& drawing, const ButtonLook& look, ImVec2 topLeft, NetworkClick onClick)
		{
			const ButtonText& button = *look.Text;
			const WindowBarColours& colours = drawing.Colours;
			const float width = ButtonWidth(look);
			const float height = Em(kChipHeight);

			if (look.Where != nullptr)
			{
				*look.Where = Place{topLeft, ImVec2(topLeft.x + width, topLeft.y + height)};
			}

			ImGui::SetCursorScreenPos(topLeft);
			const bool clicked = ImGui::InvisibleButton(look.Id, ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered() && button.Enabled;
			ShowTooltipOfLastItem(button.Tooltip);

			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			if (hovered)
			{
				drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), colours.ChipHoveredFill, Em(kButtonRounding));
			}

			const float textTop = topLeft.y + (height - ImGui::GetTextLineHeight()) * kHalf;
			const ImU32 ink = button.Enabled ? ImGui::GetColorU32(ImGuiCol_Text) : colours.Dim;
			float x = topLeft.x + Em(kButtonSide);

			if (button.Running)
			{
				// The busy mark takes the icon's place; the label stays where it was.
				Core::DrawLoadingSpinner(ImVec2(x + WidthOf(look.Icon) * kHalf, topLeft.y + height * kHalf));
			}
			else
			{
				drawList->AddText(ImVec2(x, textTop), button.Enabled ? colours.Icon : colours.Dim, look.Icon);
			}

			x += WidthOf(look.Icon) + Em(kButtonInnerGap);

			drawList->AddText(ImVec2(x, textTop), ink, button.Label.c_str());
			x += WidthOf(button.Label);

			if (!button.Count.empty())
			{
				drawList->AddText(ImVec2(x + Em(kButtonInnerGap), textTop), button.Enabled ? colours.Match : colours.Dim, button.Count.c_str());
			}

			if (!clicked || !button.Enabled)
			{
				return NetworkClick::None;
			}

			return button.Running ? NetworkClick::Cancel : onClick;
		}

		// The three buttons from `left`, then the age of the last fetch, dim, in the room kept for the widest.
		NetworkClick DrawNetwork(const WindowBarDrawing& drawing, const std::array<ButtonLook, 3>& looks, float left, float centreY)
		{
			const std::array<NetworkClick, 3> clicks{NetworkClick::Fetch, NetworkClick::Pull, NetworkClick::Push};
			NetworkClick answer = NetworkClick::None;
			float x = left;

			for (std::size_t index = 0; index < looks.size(); ++index)
			{
				const NetworkClick clicked = DrawNetworkButton(drawing, looks[index], ImVec2(x, centreY - Em(kChipHeight) * kHalf), clicks[index]);
				answer = clicked != NetworkClick::None ? clicked : answer;
				x += ButtonWidth(looks[index]) + Em(kButtonGap);
			}

			const std::string& when = drawing.Network->FetchedAge;

			if (!when.empty())
			{
				Core::PushInterfaceFont(ImGui::GetFontSize() * kWhenText / kMockupFont);
				const ImVec2 at(x + Em(kWhenGap), centreY - ImGui::GetTextLineHeight() * kHalf);
				ImGui::GetWindowDrawList()->AddText(at, drawing.Colours.Dim, when.c_str());

				if (drawing.Places != nullptr)
				{
					drawing.Places->Age = Place{at, ImVec2(at.x + WidthOf(when), at.y + ImGui::GetTextLineHeight())};
				}

				ImGui::PopFont();
			}

			return answer;
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

			if (drawing.Places != nullptr)
			{
				const ImVec2 at(left, centreY - ImGui::GetTextLineHeight() * kHalf);
				drawing.Places->Remotes = Place{at, ImVec2(at.x + width, at.y + ImGui::GetTextLineHeight())};
			}

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

	NetworkButtons DescribeNetworkButtons(const WindowBarInput& input, const NetworkState& state,
										  std::optional<std::chrono::sys_seconds> lastFetch, std::chrono::sys_seconds now, bool gitFound)
	{
		NetworkButtons buttons;
		buttons.Fetch.Label = "Fetch";
		buttons.Pull.Label = "Pull";
		buttons.Push.Label = "Push";

		if (lastFetch.has_value())
		{
			buttons.FetchedAge = FetchedText(*lastFetch, now);
		}

		const bool listed = input.Branches != nullptr && input.Remotes != nullptr;

		if (listed && !gitFound)
		{
			buttons.Fetch.Tooltip = kGitMissing;
			buttons.Pull.Tooltip = kGitMissing;
			buttons.Push.Tooltip = kGitMissing;
		}
		else if (listed)
		{
			DescribeFetch(input, buttons.Fetch);

			if (input.Head != nullptr && !input.CheckingOut)
			{
				DescribeCheckedOut(input, buttons);
			}
		}

		if (state.Running.has_value())
		{
			ApplyRunning(state, buttons);
		}

		return buttons;
	}

	std::string HeadChipTooltip(const WindowBarText& text)
	{
		return text.OnBranch ? text.HeadTooltip + "\n" + kChipClickHint : text.HeadTooltip;
	}

	std::string WindowBarProblem(const WindowBarFailures& failures)
	{
		std::string problem;

		for (const Core::Error* const failure : {failures.Status, failures.Branches, failures.Log, failures.Lines})
		{
			if (failure == nullptr)
			{
				continue;
			}

			problem += (problem.empty() ? "" : " \xC2\xB7 ") + failure->Message;
		}

		return problem;
	}

	SettledWindowBar SettleWindowBar(const WindowBarInput& input, bool headFailed, const WindowBarText* held)
	{
		const bool headKnown = input.Head != nullptr || headFailed;
		const bool remotesKnown = input.Remotes != nullptr || input.RemotesFailure != nullptr;

		// The folder's name stands first on the bar, so a name landing after the rest would push everything right. A
		// repository that could not be opened has no folder to name, and its HEAD failure ends the waiting.
		const bool folderKnown = !input.WorkingDirectory.empty() || headFailed;

		if (headKnown && remotesKnown && folderKnown)
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
		colours.Icon = Mixed(text, colours.Dim, kIconShare);
		colours.Match = Mixed(accent, text, kLighterShare);
		colours.Rule = PackedColour(ColourFromScheme(scheme, ChromeColor::Border));
		colours.ChipFill = PackedColour(ColourFromScheme(scheme, ChromeColor::Surface));
		colours.ChipHoveredFill = PackedColour(ColourFromScheme(scheme, ChromeColor::SurfaceHovered));
		colours.Accent = accent;
		colours.OnAccent = background;
		colours.AskFill = Mixed(background, warning, kAskTint);
		colours.AskQuestion = Mixed(warning, text, kLighterShare);
		colours.Automatic = ReadGitPalette(scheme).GutterAdded;
		colours.Problem = warning;

		return colours;
	}

	WindowBarClick DrawWindowBar(const WindowBarDrawing& drawing)
	{
		const ImVec2 origin(ImGui::GetCursorScreenPos().x - drawing.Bleed.x, ImGui::GetCursorScreenPos().y - drawing.Bleed.y);
		const float width = ImGui::GetContentRegionAvail().x + 2.0f * drawing.Bleed.x;
		const float height = Em(kBarHeight);
		const float side = Em(kBarSide);
		const float gap = Em(kGap);
		const float barRight = origin.x + width - side;
		const float centreY = origin.y + height * kHalf;
		const float lineHeight = ImGui::GetTextLineHeight();

		const NetworkButtons* const network = drawing.Network;
		const std::array<ButtonLook, 3> looks = network != nullptr
			? std::array<ButtonLook, 3>{
				  ButtonLook{&network->Fetch, ICON_LC_REFRESH_CW, "##networkFetch", drawing.Places ? &drawing.Places->Fetch : nullptr},
				  ButtonLook{&network->Pull, ICON_LC_ARROW_DOWN, "##networkPull", drawing.Places ? &drawing.Places->Pull : nullptr},
				  ButtonLook{&network->Push, ICON_LC_ARROW_UP, "##networkPush", drawing.Places ? &drawing.Places->Push : nullptr}}
			: std::array<ButtonLook, 3>{};

		// WARNING: The network part takes the right end, before the problem's text and the HEAD chip; its room is fixed by the
		// widest age, so the buttons stay where they are as the age changes.
		const float networkLeft = network != nullptr ? NetworkLeft(looks, barRight) : barRight + gap;
		const float right = networkLeft - gap;
		const float room = std::max(0.0f, right - (origin.x + side));

		// What lies between the parts: after the repository, after the remotes, and the rule with its gap.
		const float between = 3.0f * gap + 1.0f;
		const BarRooms rooms = ShareBar(drawing, std::max(0.0f, room - between));

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		drawList->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + width, origin.y + height - 1.0f),
						  drawing.Colours.Rule);

		float x = origin.x + side;
		x += DrawRepository(drawing, ImVec2(x, centreY - lineHeight * kHalf), rooms.Repository) + gap;
		x += DrawRemotes(drawing, x, centreY, rooms.Remotes) + gap;

		const float separatorHalf = Em(kSeparatorHeight) * kHalf;
		drawList->AddLine(ImVec2(x, centreY - separatorHalf), ImVec2(x, centreY + separatorHalf), drawing.Colours.Rule);
		x += 1.0f + gap;

		WindowBarClick click;
		const ChipDrawn chip = DrawHeadChip(drawing, ImVec2(x, centreY - Em(kChipHeight) * kHalf), std::min(rooms.Chip, right - x));
		click.Head = chip.Clicked;

		// WARNING: After the chip, or after the rule when no chip was drawn: never after whichever item came before.
		if (!drawing.Problem.empty())
		{
			DrawProblem(drawing, chip.Right + gap, right, centreY);
		}

		if (network != nullptr)
		{
			click.Network = DrawNetwork(drawing, looks, networkLeft, centreY);
		}

		// The bar is one item high however many it drew, so the cursor lands below it, as far down as without the bleed.
		// WARNING: The item keeps to the content region's width, so the bleed, which only the rule and the room for the
		// items reach into, never widens what the window scrolls; the items start a bar side in, more than the padding.
		ImGui::SetCursorScreenPos(ImVec2(origin.x + drawing.Bleed.x, origin.y));
		ImGui::Dummy(ImVec2(width - 2.0f * drawing.Bleed.x, height + drawing.Bleed.y));

		return click;
	}
}
