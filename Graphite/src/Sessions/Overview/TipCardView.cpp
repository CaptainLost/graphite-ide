#include "Sessions/Overview/TipCardView.h"

#include "Sessions/Overview/DashboardCanvas.h"

#include "Core/ImGui/ImGuiColors.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {
		// WARNING: Lengths are in font sizes, from the mockup's tooltip.
		constexpr float kMaxWidth = 28.0f;
		constexpr float kLabelMaxWidth = 19.0f;
		constexpr float kColumnGap = 0.8f;
		constexpr float kHeadGap = 1.0f;
		constexpr float kTipLinePitch = 1.35f;
		constexpr float kNoteSize = 0.92f;
		constexpr float kTitleSize = 0.8f;
		constexpr float kSectionMargin = 0.45f;
		constexpr float kSectionPadding = 0.4f;
		constexpr float kSwatchSize = 0.6f;
		constexpr float kSwatchGap = 0.4f;
		constexpr float kSwatchRounding = 0.15f;
		constexpr float kBannerPaddingX = 0.45f;
		constexpr float kBannerPaddingY = 0.15f;
		constexpr float kBannerGap = 0.4f;
		constexpr float kBannerRounding = 0.3f;
		constexpr float kNoteGap = 0.2f;
		constexpr float kHalf = 0.5f;
		constexpr std::size_t kBodyLines = 4;

		// WARNING: From the mockup's .tip: padding 8 px by 10 px, radius 6 px, shadow 0 6 px 24 px, at 13 px a font size.
		constexpr float kPaddingX = 0.77f;
		constexpr float kPaddingY = 0.62f;
		constexpr float kRounding = 0.46f;
		constexpr float kBorder = 1.0f;
		constexpr float kShadowDrop = 0.46f;
		constexpr float kShadowBlur = 1.85f;
		constexpr int kShadowLayers = 10;

		// WARNING: The card is as wide as its widest text, so that text's room equals its width and a rounding error
		// in working the room out would cut it; the slack, in pixels, absorbs that.
		constexpr float kFitSlack = 0.5f;

		std::string FitInCard(std::string_view text, float room, float size, TextFace face = TextFace::Regular)
		{
			return FitAt(text, room + kFitSlack, size, face);
		}

		struct Columns
		{
			float Label = 0.0f;
			float Value = 0.0f;
			float Extra = 0.0f;

			float Width(float gap) const
			{
				return Label + (Value > 0.0f ? gap + Value : 0.0f) + (Extra > 0.0f ? gap + Extra : 0.0f);
			}
		};

		float RowSwatchRoom(const TipRow& row, float unit)
		{
			return row.Swatch ? (kSwatchSize + kSwatchGap) * unit : 0.0f;
		}

		TextFace LabelFace(const TipRow& row)
		{
			return row.CodeLabel ? TextFace::Code : TextFace::Regular;
		}

		Columns ColumnsOf(const TipSection& section, float unit)
		{
			Columns columns;

			for (const TipRow& row : section.Rows)
			{
				columns.Label = std::max(columns.Label, RowSwatchRoom(row, unit) + TextWidthAt(row.Label, unit, LabelFace(row)));
				columns.Value = std::max(columns.Value, TextWidthAt(row.Value, unit));
				columns.Extra = std::max(columns.Extra, TextWidthAt(row.Extra, unit));
			}

			columns.Label = std::min(columns.Label, kLabelMaxWidth * unit);
			return columns;
		}

		float NaturalWidth(const TipCard& card, float unit)
		{
			float width = TextWidthAt(card.Head, unit, TextFace::Strong);

			if (!card.HeadValue.empty())
			{
				width += kHeadGap * unit + TextWidthAt(card.HeadValue, unit, TextFace::Strong);
			}

			if (!card.Banner.empty())
			{
				width = std::max(width, TextWidthAt(card.Banner, unit) + 2.0f * kBannerPaddingX * unit);
			}

			width = std::max(width, TextWidthAt(card.Body, unit));

			for (const std::string& note : card.Notes)
			{
				width = std::max(width, RunsWidthAt(note, kNoteSize * unit));
			}

			for (const TipSection& section : card.Sections)
			{
				width = std::max(width, ColumnsOf(section, unit).Width(kColumnGap * unit));
				width = std::max(width, TextWidthAt(UpperCase(section.Title), kTitleSize * unit));

				for (const std::string& note : section.Notes)
				{
					width = std::max(width, RunsWidthAt(note, kNoteSize * unit));
				}
			}

			return std::min(width, kMaxWidth * unit);
		}

		ImU32 ToneColour(TipTone tone, const DashboardColours& colours)
		{
			switch (tone)
			{
			case TipTone::Normal:
				return colours.Ink;
			case TipTone::Bad:
				return colours.Bad;
			case TipTone::Dim:
				break;
			}

			return colours.Dim;
		}

		class CardPainter
		{
		public:
			CardPainter(const DashboardColours& colours, float unit, float width)
				: m_Colours(colours), m_Unit(unit), m_Width(width), m_Drawing(*ImGui::GetWindowDrawList()),
				  m_Origin(ImGui::GetCursorScreenPos()), m_Y(m_Origin.y)
			{
			}

			void Banner(const std::string& text)
			{
				const float height = (kTipLinePitch + 2.0f * kBannerPaddingY) * m_Unit;
				m_Drawing.AddRectFilled(ImVec2(m_Origin.x, m_Y), ImVec2(m_Origin.x + m_Width, m_Y + height), m_Colours.Frame,
										kBannerRounding * m_Unit);
				const float room = m_Width - 2.0f * kBannerPaddingX * m_Unit;
				DrawTextAt(m_Drawing, ImVec2(m_Origin.x + kBannerPaddingX * m_Unit, m_Y + (height - m_Unit) * kHalf), m_Unit,
						   m_Colours.Ink, FitInCard(text, room, m_Unit));
				m_Y += height + kBannerGap * m_Unit;
			}

			void Head(const std::string& head, const std::string& value)
			{
				const float valueWidth = TextWidthAt(value, m_Unit, TextFace::Strong);
				const float headRoom = m_Width - (value.empty() ? 0.0f : valueWidth + kHeadGap * m_Unit);
				const float textTop = LineTextTop(m_Unit);
				DrawTextAt(m_Drawing, ImVec2(m_Origin.x, textTop), m_Unit, m_Colours.Ink,
						   FitInCard(head, headRoom, m_Unit, TextFace::Strong), TextFace::Strong);

				if (!value.empty())
				{
					DrawTextAt(m_Drawing, ImVec2(m_Origin.x + m_Width - valueWidth, textTop), m_Unit, m_Colours.Ink, value,
							   TextFace::Strong);
				}

				m_Y += kTipLinePitch * m_Unit;
			}

			void Note(const std::string& text)
			{
				const float size = kNoteSize * m_Unit;
				DrawWrappedRunsAt(m_Drawing, ImVec2(m_Origin.x, m_Y + kNoteGap * m_Unit), size, m_Colours.Dim, m_Colours.Dim,
								  text, m_Width);
				m_Y += WrappedRunsHeightAt(text, size, m_Width) + kNoteGap * m_Unit;
			}

			// WARNING: At most four lines; the fourth ends in an ellipsis when the text goes on, so a long first prompt
			// cannot make the card taller than the screen.
			void Body(const std::string& text)
			{
				ImFont* const font = FaceOf(TextFace::Regular);
				const char* start = text.data();
				const char* const end = text.data() + text.size();
				m_Y += kNoteGap * m_Unit;

				for (std::size_t line = 0; line < kBodyLines && start < end; ++line)
				{
					const char* wrap = font->CalcWordWrapPosition(m_Unit, start, end, m_Width);
					wrap = wrap > start ? wrap : start + 1;
					const bool last = line + 1 == kBodyLines && ImTextCalcWordWrapNextLineStart(wrap, end) < end;
					const std::string_view shown =
						last ? std::string_view{start, static_cast<std::size_t>(end - start)}
							 : std::string_view{start, static_cast<std::size_t>(wrap - start)};
					DrawTextAt(m_Drawing, ImVec2(m_Origin.x, m_Y), m_Unit, m_Colours.Ink,
							   last ? FitInCard(shown, m_Width, m_Unit) : std::string{shown});
					m_Y += m_Unit;
					start = ImTextCalcWordWrapNextLineStart(wrap, end);
				}
			}

			void Rule()
			{
				if (m_Y <= m_Origin.y)
				{
					return;
				}

				m_Y += kSectionMargin * m_Unit;
				m_Drawing.AddLine(ImVec2(m_Origin.x, m_Y), ImVec2(m_Origin.x + m_Width, m_Y), m_Colours.Frame);
				m_Y += kSectionPadding * m_Unit;
			}

			void Title(const std::string& title)
			{
				const float size = kTitleSize * m_Unit;
				DrawTextAt(m_Drawing, ImVec2(m_Origin.x, LineTextTop(size)), size, m_Colours.Dim,
						   FitInCard(UpperCase(title), m_Width, size));
				m_Y += kTipLinePitch * size;
			}

			void Rows(const TipSection& section)
			{
				const float gap = kColumnGap * m_Unit;
				Columns columns = ColumnsOf(section, m_Unit);
				columns.Label = std::max(0.0f, columns.Label - std::max(0.0f, columns.Width(gap) - m_Width));

				for (const TipRow& row : section.Rows)
				{
					Row(row, columns, gap);
				}
			}

			void Finish() const { ImGui::Dummy(ImVec2(m_Width, m_Y - m_Origin.y)); }

		private:
			float LineTextTop(float size) const { return m_Y + (kTipLinePitch * size - size) * kHalf; }

			void Row(const TipRow& row, const Columns& columns, float gap)
			{
				const float textTop = LineTextTop(m_Unit);
				float labelLeft = m_Origin.x;

				if (row.Swatch)
				{
					const float swatch = kSwatchSize * m_Unit;
					const ImVec2 min(m_Origin.x, m_Y + (kTipLinePitch * m_Unit - swatch) * kHalf);
					m_Drawing.AddRectFilled(min, ImVec2(min.x + swatch, min.y + swatch),
											m_Colours.Families[static_cast<std::size_t>(*row.Swatch)], kSwatchRounding * m_Unit);
					labelLeft += RowSwatchRoom(row, m_Unit);
				}

				const float labelRoom = std::max(0.0f, columns.Label - RowSwatchRoom(row, m_Unit));
				DrawTextAt(m_Drawing, ImVec2(labelLeft, textTop), m_Unit, m_Colours.Dim,
						   FitInCard(row.Label, labelRoom, m_Unit, LabelFace(row)), LabelFace(row));

				const float valueRight = m_Origin.x + columns.Label + gap + columns.Value;
				DrawTextAt(m_Drawing, ImVec2(valueRight - TextWidthAt(row.Value, m_Unit), textTop), m_Unit, m_Colours.Ink,
						   row.Value);

				if (!row.Extra.empty())
				{
					const float extraRight = valueRight + gap + columns.Extra;
					DrawTextAt(m_Drawing, ImVec2(extraRight - TextWidthAt(row.Extra, m_Unit), textTop), m_Unit,
							   ToneColour(row.ExtraTone, m_Colours), row.Extra);
				}

				m_Y += kTipLinePitch * m_Unit;
			}

			const DashboardColours& m_Colours;
			float m_Unit = 0.0f;
			float m_Width = 0.0f;
			ImDrawList& m_Drawing;
			ImVec2 m_Origin;
			float m_Y = 0.0f;
		};
	}

	bool BeginTipCard(const DashboardColours& colours)
	{
		constexpr int kColours = 2;
		constexpr int kVars = 4;
		const float unit = ImGui::GetFontSize();
		ImGui::PushStyleColor(ImGuiCol_Border, colours.Frame);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, colours.TipSurface);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPaddingX * unit, kPaddingY * unit));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding * unit);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kRounding * unit);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, kBorder);
		const bool open = ImGui::BeginTooltip();
		ImGui::PopStyleVar(kVars);
		ImGui::PopStyleColor(kColours);
		return open;
	}

	void EndTipCard(const DashboardColours& colours)
	{
		const ImGuiWindow* const card = ImGui::GetCurrentWindowRead();
		const ImRect edge = card->Rect();
		ImDrawList& drawing = *ImGui::GetForegroundDrawList(card->Viewport);
		const ImVec2 viewMin = card->Viewport->Pos;
		const ImVec2 viewMax(viewMin.x + card->Viewport->Size.x, viewMin.y + card->Viewport->Size.y);
		const float unit = ImGui::GetFontSize();
		const float drop = kShadowDrop * unit;
		const float blur = kShadowBlur * unit;
		const float wholeAlpha = ImGui::ColorConvertU32ToFloat4(colours.Shadow).w;
		const float layerAlpha = 1.0f - std::pow(1.0f - wholeAlpha, 1.0f / static_cast<float>(kShadowLayers));
		const ImU32 layer = Core::WithAlphaScaled(colours.Shadow, wholeAlpha > 0.0f ? layerAlpha / wholeAlpha : 0.0f);

		// WARNING: Four bands around the card, so no layer darkens the card itself.
		const std::array<ImRect, 4> bands = {ImRect(viewMin, ImVec2(viewMax.x, edge.Min.y)),
											 ImRect(ImVec2(viewMin.x, edge.Max.y), viewMax),
											 ImRect(ImVec2(viewMin.x, edge.Min.y), ImVec2(edge.Min.x, edge.Max.y)),
											 ImRect(ImVec2(edge.Max.x, edge.Min.y), ImVec2(viewMax.x, edge.Max.y))};

		for (const ImRect& band : bands)
		{
			drawing.PushClipRect(band.Min, band.Max);

			for (int index = 1; index <= kShadowLayers; ++index)
			{
				const float spread = blur * static_cast<float>(index) / static_cast<float>(kShadowLayers);
				drawing.AddRectFilled(ImVec2(edge.Min.x - spread, edge.Min.y - spread + drop),
									  ImVec2(edge.Max.x + spread, edge.Max.y + spread + drop), layer,
									  card->WindowRounding + spread);
			}

			drawing.PopClipRect();
		}

		ImGui::EndTooltip();
	}

	void DrawTipCard(const TipCard& card, const DashboardColours& colours)
	{
		const float unit = ImGui::GetFontSize();
		CardPainter painter(colours, unit, NaturalWidth(card, unit));

		if (!card.Banner.empty())
		{
			painter.Banner(card.Banner);
		}

		if (!card.Head.empty() || !card.HeadValue.empty())
		{
			painter.Head(card.Head, card.HeadValue);
		}

		if (!card.Body.empty())
		{
			painter.Body(card.Body);
		}

		for (const std::string& note : card.Notes)
		{
			painter.Note(note);
		}

		for (const TipSection& section : card.Sections)
		{
			painter.Rule();

			if (!section.Title.empty())
			{
				painter.Title(section.Title);
			}

			painter.Rows(section);

			for (const std::string& note : section.Notes)
			{
				painter.Note(note);
			}
		}

		painter.Finish();
	}
}
