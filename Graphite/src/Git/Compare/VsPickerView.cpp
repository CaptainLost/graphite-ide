#include "Git/Compare/VsPickerView.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextMetrics.h"

#include "UI/FittedText.h"
#include "UI/Lists/FilterField.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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

		constexpr float kFindIcon = 12.0f;
		constexpr ImWchar kFirstPrintable = 32;

		// The footer's phrases that name the menu's entries, drawn in the bold face.
		constexpr const char* kFooterBold[] = {"Use as base", "Stop using as base"};

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

		// The mockup's "Another branch…" row: the search icon and the hint, dim, in an option row's height; true when clicked.
		bool DrawFindRow(const std::string& hint, float width, const WindowBarColours& colours)
		{
			const float height = Em(kRowHeight);
			const ImVec2 origin = ImGui::GetCursorScreenPos();

			// WARNING: A Selectable in a popup closes the popup when clicked; this one turns into the field in it instead.
			const bool clicked = ImGui::Selectable("##find", false, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(width, height));

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const float side = Em(kSide);
			const float iconSize = Em(kFindIcon);
			const float textTop = origin.y + (height - ImGui::GetTextLineHeight()) * kHalf;

			drawList->AddText(ImGui::GetFont(), iconSize, ImVec2(origin.x + side, origin.y + (height - iconSize) * kHalf), colours.Dim,
							  ICON_LC_SEARCH);
			drawList->AddText(ImVec2(origin.x + side + iconSize + Em(kGap), textTop), colours.Dim,
							  FitToWidth(hint, width - 2.0f * side - iconSize - Em(kGap)).c_str());

			return clicked;
		}

		// The search field in the place and height of the find row.
		void DrawFindField(std::string& search, float width, bool focus, unsigned int& inputId)
		{
			const float rowHeight = Em(kRowHeight);
			const float frameHeight = ImGui::GetFrameHeight();
			const float start = ImGui::GetCursorPosY();

			ImGui::SetCursorPos(ImVec2(Em(kSide), start + std::max(0.0f, (rowHeight - frameHeight) * kHalf)));

			if (focus)
			{
				ImGui::SetKeyboardFocusHere();
			}

			DrawFilterField("##search", search, FilterFieldLook{.Hint = "", .Width = width - 2.0f * Em(kSide), .InputId = &inputId});

			ImGui::SetCursorPosY(start);
			ImGui::Dummy(ImVec2(width, std::max(rowHeight, frameHeight)));
		}

		struct FooterWord
		{
			std::string Text;
			bool Bold = false;
			bool SpaceBefore = false;
		};

		// The footer cut into words, each knowing whether it stands in a bold phrase and whether a space precedes it.
		std::vector<FooterWord> FooterWordsOf(const std::string& footer)
		{
			struct Run
			{
				std::string Text;
				bool Bold;
			};

			std::vector<Run> runs{Run{footer, false}};

			for (const char* phrase : kFooterBold)
			{
				std::vector<Run> next;

				for (Run& run : runs)
				{
					const std::size_t at = run.Bold ? std::string::npos : run.Text.find(phrase);

					if (at == std::string::npos)
					{
						next.push_back(std::move(run));
						continue;
					}

					next.push_back(Run{run.Text.substr(0, at), false});
					next.push_back(Run{phrase, true});
					next.push_back(Run{run.Text.substr(at + std::string(phrase).size()), false});
				}

				runs = std::move(next);
			}

			std::vector<FooterWord> words;
			bool spaceBefore = false;

			for (const Run& run : runs)
			{
				std::size_t at = 0;

				while (at < run.Text.size())
				{
					if (run.Text[at] == ' ')
					{
						spaceBefore = true;
						++at;
						continue;
					}

					const std::size_t end = std::min(run.Text.find(' ', at), run.Text.size());
					words.push_back(FooterWord{run.Text.substr(at, end - at), run.Bold, spaceBefore});
					spaceBefore = false;
					at = end;
				}
			}

			return words;
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
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::PushStyleColor(ImGuiCol_Text, base.OldCopy ? colours.Dim : ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::TextUnformatted(FitToWidth(base.Name, nameWidth).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

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

			// WARNING: Wrapped word by word, as the entries named in bold change the face mid-line; the footer is one fixed
			// paragraph (VsPicker.cpp) and a newline in it would be drawn as a glyph.
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float right = origin.x + width - 2.0f * Em(kSide);
			const float lineHeight = ImGui::GetTextLineHeight();
			const float spaceWidth = Core::TextWidth(" ");
			const float size = ImGui::GetFontSize();
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			float x = origin.x;
			float y = origin.y;

			for (const FooterWord& word : FooterWordsOf(picker.Footer))
			{
				if (word.Bold)
				{
					ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), size);
				}

				const float wordWidth = Core::TextWidth(word.Text);
				const float gap = word.SpaceBefore && x > origin.x ? spaceWidth : 0.0f;

				if (x > origin.x && x + gap + wordWidth > right)
				{
					x = origin.x;
					y += lineHeight;
				}
				else
				{
					x += gap;
				}

				drawList->AddText(ImVec2(x, y), word.Bold ? ImGui::GetColorU32(ImGuiCol_Text) : colours.Dim, word.Text.c_str());
				x += wordWidth;

				if (word.Bold)
				{
					ImGui::PopFont();
				}
			}

			ImGui::Dummy(ImVec2(width, y + lineHeight - origin.y));
			ImGui::PopFont();
		}
	}

	void VsPickerView::Open(std::string branch, ImVec2 below)
	{
		m_Open = true;
		m_PopupRequested = true;
		m_Branch = std::move(branch);
		m_Search.clear();
		m_Searching = false;
		m_FocusSearch = false;
		m_SearchWasActive = false;
		m_Below = below;
	}

	// A character typed while the picker is open starts the search; the field takes the character in this same frame.
	void VsPickerView::DrawAnotherBranch(const VsOption& option, float width, const WindowBarColours& colours)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const bool typed = !io.KeyCtrl && !io.KeyAlt && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
						   std::any_of(io.InputQueueCharacters.begin(), io.InputQueueCharacters.end(),
									   [](ImWchar character) { return character >= kFirstPrintable; });

		if (!m_Searching && typed)
		{
			m_Searching = true;
			m_FocusSearch = true;
			m_SearchWasActive = false;
		}

		if (!m_Searching)
		{
			if (DrawFindRow(option.Name + " " + option.Why, width, colours))
			{
				m_Searching = true;
				m_FocusSearch = true;
				m_SearchWasActive = false;
			}

			return;
		}

		unsigned int inputId = 0;
		DrawFindField(m_Search, width, m_FocusSearch, inputId);
		m_FocusSearch = false;

		// Esc or the clear button leaves the field empty and unfocused: the row comes back.
		const bool active = inputId != 0 && ImGui::GetActiveID() == inputId;

		if (m_SearchWasActive && !active && m_Search.empty())
		{
			m_Searching = false;
		}

		m_SearchWasActive = active;
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
				DrawAnotherBranch(option, width, drawing.Colours);
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
