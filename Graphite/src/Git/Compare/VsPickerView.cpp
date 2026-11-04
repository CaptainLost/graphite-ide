#include "Git/Compare/VsPickerView.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextMetrics.h"

#include "UI/FittedText.h"
#include "UI/Lists/FilterField.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		constexpr float kHalf = 0.5f;
		constexpr float kMockupFont = 13.0f;
		constexpr float kKeepFontSize = 0.0f;

		constexpr float kPopupWidth = 640.0f;
		constexpr float kEdge = 16.0f;
		constexpr float kRoundingOfPopup = 8.0f;
		constexpr float kPopupPadding = 6.0f;
		constexpr float kRowHeight = 26.0f;
		constexpr float kSide = 12.0f;
		constexpr float kGap = 8.0f;
		constexpr float kRadio = 12.0f;
		constexpr float kNameWidth = 170.0f;
		constexpr float kCountsWidth = 132.0f;
		constexpr float kBaseLineGap = 10.0f;
		constexpr float kChipSide = 5.0f;
		constexpr float kChipRounding = 4.0f;
		constexpr float kChipPadding = 1.0f;
		constexpr float kRadioRing = 1.5f;
		constexpr float kRadioInset = 2.5f;
		constexpr float kRestingRingAlpha = 0.4f; // as a tick box's resting outline
		constexpr ImU32 kChipFillAlphaByte = 0x38u; // as the details' "In bases" chips

		constexpr float kHeadingText = 11.0f;
		constexpr float kChipText = 10.5f;
		constexpr float kFootText = 11.5f;

		constexpr const char* kAutoChip = "auto";
		constexpr const char* kGuessedChip = "guessed";

		float Em(float mockupPixels)
		{
			return WindowBarEm(mockupPixels);
		}

		std::string UpperCased(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
						   [](unsigned char letter) { return static_cast<char>(std::toupper(letter)); });
			return text;
		}

		// A chip the size of its text; returns its width. `draw` false only measures.
		float DrawChip(ImDrawList* drawList, ImVec2 at, const std::string& text, ImU32 fill, ImU32 ink, bool draw, float rowHeight)
		{
			Core::PushInterfaceFont(ImGui::GetFontSize() * kChipText / kMockupFont);

			const float side = Em(kChipSide);
			const float width = Core::TextWidth(text) + 2.0f * side;

			if (draw)
			{
				const float height = ImGui::GetTextLineHeight() + 2.0f * Em(kChipPadding);
				const float top = at.y + (rowHeight - height) * kHalf;
				drawList->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + height), fill, Em(kChipRounding));
				drawList->AddText(ImVec2(at.x + side, top + Em(kChipPadding)), ink, text.c_str());
			}

			ImGui::PopFont();
			return width;
		}

		float ChipSlotWidth()
		{
			return DrawChip(nullptr, ImVec2(), kAutoChip, 0, 0, false, 0.0f);
		}

		void DrawRadio(ImDrawList* drawList, ImVec2 centre, bool chosen, const WindowBarColours& colours)
		{
			const float radius = Em(kRadio) * kHalf;

			if (chosen)
			{
				drawList->AddCircleFilled(centre, radius, colours.Accent);
				drawList->AddCircleFilled(centre, radius - Em(kRadioInset), ImGui::GetColorU32(ImGuiCol_PopupBg));
			}
			else
			{
				drawList->AddCircle(centre, radius - Em(kRadioRing) * kHalf, Core::WithAlphaScaled(colours.Dim, kRestingRingAlpha), 0,
									Em(kRadioRing));
			}
		}

		// "6 ahead · 80 behind": the ahead count bold in the match colour, the rest dim; a pending "…" dim.
		void DrawCounts(ImDrawList* drawList, const std::string& counts, float right, float top, const WindowBarColours& colours)
		{
			constexpr const char* kBetween = " \xC2\xB7 ";
			const std::size_t split = counts.find(kBetween);

			if (split == std::string::npos)
			{
				drawList->AddText(ImVec2(right - Core::TextWidth(counts), top), colours.Dim, counts.c_str());
				return;
			}

			const std::string ahead = counts.substr(0, split);
			const std::string rest = counts.substr(split);

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const float aheadWidth = Core::TextWidth(ahead);
			ImGui::PopFont();

			const float left = right - aheadWidth - Core::TextWidth(rest);

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			drawList->AddText(ImVec2(left, top), colours.Match, ahead.c_str());
			ImGui::PopFont();
			drawList->AddText(ImVec2(left + aheadWidth, top), colours.Dim, rest.c_str());
		}

		// One option that names a ref, or the Nothing option: true when clicked.
		bool DrawOptionRow(const VsOption& option, const LineRead& read, float width, const WindowBarColours& colours)
		{
			const float height = Em(kRowHeight);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::Selectable("##option", false, ImGuiSelectableFlags_None, ImVec2(width, height));

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const float textTop = origin.y + (height - ImGui::GetTextLineHeight()) * kHalf;
			const float side = Em(kSide);
			const float gap = Em(kGap);
			const float slot = ChipSlotWidth();

			DrawRadio(drawList, ImVec2(origin.x + side + Em(kRadio) * kHalf, origin.y + height * kHalf), option.Chosen, colours);

			const float nameLeft = origin.x + side + Em(kRadio) + gap;
			const float nameRight = nameLeft + Em(kNameWidth);
			const float slotLeft = origin.x + width - side - slot;
			const float countsRight = slotLeft - gap;
			const float countsLeft = countsRight - Em(kCountsWidth);

			// A remote branch's remote stands dim before its name.
			const std::string remote = option.Ref.has_value() && *option.Ref < read.Refs.size() ? read.Refs[*option.Ref].Remote : std::string{};
			const std::string prefix = !remote.empty() && option.Name.starts_with(remote + "/") ? remote + "/" : std::string{};
			const std::string shownPrefix = FitToWidth(prefix, nameRight - nameLeft);
			const float prefixWidth = Core::TextWidth(shownPrefix);

			drawList->AddText(ImVec2(nameLeft, textTop), colours.Dim, shownPrefix.c_str());
			drawList->AddText(ImVec2(nameLeft + prefixWidth, textTop), ImGui::GetColorU32(ImGuiCol_Text),
							  FitToWidth(option.Name.substr(prefix.size()), nameRight - nameLeft - prefixWidth).c_str());

			const float whyLeft = nameRight + gap;
			float whyRoom = std::max(0.0f, countsLeft - gap - whyLeft);
			float guessedWidth = 0.0f;

			if (option.Guessed)
			{
				guessedWidth = DrawChip(nullptr, ImVec2(), kGuessedChip, 0, 0, false, height) + Em(kGap) * kHalf;
			}

			const std::string why = FitToWidth(option.Why, std::max(0.0f, whyRoom - guessedWidth));
			drawList->AddText(ImVec2(whyLeft, textTop), colours.Dim, why.c_str());

			if (option.Guessed)
			{
				const float chipLeft = whyLeft + Core::TextWidth(why) + Em(kGap) * kHalf;
				DrawChip(drawList, ImVec2(chipLeft, origin.y), kGuessedChip, colours.AskFill, colours.AskQuestion, true, height);
			}

			if (!option.Counts.empty())
			{
				DrawCounts(drawList, option.Counts, countsRight, textTop, colours);
			}

			if (option.Automatic)
			{
				DrawChip(drawList, ImVec2(slotLeft, origin.y), kAutoChip, Core::WithAlphaReplaced(colours.Automatic, kChipFillAlphaByte),
						 colours.Automatic, true, height);
			}

			return clicked;
		}

		void DrawTitle(const VsPicker& picker, float width, const WindowBarColours& colours)
		{
			const float left = Em(kSide);
			const std::string lead = "Compare ";
			const std::string tail = " with";
			const float room = std::max(0.0f, width - 2.0f * left - Core::TextWidth(lead) - Core::TextWidth(tail));

			ImGui::SetCursorPosX(left);
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextUnformatted(lead.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextUnformatted(FitToWidth(picker.Subject, room).c_str());
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextUnformatted(tail.c_str());
			ImGui::PopStyleColor();
		}

		void DrawBaseLine(const VsBaseLine& base, float width, const WindowBarColours& colours)
		{
			const float left = Em(kSide);
			const float nameWidth = Em(kNameWidth);

			ImGui::SetCursorPosX(left);
			ImGui::PushStyleColor(ImGuiCol_Text, base.OldCopy ? colours.Dim : ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::TextUnformatted(FitToWidth(base.Name, nameWidth).c_str());
			ImGui::PopStyleColor();

			ImGui::SameLine(left + nameWidth + Em(kBaseLineGap));
			ImGui::PushTextWrapPos(width - left);
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextWrapped("%s", base.Reason.c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}

		void DrawBases(const VsPicker& picker, float width, const WindowBarColours& colours)
		{
			ImGui::Separator();

			Core::PushInterfaceFont(ImGui::GetFontSize() * kHeadingText / kMockupFont);
			ImGui::SetCursorPosX(Em(kSide));
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextUnformatted(UpperCased(kVsBasesHeading).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

			for (const VsBaseLine& base : picker.Bases)
			{
				DrawBaseLine(base, width, colours);
			}

			if (picker.QuietBases > 0)
			{
				const VsBaseLine quiet{.Name = std::to_string(picker.QuietBases) + " more",
									   .Reason = "bases once, quiet for 90 days; folded in the list",
									   .OldCopy = true};
				DrawBaseLine(quiet, width, colours);
			}
		}

		void DrawFooter(const VsPicker& picker, float width, const WindowBarColours& colours)
		{
			ImGui::Separator();

			Core::PushInterfaceFont(ImGui::GetFontSize() * kFootText / kMockupFont);
			ImGui::SetCursorPosX(Em(kSide));
			ImGui::PushTextWrapPos(width - Em(kSide));
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextWrapped("%s", picker.Footer.c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
			ImGui::PopFont();
		}
	}

	void VsPickerView::Open(std::string branch, ImVec2 below)
	{
		m_Open = true;
		m_PopupRequested = true;
		m_Branch = std::move(branch);
		m_Search.clear();
		m_Below = below;
	}

	std::optional<LineAnswerChange> VsPickerView::Draw(const Drawing& drawing)
	{
		if (!m_Open)
		{
			return std::nullopt;
		}

		const std::optional<std::size_t> self = drawing.Lines.IndexOf(m_Branch);

		const std::function<std::optional<Core::LineDistance>(std::size_t)> distanceTo =
			[&](std::size_t ref) -> std::optional<Core::LineDistance> {
			return self.has_value() ? drawing.DistanceBetween(drawing.Read.Refs[*self].Tip, drawing.Read.Refs[ref].Tip) : std::nullopt;
		};

		const std::function<std::optional<Core::LineDistance>(const Core::OldCopy&)> copyDistance =
			[&](const Core::OldCopy& copy) { return drawing.DistanceBetween(drawing.Read.Refs[copy.Ref].Tip, drawing.Read.Refs[copy.Of].Tip); };

		const VsPicker picker = BuildVsPicker(VsPickerInput{.Read = drawing.Read,
															.Lines = drawing.Lines,
															.Branch = m_Branch,
															.Search = m_Search,
															.DistanceTo = distanceTo,
															.OldCopyDistance = copyDistance,
															.Now = drawing.Now});

		if (m_PopupRequested)
		{
			ImGui::OpenPopup(kPopupId);
			m_PopupRequested = false;
		}

		const float regionWidth = drawing.RegionMax.x - drawing.RegionMin.x;
		const float width = std::max(0.0f, std::min(Em(kPopupWidth), regionWidth - Em(kEdge)));
		const float left = std::max(drawing.RegionMin.x + Em(kEdge) * kHalf,
									std::min(m_Below.x, drawing.RegionMax.x - Em(kEdge) * kHalf - width));

		ImGui::SetNextWindowPos(ImVec2(left, m_Below.y));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, Em(kPopupPadding)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Em(kRoundingOfPopup));
		const bool visible = ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar(2);

		if (!visible)
		{
			m_Open = false;
			return std::nullopt;
		}

		// A new read that no longer holds the branch ends the question.
		if (picker.Title.empty())
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			m_Open = false;
			return std::nullopt;
		}

		std::optional<LineAnswerChange> chosen;

		DrawTitle(picker, width, drawing.Colours);
		ImGui::Dummy(ImVec2(0.0f, Em(kPopupPadding) * kHalf));

		// The option rows touch, each as high as the mockup's.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

		for (std::size_t index = 0; index < picker.Options.size(); ++index)
		{
			const VsOption& option = picker.Options[index];

			ImGui::PushID(static_cast<int>(index));

			if (option.What == VsOption::Kind::AnotherBranch)
			{
				const std::string hint = option.Name + " " + option.Why;

				ImGui::SetCursorPosX(Em(kSide));
				DrawFilterField("##search", m_Search, FilterFieldLook{.Hint = hint.c_str(), .Width = width - 2.0f * Em(kSide)});
			}
			else if (DrawOptionRow(option, drawing.Read, width, drawing.Colours))
			{
				chosen = ChangeFor(picker, option, m_Branch);
			}

			ImGui::PopID();
		}

		ImGui::PopStyleVar();

		ImGui::Dummy(ImVec2(0.0f, Em(kPopupPadding)));
		DrawBases(picker, width, drawing.Colours);
		ImGui::Dummy(ImVec2(0.0f, Em(kPopupPadding) * kHalf));
		DrawFooter(picker, width, drawing.Colours);

		if (chosen.has_value())
		{
			ImGui::CloseCurrentPopup();
			m_Open = false;
		}

		ImGui::EndPopup();

		return chosen;
	}
}
