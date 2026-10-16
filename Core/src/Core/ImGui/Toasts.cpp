#include "corepch.h"

#include "Core/ImGui/Toasts.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Timestep.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/ToastBorder.h"
#include "Core/ImGui/ToastStyle.h"
#include "Core/ImGui/ToastText.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Core {

	namespace {
		constexpr float kMaxBodyLines = 8.0f;
		constexpr float kSpacingBetweenCards = 8.0f;
		constexpr float kGapBetweenIconAndText = 12.0f;
		constexpr float kGapBetweenTitleAndMessage = 4.0f;
		constexpr float kCardRounding = 8.0f;
		constexpr float kBorderWidth = 1.5f;
		constexpr float kBorderAlpha = 0.55f;
		constexpr float kDrainedBorderAlphaKeepingCardEdge = 0.18f;
		constexpr float kStrokeInset = 0.5f;
		constexpr std::chrono::duration<double> kCountdownFrameInterval{1.0 / 60.0};
		constexpr float kFadeInRise = 8.0f;
		constexpr float kMoveToNewPlaceSeconds = 0.15f;
		constexpr float kSettledDistancePixels = 0.01f;
		constexpr float kHalf = 0.5f;
		constexpr float kShortestWidth = 1.0f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kNoWrap = 0.0f;
		constexpr float kNoBorderSize = 0.0f;
		constexpr float kTransparentBackgroundAlpha = 0.0f;
		constexpr float kInstantSeconds = 0.0f;
		constexpr float kInitialValueOffTarget = 1.0f;
		constexpr int kWindowOnlyStyleVarCount = 2;
		constexpr int kBodyColourCount = 2;
		constexpr std::string_view kWindowPrefix = "##toast";

		ImGuiID RestingYChannel()
		{
			static const ImGuiID channel = ImHashStr("Core::Toasts::RestingY");
			return channel;
		}

		std::vector<ImVec2> ToImVec2s(const std::vector<BorderPoint>& points)
		{
			std::vector<ImVec2> converted;
			converted.reserve(points.size());
			for (const BorderPoint& point : points)
			{
				converted.emplace_back(point.X, point.Y);
			}
			return converted;
		}

		void DrawOpenPath(ImDrawList& drawList, const std::vector<BorderPoint>& path, ImU32 colour, float thickness)
		{
			const std::vector<ImVec2> points = ToImVec2s(path);
			drawList.AddPolyline(points.data(), static_cast<int>(points.size()), colour, thickness, ImDrawFlags_None);
		}

		struct CountdownColours
		{
			ImU32 Remaining = 0;
			ImU32 Drained = 0;
		};

		int SegmentsPerCorner(const ImDrawList& drawList, float rounding)
		{
			constexpr int kCornersInACircle = 4;
			const int segmentsInWholeCircle = drawList._CalcCircleAutoSegmentCount(rounding);
			return (segmentsInWholeCircle + kCornersInACircle - 1) / kCornersInACircle;
		}

		void DrawCountdownBorder(ImDrawList& drawList, ImVec2 cardMin, ImVec2 cardMax, float rounding,
								 float shareOfLifetimeLeft, const CountdownColours& colours)
		{
			const std::vector<BorderPoint> outline =
				TraceToastBorder(cardMin.x + kStrokeInset, cardMin.y + kStrokeInset, cardMax.x - kStrokeInset,
								 cardMax.y - kStrokeInset, rounding, SegmentsPerCorner(drawList, rounding));
			const float thickness = Scaled(kBorderWidth);

			const bool wholeOutlineRemains = shareOfLifetimeLeft >= 1.0f;
			if (wholeOutlineRemains)
			{
				const std::vector<ImVec2> loop = ToImVec2s(outline);
				const int pointsWithoutRepeatedStart = static_cast<int>(loop.size()) - 1;
				drawList.AddPolyline(loop.data(), pointsWithoutRepeatedStart, colours.Remaining, thickness,
									 ImDrawFlags_Closed);
				return;
			}

			const SplitBorder split = SplitToastBorder(outline, shareOfLifetimeLeft);
			DrawOpenPath(drawList, split.Drained, colours.Drained, thickness);
			DrawOpenPath(drawList, split.Remaining, colours.Remaining, thickness);
		}

		float Seconds(SteadyClock::duration duration)
		{
			return std::chrono::duration<float>(duration).count();
		}

		bool BelongsToToast(const ImGuiWindow& window)
		{
			const ImGuiWindow* const root = window.RootWindowPopupTree;
			return root != nullptr && std::string_view(root->Name).starts_with(kWindowPrefix);
		}

		ImFont* FontForRun(bool strong, ImFont* boldFont, ImFont* regularFont)
		{
			return strong && boldFont != nullptr ? boldFont : regularFont;
		}

		// CalcTextSize rounds each run up, so runs placed by rounded widths drift apart from where AddText
		// ends them.
		float UnroundedTextWidth(ImFont* font, float fontSize, std::string_view text)
		{
			return font->CalcTextSizeA(fontSize, FLT_MAX, kNoWrap, text.data(), text.data() + text.size()).x;
		}

		float WrappedHeight(ImFont* font, const std::string& text, float wrapWidth)
		{
			const char* const textEnd = nullptr;
			const bool hideTextAfterDoubleHash = false;

			if (font != nullptr)
			{
				ImGui::PushFont(font, kKeepFontSize);
			}

			const float height = ImGui::CalcTextSize(text.c_str(), textEnd, hideTextAfterDoubleHash, wrapWidth).y;

			if (font != nullptr)
			{
				ImGui::PopFont();
			}

			return height;
		}
	}

	std::uint64_t Toasts::Show(ToastKind kind, std::string title, std::string message, std::string copyText,
							   std::uint32_t group)
	{
		return m_Stack.Show(kind, std::move(title), std::move(message), SteadyClock::now(), std::move(copyText), group);
	}

	void Toasts::Render(const ToastPalette& palette)
	{
		const SteadyClock::time_point now = SteadyClock::now();
		m_Stack.Update(now);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float width = ToastCardWidth();
		const float paddingX = ToastPaddingX();
		const float paddingY = ToastPaddingY();
		const float iconDiameter = ToastIconDiameter();
		const float closeButtonWidth = IconButtonSize(IconButtonFit::Line);
		const float closeButtonSlotWidth = style.ItemSpacing.x + closeButtonWidth;

		const float textLeft = paddingX + iconDiameter + Scaled(kGapBetweenIconAndText);
		const float messageWidth = width - textLeft - paddingX;
		const float titleWrapWidth = messageWidth - closeButtonSlotWidth;

		ImFont* const boldFont = ImGuiRenderer::GetBoldFont();
		ImFont* const regularFont = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();

		const ToastTextMeasure measureText = [&](std::string_view text, bool strong) { return UnroundedTextWidth(FontForRun(strong, boldFont, regularFont), fontSize, text); };

		const float frameStepSeconds = std::min(ImGui::GetIO().DeltaTime, Animation::kLongestStepSeconds);

		float nextCardBottom = viewport->WorkPos.y + viewport->WorkSize.y - ToastMargin();

		const std::vector<Toast> toasts = m_Stack.Get();

		std::unordered_set<std::uint64_t> drawnIds;
		bool anyToastMoving = false;

		std::vector<ImGuiWindow*> raisedWindows;
		raisedWindows.reserve(toasts.size());

		ImGuiWindow* focusedToast = nullptr;

		for (std::vector<Toast>::const_reverse_iterator toastIterator = toasts.rbegin(); toastIterator != toasts.rend();
			 ++toastIterator)
		{
			const Toast& toast = *toastIterator;

			const float fadeInProgress =
				std::clamp(Seconds(now - toast.Shown) / Seconds(ToastStack::kEnter), 0.0f, 1.0f);
			const float fadeInEased = iam_eval_preset(iam_ease_out_cubic, fadeInProgress);

			float alpha = fadeInEased;

			if (toast.LeavingSince.has_value())
			{
				const float fadeOutProgress =
					std::clamp(Seconds(now - *toast.LeavingSince) / Seconds(ToastStack::kLeave), 0.0f, 1.0f);
				alpha *= 1.0f - iam_eval_preset(iam_ease_in_quad, fadeOutProgress);
			}

			const std::string repeatCountText = toast.Count > 1 ? std::format("(x{})", toast.Count) : std::string();
			const float repeatCountWidth =
				repeatCountText.empty() ? 0.0f : style.ItemSpacing.x + ImGui::CalcTextSize(repeatCountText.c_str()).x;
			const float titleWidth = std::max(titleWrapWidth - repeatCountWidth, kShortestWidth);

			const float titleHeight = std::max(WrappedHeight(boldFont, toast.Title, titleWidth), fontSize);

			float layoutWidth = messageWidth;
			std::vector<ToastTextLine> lines = LayoutToastText(toast.Message, layoutWidth, measureText);

			if (static_cast<float>(lines.size()) > kMaxBodyLines)
			{
				layoutWidth = std::max(messageWidth - style.ScrollbarSize, kShortestWidth);
				lines = LayoutToastText(toast.Message, layoutWidth, measureText);
			}

			const float messageHeight = static_cast<float>(std::max<std::size_t>(lines.size(), 1)) * fontSize;
			const float bodyHeight = std::min(messageHeight, fontSize * kMaxBodyLines);
			const float textHeight = titleHeight + Scaled(kGapBetweenTitleAndMessage) + bodyHeight;

			const float iconCentreY = paddingY + fontSize * kHalf;
			const float height =
				std::max(paddingY + textHeight + paddingY, iconCentreY + iconDiameter * kHalf + paddingY);

			const std::string windowName = std::format("{}{}", kWindowPrefix, toast.Id);

			const float restingY = nextCardBottom - height;
			const ImGuiID tweenId = ImHashStr(windowName.c_str());
			float tweenedRestingY = restingY;

			if (!m_Drawn.contains(toast.Id))
			{
				// An init value off the target makes ImAnim create the channel rather than skip it, so the next
				// frame's move has a channel to start from.
				tweenedRestingY = iam_tween_float(tweenId, RestingYChannel(), restingY, kInstantSeconds,
												  iam_ease_preset(iam_ease_out_cubic), iam_policy_cut,
												  frameStepSeconds, restingY + kInitialValueOffTarget);
			}
			else
			{
				tweenedRestingY = iam_tween_float(tweenId, RestingYChannel(), restingY, kMoveToNewPlaceSeconds,
												  iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade,
												  frameStepSeconds, restingY);
			}

			drawnIds.insert(toast.Id);
			anyToastMoving = anyToastMoving || std::fabs(tweenedRestingY - restingY) > kSettledDistancePixels;

			const float cardX = viewport->WorkPos.x + viewport->WorkSize.x - ToastMargin() - width;
			const float cardY = tweenedRestingY + (1.0f - fadeInEased) * Scaled(kFadeInRise);

			ImGui::SetNextWindowPos(ImVec2(cardX, cardY));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowBgAlpha(kTransparentBackgroundAlpha);

			const ImVec2 noWindowPadding(0.0f, 0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, noWindowPadding);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kNoBorderSize);

			constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
													  ImGuiWindowFlags_NoSavedSettings |
													  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
													  ImGuiWindowFlags_NoMove;

			const bool windowOpen = ImGui::Begin(windowName.c_str(), nullptr, kWindowFlags);
			ImGui::PopStyleVar(kWindowOnlyStyleVarCount);

			bool hovered = false;

			if (windowOpen)
			{
				ImGuiWindow* const window = ImGui::GetCurrentWindow();
				raisedWindows.push_back(window);

				ImDrawList* const cardDrawList = ImGui::GetWindowDrawList();
				const ImVec2 cardMin = ImGui::GetWindowPos();
				const ImVec2 cardMax(cardMin.x + width, cardMin.y + height);
				const float rounding = Scaled(kCardRounding);
				const ImVec4 border = WithAlphaScaled(ToImVec4(ToastAccentOf(palette, toast.Kind)), kBorderAlpha);
				const ImVec4 drainedBorder =
					WithAlphaScaled(ToImVec4(ToastAccentOf(palette, toast.Kind)), kDrainedBorderAlphaKeepingCardEdge);
				const ImVec4 surface = ToImVec4(palette.Surface);

				cardDrawList->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(surface), rounding);
				DrawCountdownBorder(*cardDrawList, cardMin, cardMax, rounding,
									ToastStack::ShareOfLifetimeLeft(toast, now),
									{.Remaining = ImGui::GetColorU32(border), .Drained = ImGui::GetColorU32(drainedBorder)});

				const ImVec2 iconCentre(cardMin.x + paddingX + iconDiameter * kHalf, cardMin.y + iconCentreY);
				DrawToastIcon(*cardDrawList, toast.Kind, iconCentre, palette);

				const float rowY = paddingY;

				ImGui::SetCursorPos(ImVec2(textLeft, rowY));
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));

				if (boldFont != nullptr)
				{
					ImGui::PushFont(boldFont, kKeepFontSize);
				}

				ImGui::PushTextWrapPos(textLeft + titleWidth);
				ImGui::TextUnformatted(toast.Title.c_str());
				ImGui::PopTextWrapPos();

				if (boldFont != nullptr)
				{
					ImGui::PopFont();
				}

				ImGui::PopStyleColor();

				if (!repeatCountText.empty())
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.TextDim));
					ImGui::TextUnformatted(repeatCountText.c_str());
					ImGui::PopStyleColor();
				}

				const ImVec4 transparentColour(0.0f, 0.0f, 0.0f, 0.0f);
				constexpr bool kNotEngaged = false;

				ImGui::SetCursorPos(ImVec2(width - paddingX - closeButtonWidth, rowY));

				if (DrawIconButton("##close", ICON_MDI_CLOSE, kNotEngaged, "Close", {.Fit = IconButtonFit::Line}))
				{
					m_Stack.Close(toast.Id, now);
				}

				ImGui::SetCursorPos(ImVec2(textLeft, rowY + titleHeight + Scaled(kGapBetweenTitleAndMessage)));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, transparentColour);
				ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, transparentColour);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, noWindowPadding);

				const bool bodyOpen = ImGui::BeginChild("body", ImVec2(messageWidth, bodyHeight));

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(kBodyColourCount);

				if (bodyOpen)
				{
					ImDrawList* const bodyDrawList = ImGui::GetWindowDrawList();
					const ImVec2 origin = ImGui::GetCursorScreenPos();
					const ImU32 plainColour = ImGui::GetColorU32(ToImVec4(ToastMessageColour(palette)));
					const ImU32 strongColour = ImGui::GetColorU32(ToImVec4(palette.Text));

					for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
					{
						const float lineY = origin.y + static_cast<float>(lineIndex) * fontSize;

						for (const ToastTextRun& run : lines[lineIndex].Runs)
						{
							const bool strong = run.Style == ToastTextStyle::Strong;
							ImFont* const font = FontForRun(strong, boldFont, regularFont);

							bodyDrawList->AddText(font, fontSize, ImVec2(origin.x + run.X, lineY),
												  strong ? strongColour : plainColour, run.Text.data(),
												  run.Text.data() + run.Text.size());
						}
					}

					ImGui::Dummy(ImVec2(layoutWidth, messageHeight));
				}

				ImGui::EndChild();

				const bool clickedOnToast =
					ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
					ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

				if (BeginRightClickMenu("##toastMenu", clickedOnToast))
				{
					// A toast under an open popup is not raised, but one raised on the frame the menu opened
					// would cover it.
					ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

					if (ImGui::MenuItem("Copy"))
					{
						const std::string copied =
							toast.CopyText.empty() ? toast.Title + "\n" + toast.Message : toast.CopyText;

						if (OnCopied)
						{
							OnCopied(copied);
						}
						else
						{
							ImGui::SetClipboardText(copied.c_str());
						}
					}

					ImGui::EndPopup();
				}

				hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

				// NoFocusOnAppearing covers only the appearing: a later click focuses the toast like any
				// window and the editor loses the keyboard. Asked after the toast's items, because a press
				// on the close button or the scrollbar focuses the toast inside the item's behaviour.
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy))
				{
					focusedToast = window;
				}
			}

			m_Stack.SetHovered(toast.Id, hovered, now);

			ImGui::End();
			ImGui::PopStyleVar();

			nextCardBottom = restingY - Scaled(kSpacingBetweenCards);
		}

		m_Drawn = std::move(drawnIds);

		// Run after the loop: inside it the close button is still active on the frame its release comes,
		// and focusing another window clears the active item, so the click would never count.
		if (focusedToast != nullptr && ImGui::GetActiveID() == 0 && !ImGui::IsAnyMouseDown())
		{
			ImGuiWindow* const holder = ImGui::FindWindowByID(m_FocusBeforeToasts);

			if (holder != nullptr && holder->WasActive)
			{
				ImGui::FocusWindow(holder);
			}
			else
			{
				ImGuiViewport* const anyViewport = nullptr;
				ImGui::FocusTopMostWindowUnderOne(focusedToast, focusedToast, anyViewport, ImGuiFocusRequestFlags_None);
			}
		}

		// A toast raised every frame while a popup is open would cover it from its second frame on and take
		// its hover, the toast's own Copy menu included.
		if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
		{
			for (std::vector<ImGuiWindow*>::const_reverse_iterator raisedIterator = raisedWindows.rbegin();
				 raisedIterator != raisedWindows.rend(); ++raisedIterator)
			{
				ImGui::BringWindowToDisplayFront(*raisedIterator);
			}
		}

		if (focusedToast == nullptr)
		{
			const ImGuiWindow* const holder = ImGui::GetCurrentContext()->NavWindow;

			if (holder == nullptr)
			{
				m_FocusBeforeToasts = 0;
			}
			else if (!BelongsToToast(*holder))
			{
				m_FocusBeforeToasts = holder->ID;
			}
		}

		if (anyToastMoving)
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}

		if (m_Stack.IsAnyCountdownRunning())
		{
			Redraw::RequestIn(kCountdownFrameInterval);
		}

		if (const std::optional<SteadyClock::time_point> nextChange = m_Stack.NextChange(now); nextChange.has_value())
		{
			Redraw::RequestIn(std::max(*nextChange - now, SteadyClock::duration::zero()));
		}
	}
}
