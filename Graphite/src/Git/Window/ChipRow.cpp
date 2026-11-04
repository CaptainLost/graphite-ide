#include "Git/Window/ChipRow.h"

#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"

#include "Core/ImGui/FontSet.h"

#include <imgui_internal.h>

#include <string>

namespace Graphite {

	namespace {

		constexpr float kChipPaddingX = 7.0f;
		constexpr float kChipPaddingY = 2.0f;

		// The chips' text is a step smaller than the interface's, as their labels are short and many.
		constexpr float kChipFontScale = 0.875f;

		// How much of a tone colours the fill under the chip and how far its text is drawn toward the scheme's text.
		constexpr float kToneFillShare = 0.2f;
		constexpr float kToneTextShare = 0.4f;
		constexpr float kHalf = 0.5f;

		ChipInk ToneOf(ImU32 colour)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const ImVec4 tone = ImGui::ColorConvertU32ToFloat4(colour);

			return ChipInk{.Fill = ImGui::GetColorU32(ImLerp(style.Colors[ImGuiCol_WindowBg], tone, kToneFillShare)),
						   .Text = ImGui::GetColorU32(ImLerp(tone, style.Colors[ImGuiCol_Text], kToneTextShare))};
		}

		float ChipWidth(const std::string& text)
		{
			return ImGui::CalcTextSize(text.c_str()).x + 2.0f * kChipPaddingX;
		}

		std::string PlusText(std::size_t hidden)
		{
			return "+" + std::to_string(hidden);
		}

		// Lines the chips take, with an optional last one of `extraWidth`.
		int LinesFor(const std::vector<float>& widths, std::size_t count, float extraWidth, float room, float spacing)
		{
			int lines = 1;
			float lineWidth = 0.0f;
			bool first = true;

			const auto place = [&](float width) {
				if (!first && lineWidth + spacing + width > room)
				{
					++lines;
					lineWidth = width;
				}
				else
				{
					lineWidth += (first ? 0.0f : spacing) + width;
				}

				first = false;
			};

			for (std::size_t index = 0; index < count; ++index)
			{
				place(widths[index]);
			}

			if (extraWidth > 0.0f)
			{
				place(extraWidth);
			}

			return lines;
		}

		void DrawChip(const std::string& text, const ChipInk& ink, float width, float height)
		{
			ImGui::Dummy(ImVec2(width, height));

			const ImVec2 min = ImGui::GetItemRectMin();
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(min, ImGui::GetItemRectMax(), ink.Fill, height * kHalf);
			drawList->AddText(ImVec2(min.x + kChipPaddingX, min.y + (height - ImGui::GetFontSize()) * kHalf), ink.Text,
							  text.c_str());
		}
	}

	ChipColours ReadChipColours(const ResolvedScheme& scheme)
	{
		ChipColours colours;
		const ImGuiStyle& style = ImGui::GetStyle();

		colours.Tones[static_cast<std::size_t>(ChipTone::Plain)] =
			ChipInk{.Fill = ImGui::GetColorU32(ImGuiCol_FrameBg), .Text = ImGui::GetColorU32(ImGuiCol_Text)};
		colours.Tones[static_cast<std::size_t>(ChipTone::Good)] = ToneOf(ReadGitPalette(scheme).GutterAdded);
		colours.Tones[static_cast<std::size_t>(ChipTone::Warning)] = ToneOf(ReadEditorPalette(scheme).DiagnosticWarning);
		colours.Tones[static_cast<std::size_t>(ChipTone::Info)] =
			ToneOf(ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_CheckMark]));

		return colours;
	}

	float ChipHeight()
	{
		return ImGui::GetFontSize() * kChipFontScale + 2.0f * kChipPaddingY;
	}

	float ChipWidthOf(const std::string& text)
	{
		Core::PushInterfaceFont(ImGui::GetFontSize() * kChipFontScale);
		const float width = ChipWidth(text);
		ImGui::PopFont();

		return width;
	}

	std::size_t ChipsShown(const std::vector<float>& widths, float room, float spacing, int maxLines,
						   const std::function<float(std::size_t hidden)>& plusWidth)
	{
		const std::size_t count = widths.size();
		constexpr float kNoExtra = 0.0f;

		if (maxLines <= 0 || LinesFor(widths, count, kNoExtra, room, spacing) <= maxLines)
		{
			return count;
		}

		for (std::size_t shown = count - 1; shown > 0; --shown)
		{
			if (LinesFor(widths, shown, plusWidth(count - shown), room, spacing) <= maxLines)
			{
				return shown;
			}
		}

		return 0;
	}

	void DrawChipRow(const std::vector<Chip>& chips, const ChipColours& colours, int maxLines)
	{
		const float height = ChipHeight();

		if (chips.empty())
		{
			ImGui::Dummy(ImVec2(0.0f, height));
			return;
		}

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float room = ImGui::GetContentRegionAvail().x;
		const float right = ImGui::GetCursorScreenPos().x + room;

		Core::PushInterfaceFont(ImGui::GetFontSize() * kChipFontScale);

		std::vector<float> widths;
		widths.reserve(chips.size());

		for (const Chip& chip : chips)
		{
			widths.push_back(ChipWidth(chip.Text));
		}

		const std::size_t shown = ChipsShown(widths, room, spacing, maxLines,
											 [](std::size_t hidden) { return ChipWidth(PlusText(hidden)); });
		bool first = true;

		const auto place = [&](float width) {
			if (!first && ImGui::GetItemRectMax().x + spacing + width <= right)
			{
				ImGui::SameLine();
			}

			first = false;
		};

		for (std::size_t index = 0; index < shown; ++index)
		{
			place(widths[index]);
			DrawChip(chips[index].Text, colours.Of(chips[index].Tone), widths[index], height);
		}

		std::string hiddenNames;

		if (shown < chips.size())
		{
			const std::string plus = PlusText(chips.size() - shown);
			const float width = ChipWidth(plus);

			place(width);
			DrawChip(plus, colours.Of(ChipTone::Plain), width, height);

			for (std::size_t index = shown; index < chips.size(); ++index)
			{
				hiddenNames += (hiddenNames.empty() ? "" : "\n") + chips[index].Text;
			}
		}

		ImGui::PopFont();

		if (!hiddenNames.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip("%s", hiddenNames.c_str());
		}
	}
}
