#include "Git/Sidebar/SidebarView.h"

#include "Git/Branches/BranchListFilter.h"
#include "Theme/ChromeColor.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"
#include "UI/FittedText.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"

#include "Core/Base/Color.h"
#include "Core/Base/Count.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace Graphite {

	namespace {

		// Every length below is the mockup's px at its 13 px font, scaled to the font in use.
		constexpr float kMockupFont = 13.0f;
		constexpr float kHalf = 0.5f;

		constexpr float kTwoLineHeight = 38.0f;
		constexpr float kColumnWidth = 300.0f;
		constexpr float kFieldHeight = 26.0f;
		constexpr float kFieldTop = 7.0f;
		constexpr float kFieldBottom = 6.0f;
		constexpr float kFieldSide = 8.0f;
		constexpr float kRowSide = 8.0f;
		constexpr float kIndent = 14.0f;
		constexpr float kGap = 6.0f;
		constexpr float kTwoLineGap = 8.0f;
		constexpr float kLeadSlot = 14.0f;
		constexpr float kRefLeadSlot = 10.0f;
		constexpr float kChevronSize = 12.0f;
		constexpr float kIconSize = 13.0f;
		constexpr float kSmallText = 11.5f;
		constexpr float kCountText = 12.0f;
		constexpr float kHeadDot = 7.0f;
		constexpr float kHeadGlow = 11.0f;
		constexpr float kLineSpacing = 1.2f;
		constexpr float kLinesApart = 2.0f;
		constexpr float kHeadGlowAlpha = 0.25f;
		constexpr float kLighterShare = 0.4f;
		constexpr float kRuleThickness = 1.0f;
		constexpr float kUnderlineThickness = 1.0f;

		constexpr const char* kSeparator = " \xC2\xB7 ";
		constexpr const char* kSearchHint = "Branches, tags, authors, remotes";

		float Em(float mockupPixels)
		{
			return ImGui::GetFontSize() * mockupPixels / kMockupFont;
		}

		ImU32 PackedColour(const Core::Color& colour)
		{
			return IM_COL32(Core::ChannelToByte(colour.R), Core::ChannelToByte(colour.G), Core::ChannelToByte(colour.B),
							Core::ChannelToByte(colour.A));
		}

		ImU32 Lightened(const Core::Color& colour, const Core::Color& toward)
		{
			const ImVec4 from = ImGui::ColorConvertU32ToFloat4(PackedColour(colour));
			const ImVec4 to = ImGui::ColorConvertU32ToFloat4(PackedColour(toward));

			return ImGui::ColorConvertFloat4ToU32(ImLerp(from, to, kLighterShare));
		}

		ImU32 WithAlphaShare(ImU32 colour, float share)
		{
			ImVec4 faded = ImGui::ColorConvertU32ToFloat4(colour);
			faded.w *= share;

			return ImGui::ColorConvertFloat4ToU32(faded);
		}

		// The text size is set for the life of this object.
		class TextAtSize final
		{
		public:
			explicit TextAtSize(float size) { Core::PushInterfaceFont(size); }
			~TextAtSize() { ImGui::PopFont(); }

			TextAtSize(const TextAtSize&) = delete;
			TextAtSize& operator=(const TextAtSize&) = delete;
			TextAtSize(TextAtSize&&) = delete;
			TextAtSize& operator=(TextAtSize&&) = delete;
		};

		float WidthOf(std::string_view text)
		{
			return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
		}

		void DrawText(ImVec2 at, ImU32 colour, std::string_view text)
		{
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, colour, text.data(),
												text.data() + text.size());
		}

		std::string CountText(const SidebarRow& row)
		{
			if (row.Total.has_value())
			{
				return Core::FormatCount(row.Count) + " of " + Core::FormatCount(*row.Total);
			}

			return Core::FormatCount(row.Count);
		}

		std::string Uppercase(std::string_view text)
		{
			std::string upper(text);

			for (char& character : upper)
			{
				character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
			}

			return upper;
		}

		bool IsTwoLine(const SidebarRow& row)
		{
			return row.Kind == SidebarRowKind::Branch || row.Kind == SidebarRowKind::Base;
		}

		float HeightOf(const SidebarRow& row)
		{
			return IsTwoLine(row) ? SidebarTwoLineRowHeight() : ListRowPitch();
		}

		bool OpensMenu(const SidebarRow& row)
		{
			return IsTwoLine(row) || row.Kind == SidebarRowKind::Stash;
		}

		// Rows whose line 1 and line 2 come from `textOf`.
		bool HasText(const SidebarRow& row)
		{
			return IsTwoLine(row) || row.Kind == SidebarRowKind::Stash;
		}

		char LowerOf(char character)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}

		// Where each search word occurs in `text`, ignoring case; overlapping or touching occurrences are one range.
		std::vector<std::pair<std::size_t, std::size_t>> MatchRanges(std::string_view text,
																	 const std::vector<std::string>& loweredWords)
		{
			std::vector<std::pair<std::size_t, std::size_t>> ranges;

			for (const std::string& word : loweredWords)
			{
				if (word.empty() || word.size() > text.size())
				{
					continue;
				}

				for (std::size_t at = 0; at + word.size() <= text.size(); ++at)
				{
					bool same = true;

					for (std::size_t offset = 0; same && offset < word.size(); ++offset)
					{
						same = LowerOf(text[at + offset]) == word[offset];
					}

					if (same)
					{
						ranges.emplace_back(at, at + word.size());
					}
				}
			}

			std::ranges::sort(ranges);
			std::vector<std::pair<std::size_t, std::size_t>> merged;

			for (const std::pair<std::size_t, std::size_t>& range : ranges)
			{
				if (!merged.empty() && range.first <= merged.back().second)
				{
					merged.back().second = std::max(merged.back().second, range.second);
				}
				else
				{
					merged.push_back(range);
				}
			}

			return merged;
		}

		bool IsSelected(const SidebarRow& row, const SidebarSelection& selected)
		{
			if (IsTwoLine(row))
			{
				const bool isLocalCopy = row.LocalCopy.has_value() && selected.Ref == row.LocalCopy;
				return row.Ref.has_value() && (selected.Ref == row.Ref || isLocalCopy);
			}

			return row.Kind == SidebarRowKind::Stash && row.Stash.has_value() && selected.Stash == row.Stash;
		}

		// Pieces side by side, the ones in a matched range in `matchInk`.
		void DrawHighlightedText(ImVec2 at, ImU32 ink, ImU32 matchInk, std::string_view text,
								 const std::vector<std::string>& loweredWords)
		{
			std::size_t done = 0;
			float x = at.x;

			const auto piece = [&](std::size_t from, std::size_t to, ImU32 colour) {
				if (from >= to)
				{
					return;
				}

				const std::string_view part = text.substr(from, to - from);
				DrawText(ImVec2(x, at.y), colour, part);
				x += WidthOf(part);
			};

			for (const std::pair<std::size_t, std::size_t>& range : MatchRanges(text, loweredWords))
			{
				piece(done, range.first, ink);
				piece(range.first, range.second, matchInk);
				done = range.second;
			}

			piece(done, text.size(), ink);
		}

		// WARNING: A row keeps its id when the rows are rebuilt around it, so keyboard focus and an open menu stay on
		// it. Returns how many ids were pushed.
		int PushRowId(const SidebarRow& row)
		{
			ImGui::PushID(static_cast<int>(row.Kind));
			ImGui::PushID(row.Key.c_str());
			ImGui::PushID(row.Label.c_str());

			if (row.Ref.has_value())
			{
				ImGui::PushID(static_cast<int>(*row.Ref));
			}
			else if (row.Stash.has_value())
			{
				ImGui::PushID(static_cast<int>(*row.Stash));
			}
			else if (row.Link.has_value())
			{
				ImGui::PushID(static_cast<int>(*row.Link));
			}
			else
			{
				ImGui::PushID(0);
			}

			constexpr int kPushed = 4;
			return kPushed;
		}

		void PopRowId(int pushed)
		{
			for (int id = 0; id < pushed; ++id)
			{
				ImGui::PopID();
			}
		}

		struct RowBox
		{
			float Left = 0.0f; // where content starts at depth 0
			float Right = 0.0f;
			float Top = 0.0f;
			float Height = 0.0f;
		};

		float CentredTop(const RowBox& box)
		{
			return box.Top + std::floor((box.Height - ImGui::GetFontSize()) * kHalf);
		}

		float IndentedLeft(const RowBox& box, const SidebarRow& row)
		{
			return box.Left + static_cast<float>(row.Depth) * Em(kIndent);
		}

		void DrawChevronAt(float left, const RowBox& box, bool open, ImU32 ink)
		{
			const TextAtSize size(Em(kChevronSize));
			DrawText(ImVec2(left, CentredTop(box)), ink, ChevronGlyph(open));
		}

		void DrawIconAt(float left, const RowBox& box, const char* glyph, ImU32 ink)
		{
			const TextAtSize size(Em(kIconSize));
			DrawText(ImVec2(left, CentredTop(box)), ink, glyph);
		}

		void DrawSectionRow(const SidebarRow& row, const RowBox& box, bool hovered, const SidebarColours& colours)
		{
			const ImU32 dim = colours.Of(SidebarTone::Dim);
			const ImU32 title = hovered ? ImGui::GetColorU32(ImGuiCol_Text) : dim;
			float left = box.Left;

			DrawChevronAt(left, box, row.Open, dim);
			left += Em(kChevronSize) + Em(kGap);

			{
				const TextAtSize small(Em(kSmallText));
				const float top = CentredTop(box);

				const std::string heading = Uppercase(row.Label);
				DrawText(ImVec2(left, top), title, heading);
				left += WidthOf(heading) + Em(kGap);

				const std::string count = CountText(row);
				DrawText(ImVec2(left, top), dim, count);
				left += WidthOf(count) + Em(kGap);

				if (!row.Note.empty())
				{
					DrawText(ImVec2(left, top), dim, row.Note);
					left += WidthOf(row.Note) + Em(kGap);
				}
			}

			if (left < box.Right)
			{
				const float middle = std::floor(box.Top + box.Height * kHalf);
				ImGui::GetWindowDrawList()->AddLine(ImVec2(left, middle), ImVec2(box.Right, middle), colours.Rule,
													kRuleThickness);
			}
		}

		void DrawGroupRow(const SidebarRow& row, const RowBox& box, const SidebarColours& colours)
		{
			const ImU32 dim = colours.Of(SidebarTone::Dim);
			float left = IndentedLeft(box, row);

			DrawChevronAt(left, box, row.Open, dim);
			left += Em(kChevronSize) + Em(kGap);

			DrawIconAt(left, box, row.Open ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER, dim);
			left += Em(kIconSize) + Em(kGap);

			float labelRight = box.Right;

			{
				const TextAtSize small(Em(kCountText));
				const std::string count = CountText(row);
				const float countLeft = box.Right - WidthOf(count);
				DrawText(ImVec2(countLeft, CentredTop(box)), dim, count);
				labelRight = countLeft - Em(kGap);
			}

			DrawText(ImVec2(left, ListRowTextTop(box.Top)), ImGui::GetColorU32(ImGuiCol_Text),
					 FitToWidth(row.Label, labelRight - left));
		}

		void DrawLinkRow(const SidebarRow& row, const RowBox& box, bool hovered, const SidebarColours& colours)
		{
			const float left = IndentedLeft(box, row) + Em(kLeadSlot) + Em(kGap);
			const float top = ListRowTextTop(box.Top);
			const std::string label = FitToWidth(row.Label, box.Right - left);

			DrawText(ImVec2(left, top), colours.Link, label);

			if (hovered)
			{
				const float underline = std::floor(top + ImGui::GetFontSize());
				ImGui::GetWindowDrawList()->AddLine(ImVec2(left, underline), ImVec2(left + WidthOf(label), underline),
													colours.Link, kUnderlineThickness);
			}
		}

		void DrawStashRow(const SidebarRow& row, const RowBox& box, const SidebarRowText& text, bool selected,
						  const SidebarColours& colours, const std::vector<std::string>& searchWords)
		{
			const ImU32 dim = colours.Of(SidebarTone::Dim);
			float left = IndentedLeft(box, row);

			DrawIconAt(left, box, ICON_LC_ARCHIVE, dim);
			left += Em(kLeadSlot) + Em(kGap);

			float textRight = box.Right;

			if (!text.Right.empty())
			{
				const TextAtSize small(Em(kCountText));
				const float ageLeft = box.Right - WidthOf(text.Right);
				DrawText(ImVec2(ageLeft, CentredTop(box)), dim, text.Right);
				textRight = ageLeft - Em(kGap);
			}

			DrawHighlightedText(ImVec2(left, ListRowTextTop(box.Top)), ListRowNameColour(selected), colours.Link,
								FitToWidth(text.Head, textRight - left), searchWords);
		}

		// The spans of line 2 joined by a dim separator; the piece that would pass `right` is cut with an ellipsis.
		void DrawMeta(const std::vector<SidebarSpan>& meta, float left, float top, float right,
					  const SidebarColours& colours)
		{
			float x = left;
			bool first = true;

			const auto place = [&](std::string_view text, ImU32 ink) {
				const float width = WidthOf(text);

				if (x + width <= right)
				{
					DrawText(ImVec2(x, top), ink, text);
					x += width;
					return true;
				}

				DrawText(ImVec2(x, top), ink, FitToWidth(text, right - x));
				return false;
			};

			for (const SidebarSpan& span : meta)
			{
				if (!first && !place(kSeparator, colours.Of(SidebarTone::Dim)))
				{
					return;
				}

				if (!place(span.Text, colours.Of(span.Tone)))
				{
					return;
				}

				first = false;
			}
		}

		// One dim line of a list row, its text where a one-line row's text is, at `left` and `rowTop` from the cursor's
		// origin. Only a text that waits has a loader beside it: for a failure or an empty list nothing is on its way, and
		// a loader would have every frame drawn for good.
		void DrawNotice(const std::string& text, bool waiting, float left, float rowTop)
		{
			ImGui::SetCursorPos(ImVec2(left, ListRowTextTop(rowTop)));

			if (waiting)
			{
				Core::LoadingLine(text.c_str(), Core::WhileLoaderWaits::ShowText);
				return;
			}

			bool cut = false;
			ImGui::TextDisabled("%s", FitToWidth(text, ImGui::GetContentRegionAvail().x - Em(kRowSide), &cut).c_str());

			if (cut && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s", text.c_str());
			}
		}

		void DrawWaitingRow(const SidebarWaitingRow& waiting, ImVec2 contentStart)
		{
			ImGui::PushID("##SidebarWaiting");
			DrawNotice(waiting.Text, !waiting.Failed, contentStart.x + Em(kRowSide), contentStart.y);
			ImGui::PopID();
		}

		// A notice is no item to click: its text stands where the section's other rows have theirs.
		void DrawNoticeRow(const SidebarRow& row, ImVec2 contentStart, float rowTop)
		{
			const float left = contentStart.x + Em(kRowSide) + static_cast<float>(row.Depth) * Em(kIndent) + Em(kLeadSlot) +
							   Em(kGap);
			DrawNotice(row.Label, row.Waiting, left, contentStart.y + rowTop);
		}

		void DrawHeadDot(ImVec2 centre, const SidebarColours& colours)
		{
			ImDrawList* const drawing = ImGui::GetWindowDrawList();

			drawing->AddCircleFilled(centre, Em(kHeadGlow) * kHalf, WithAlphaShare(colours.Accent, kHeadGlowAlpha));
			drawing->AddCircleFilled(centre, Em(kHeadDot) * kHalf, colours.Accent);
		}

		void DrawRefRow(const SidebarRow& row, const RowBox& box, const SidebarRowText& text, bool selected,
						const SidebarColours& colours, const std::vector<std::string>& searchWords)
		{
			const float left = IndentedLeft(box, row);
			const float textLeft = left + Em(kRefLeadSlot) + Em(kTwoLineGap);
			float textRight = box.Right;

			if (text.IsHead)
			{
				const float slotMiddle = left + Em(kRefLeadSlot) * kHalf;
				DrawHeadDot(ImVec2(slotMiddle, box.Top + box.Height * kHalf), colours);
			}

			if (!text.Right.empty())
			{
				const TextAtSize small(Em(kSmallText));
				const float rightLeft = box.Right - WidthOf(text.Right);
				DrawText(ImVec2(rightLeft, CentredTop(box)), colours.Right, text.Right);
				textRight = rightLeft - Em(kTwoLineGap);
			}

			const float firstLineHeight = ImGui::GetFontSize() * kLineSpacing;
			const float smallSize = Em(kSmallText);
			const float secondLineHeight = smallSize * kLineSpacing;
			const float blockHeight = firstLineHeight + Em(kLinesApart) + secondLineHeight;
			const float blockTop = box.Top + std::floor((box.Height - blockHeight) * kHalf);

			const ImU32 headInk = text.IsHead ? Core::EmphasisText() : ListRowNameColour(selected);
			DrawHighlightedText(ImVec2(textLeft, blockTop + std::floor((firstLineHeight - ImGui::GetFontSize()) * kHalf)),
								headInk, colours.Link, FitToWidth(text.Head, textRight - textLeft), searchWords);

			const TextAtSize small(smallSize);
			const float secondLineTop =
				blockTop + firstLineHeight + Em(kLinesApart) + std::floor((secondLineHeight - smallSize) * kHalf);
			DrawMeta(text.Meta, textLeft, secondLineTop, textRight, colours);
		}
	}

	SidebarColours ReadSidebarColours(const ResolvedScheme& scheme)
	{
		const EditorPalette editor = ReadEditorPalette(scheme);
		const GitPalette git = ReadGitPalette(scheme);
		const Core::Color& accent = ColourFromScheme(scheme, ChromeColor::Accent);
		const ImU32 lighterAccent = Lightened(accent, ColourFromScheme(scheme, ChromeColor::Text));

		SidebarColours colours;
		colours.Tones[static_cast<std::size_t>(SidebarTone::Dim)] =
			PackedColour(ColourFromScheme(scheme, ChromeColor::TextDisabled));
		colours.Tones[static_cast<std::size_t>(SidebarTone::Local)] = editor.DiagnosticWarning;
		colours.Tones[static_cast<std::size_t>(SidebarTone::Both)] = lighterAccent;
		colours.Tones[static_cast<std::size_t>(SidebarTone::Gone)] = editor.DiagnosticError;
		colours.Tones[static_cast<std::size_t>(SidebarTone::Unusual)] = editor.DiagnosticWarning;
		colours.Tones[static_cast<std::size_t>(SidebarTone::Merged)] = git.GutterAdded;
		colours.Accent = PackedColour(accent);
		colours.Link = lighterAccent;
		colours.Right = lighterAccent;
		colours.Rule = PackedColour(ColourFromScheme(scheme, ChromeColor::Border));

		return colours;
	}

	float SidebarTwoLineRowHeight()
	{
		return std::floor(Em(kTwoLineHeight));
	}

	float SidebarColumnWidth()
	{
		return std::floor(Em(kColumnWidth));
	}

	void SidebarView::Draw(const std::vector<SidebarRow>* rows,
						   const std::function<SidebarRowText(const SidebarRow&)>& textOf,
						   const std::optional<SidebarWaitingRow>& waiting, const SidebarSelection& selected,
						   const SidebarColours& colours)
	{
		{
			const ImVec2 start = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(start.x + Em(kFieldSide), start.y + Em(kFieldTop)));

			const float verticalPadding = std::floor((Em(kFieldHeight) - ImGui::GetFontSize()) * kHalf);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, verticalPadding));
			FilterFieldLook look;
			look.Hint = kSearchHint;
			look.Width = ImGui::GetContentRegionAvail().x - Em(kFieldSide);
			DrawFilterField("##SidebarSearch", m_Search, look);
			ImGui::PopStyleVar();

			ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, Em(kFieldBottom) - ImGui::GetStyle().ItemSpacing.y)));
		}

		std::optional<SidebarRow> clicked;

		if (ImGui::BeginChild("##SidebarRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			const ListBodyScope body;

			if (m_ScrollToTop)
			{
				ImGui::SetScrollY(0.0f);
				m_ScrollToTop = false;
			}

			// WARNING: Row positions are relative to where the child's content starts, which a window padding moves. Read
			// as the cursor before any row, not as GetCursorStartPos: that one has the scroll taken off already, and
			// SetCursorPos takes it off again.
			const ImVec2 contentStart = ImGui::GetCursorPos();
			float listTop = 0.0f;

			if (waiting.has_value())
			{
				DrawWaitingRow(*waiting, contentStart);
				listTop = ListRowPitch();
			}

			if (rows != nullptr)
			{
				const std::vector<std::string> searchWords = LoweredWordsOf(m_Search);
				m_RowTops.resize(rows->size() + 1);
				float top = listTop;

				for (std::size_t index = 0; index < rows->size(); ++index)
				{
					m_RowTops[index] = top;
					top += HeightOf((*rows)[index]);
				}

				m_RowTops[rows->size()] = top;

				const float scrolled = ImGui::GetScrollY();
				const float visibleBottom = scrolled + ImGui::GetWindowHeight();
				const auto firstAfter = std::upper_bound(m_RowTops.begin(), m_RowTops.begin() + rows->size(), scrolled);
				std::size_t index =
					firstAfter == m_RowTops.begin() ? 0 : static_cast<std::size_t>(firstAfter - m_RowTops.begin()) - 1;

				for (; index < rows->size() && m_RowTops[index] < visibleBottom; ++index)
				{
					const SidebarRow& row = (*rows)[index];

					if (row.Kind == SidebarRowKind::Notice)
					{
						const int pushed = PushRowId(row);
						DrawNoticeRow(row, contentStart, m_RowTops[index]);
						PopRowId(pushed);
						continue;
					}

					const bool isSelected = IsSelected(row, selected);
					const float height = HeightOf(row);

					const int pushed = PushRowId(row);
					ImGui::SetCursorPos(ImVec2(contentStart.x, contentStart.y + m_RowTops[index]));

					if (ListRowSelectableOfHeight("##row", isSelected, height))
					{
						clicked = row;
					}

					const bool hovered = ImGui::IsItemHovered();
					const RowBox box{.Left = ListLeft() + Em(kRowSide),
									 .Right = ListRight() - Em(kRowSide),
									 .Top = ImGui::GetItemRectMin().y,
									 .Height = height};

					if (OpensMenu(row) && DrawRowMenu)
					{
						const ListPopupScope popup;

						if (Core::BeginRightClickPopup("##SidebarRowMenu"))
						{
							DrawRowMenu(row);
							ImGui::EndPopup();
						}
					}

					const SidebarRowText text = HasText(row) ? textOf(row) : SidebarRowText{};

					if (hovered && !text.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					{
						ImGui::SetTooltip("%s", text.Tooltip.c_str());
					}

					switch (row.Kind)
					{
					case SidebarRowKind::Section:
						DrawSectionRow(row, box, hovered, colours);
						break;
					case SidebarRowKind::Group:
						DrawGroupRow(row, box, colours);
						break;
					case SidebarRowKind::Link:
						DrawLinkRow(row, box, hovered, colours);
						break;
					case SidebarRowKind::Stash:
						DrawStashRow(row, box, text, isSelected, colours, searchWords);
						break;
					case SidebarRowKind::Branch:
					case SidebarRowKind::Base:
						DrawRefRow(row, box, text, isSelected, colours, searchWords);
						break;
					case SidebarRowKind::Notice:
						break;
					}

					PopRowId(pushed);
				}

				// WARNING: One item as tall as the whole list ends it at the last row's bottom. The last row drawn holds a line
				// for pieces laid over it, so an empty item placed after that row would take the line too and end the list
				// that much lower.
				ImGui::SetCursorPos(contentStart);
				ImGui::Dummy(ImVec2(0.0f, top));
			}
		}

		ImGui::EndChild();

		// WARNING: After the rows are drawn, so a callback that rebuilds them does it between frames, not under the loop.
		if (clicked.has_value() && OnClick)
		{
			OnClick(*clicked);
		}
	}
}
