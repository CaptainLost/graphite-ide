#include "Sessions/Overview/TipCardView.h"

#include "Sessions/Overview/DashboardCanvas.h"

#include <imgui.h>

#include <algorithm>
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

		// WARNING: The card is as wide as its widest text, so that text's room equals its width and a rounding error
		// in working the room out would cut it; the slack, in pixels, absorbs that.
		constexpr float kFitSlack = 0.5f;

		std::string FitInCard(std::string_view text, float room, float size, TextWeight weight = TextWeight::Regular)
		{
			return FitAt(text, room + kFitSlack, size, weight);
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

		Columns ColumnsOf(const TipSection& section, float unit)
		{
			Columns columns;

			for (const TipRow& row : section.Rows)
			{
				columns.Label = std::max(columns.Label, RowSwatchRoom(row, unit) + TextWidthAt(row.Label, unit));
				columns.Value = std::max(columns.Value, TextWidthAt(row.Value, unit));
				columns.Extra = std::max(columns.Extra, TextWidthAt(row.Extra, unit));
			}

			columns.Label = std::min(columns.Label, kLabelMaxWidth * unit);
			return columns;
		}

		float NaturalWidth(const TipCard& card, float unit)
		{
			float width = TextWidthAt(card.Head, unit, TextWeight::Strong);

			if (!card.HeadValue.empty())
			{
				width += kHeadGap * unit + TextWidthAt(card.HeadValue, unit, TextWeight::Strong);
			}

			if (!card.Banner.empty())
			{
				width = std::max(width, TextWidthAt(card.Banner, unit) + 2.0f * kBannerPaddingX * unit);
			}

			width = std::max(width, TextWidthAt(card.Body, unit));

			for (const std::string& note : card.Notes)
			{
				width = std::max(width, TextWidthAt(note, kNoteSize * unit));
			}

			for (const TipSection& section : card.Sections)
			{
				width = std::max(width, ColumnsOf(section, unit).Width(kColumnGap * unit));
				width = std::max(width, TextWidthAt(UpperCase(section.Title), kTitleSize * unit));

				for (const std::string& note : section.Notes)
				{
					width = std::max(width, TextWidthAt(note, kNoteSize * unit));
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
				const float valueWidth = TextWidthAt(value, m_Unit, TextWeight::Strong);
				const float headRoom = m_Width - (value.empty() ? 0.0f : valueWidth + kHeadGap * m_Unit);
				const float textTop = LineTextTop(m_Unit);
				DrawTextAt(m_Drawing, ImVec2(m_Origin.x, textTop), m_Unit, m_Colours.Ink,
						   FitInCard(head, headRoom, m_Unit, TextWeight::Strong), TextWeight::Strong);

				if (!value.empty())
				{
					DrawTextAt(m_Drawing, ImVec2(m_Origin.x + m_Width - valueWidth, textTop), m_Unit, m_Colours.Ink, value,
							   TextWeight::Strong);
				}

				m_Y += kTipLinePitch * m_Unit;
			}

			void Wrapped(const std::string& text, float size, ImU32 colour)
			{
				DrawWrappedAt(m_Drawing, ImVec2(m_Origin.x, m_Y + kNoteGap * m_Unit), size, colour, text, m_Width);
				m_Y += WrappedHeightAt(text, size, m_Width) + kNoteGap * m_Unit;
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
				DrawTextAt(m_Drawing, ImVec2(labelLeft, textTop), m_Unit, m_Colours.Dim, FitInCard(row.Label, labelRoom, m_Unit));

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
		ImGui::PushStyleColor(ImGuiCol_Border, colours.Frame);
		const bool open = ImGui::BeginTooltip();
		ImGui::PopStyleColor();
		return open;
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
			painter.Wrapped(card.Body, unit, colours.Ink);
		}

		for (const std::string& note : card.Notes)
		{
			painter.Wrapped(note, kNoteSize * unit, colours.Dim);
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
				painter.Wrapped(note, kNoteSize * unit, colours.Dim);
			}
		}

		painter.Finish();
	}
}
