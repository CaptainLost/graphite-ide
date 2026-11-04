#include "Git/Window/MainLineQuestion.h"

#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextButton.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>

namespace Graphite {

	namespace {

		constexpr float kHalf = 0.5f;
		constexpr float kPaddingX = 12.0f;
		constexpr float kPaddingY = 8.0f;
		constexpr float kGap = 10.0f;
		constexpr float kButtonHeight = 26.0f;
		constexpr float kButtonPaddingX = 12.0f;
		constexpr float kButtonGap = 6.0f;
		constexpr float kBodyText = 12.5f / 13.0f;
		constexpr float kKeepFontSize = 0.0f;

		constexpr const char* kQuestion = "Which branch is this repo's main line?";

		float Em(float mockupPixels)
		{
			return WindowBarEm(mockupPixels);
		}

		std::string ExplanationOf(const MainLineAsk& ask)
		{
			return "No server gives it a default branch. Best guess: " + ask.GuessName +
				   ", the line most branches forked from in the last 90 days. Until you answer, it is marked as guessed; "
				   "any branch's menu has Use as main line.";
		}

		float ButtonWidth(const std::string& label)
		{
			return ImGui::CalcTextSize(label.c_str()).x + 2.0f * Em(kButtonPaddingX);
		}

		bool DrawButton(const char* label, ImVec2 at, float width, ImU32 fill, ImU32 text)
		{
			if (fill != 0)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + Em(kButtonHeight)), fill,
														  Core::TextButtonRounding());
			}

			ImGui::SetCursorScreenPos(at);

			if (text != 0)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, text);
			}

			const bool clicked = Core::TextButton(label, ImVec2(width, Em(kButtonHeight)));

			if (text != 0)
			{
				ImGui::PopStyleColor();
			}

			return clicked;
		}
	}

	std::optional<MainLineAsk> MainLineToAsk(const LineRead* read, bool dismissed)
	{
		if (read == nullptr || dismissed || !read->Model.MainLineGuessed || read->Model.Bases.empty())
		{
			return std::nullopt;
		}

		const Core::LineBase& base = read->Model.Bases.front();

		if (base.Reason != Core::BaseReason::MainLineGuess || base.Ref >= read->Refs.size())
		{
			return std::nullopt;
		}

		const Core::LineRef& guess = read->Refs[base.Ref];

		return MainLineAsk{.GuessRef = guess.FullName,
						   .GuessName = guess.Remote.empty() ? guess.ShortName : guess.Remote + "/" + guess.ShortName};
	}

	MainLineAnswer DrawMainLineQuestion(const MainLineAsk& ask, const WindowBarColours& colours)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float paddingX = Em(kPaddingX);
		const float gap = Em(kGap);

		const std::string useLabel = "Use " + ask.GuessName;
		const std::string notNowLabel = "Not now";
		const float useWidth = ButtonWidth(useLabel);
		const float notNowWidth = ButtonWidth(notNowLabel);
		const float buttonsWidth = useWidth + Em(kButtonGap) + notNowWidth;

		ImFont* const bold = Core::ImGuiRenderer::GetBoldFont();
		ImGui::PushFont(bold, kKeepFontSize);
		const float questionWidth = ImGui::CalcTextSize(kQuestion).x;
		ImGui::PopFont();

		const std::string explanation = ExplanationOf(ask);
		const float explanationLeft = origin.x + paddingX + questionWidth + gap;
		const float explanationWidth = std::max(Em(kButtonHeight), origin.x + width - paddingX - buttonsWidth - gap - explanationLeft);
		const float bodySize = ImGui::GetFontSize() * kBodyText;
		const float explanationHeight =
			ImGui::GetFont()->CalcTextSizeA(bodySize, FLT_MAX, explanationWidth, explanation.c_str()).y;

		const float height = std::max(explanationHeight, Em(kButtonHeight)) + 2.0f * Em(kPaddingY);
		const float centreY = origin.y + height * kHalf;

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), colours.AskFill);
		drawList->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + width, origin.y + height - 1.0f),
						  colours.Rule);

		drawList->AddText(bold, ImGui::GetFontSize(),
						  ImVec2(origin.x + paddingX, centreY - ImGui::GetTextLineHeight() * kHalf), colours.AskQuestion,
						  kQuestion);
		drawList->AddText(ImGui::GetFont(), bodySize, ImVec2(explanationLeft, centreY - explanationHeight * kHalf),
						  ImGui::GetColorU32(ImGuiCol_Text), explanation.c_str(), nullptr, explanationWidth);

		const float buttonTop = centreY - Em(kButtonHeight) * kHalf;
		const float buttonsLeft = origin.x + width - paddingX - buttonsWidth;

		MainLineAnswer answer = MainLineAnswer::None;

		ImGui::PushID("##mainLineQuestion");

		if (DrawButton(useLabel.c_str(), ImVec2(buttonsLeft, buttonTop), useWidth, colours.Accent, colours.OnAccent))
		{
			answer = MainLineAnswer::UseGuess;
		}

		if (DrawButton(notNowLabel.c_str(), ImVec2(buttonsLeft + useWidth + Em(kButtonGap), buttonTop), notNowWidth,
					   colours.ChipHoveredFill, 0))
		{
			answer = MainLineAnswer::NotNow;
		}

		ImGui::PopID();

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(width, height));

		return answer;
	}
}
