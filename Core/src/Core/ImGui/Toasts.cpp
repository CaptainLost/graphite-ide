#include "corepch.h"

#include "Core/ImGui/Toasts.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core {

	namespace {
		// The card's width; the window around it is wider by the shadow margin on each side.
		constexpr float kMaxWidth = 420.0f;
		constexpr float kMaxBodyLines = 8.0f;

		// Between the card and the edge of the work area, and between two cards.
		constexpr float kMargin = 12.0f;
		constexpr float kSpacing = 8.0f;

		// Inside the card: around its contents, between the kind's icon and the text, and
		// between the title row and the message.
		constexpr float kPadding = 12.0f;
		constexpr float kIconGap = 8.0f;
		constexpr float kTitleGap = 6.0f;

		constexpr float kRounding = 6.0f;
		constexpr float kBorderWidth = 1.0f;

		// The shadow is a few rounded rects behind the card, each grown and shifted down by
		// kShadowStep more than the last, so it reaches 2 * kShadowStep * count below the card
		// and kShadowStep * count to its sides. The window is that much larger than the card
		// on every side, for ImGui clips a window's drawing to the window.
		constexpr float kShadowStep = 2.0f;
		constexpr float kShadowAlphas[] = {0.18f, 0.10f, 0.05f};
		constexpr float kShadowMargin = kShadowStep * 2.0f * static_cast<float>(std::size(kShadowAlphas));

		// How far below its resting place a toast starts as it fades in.
		constexpr float kRise = 8.0f;

		// How long a toast takes to move to its new place when one below it comes or goes.
		constexpr float kMoveSeconds = 0.15f;

		// Closer than this to its resting place a moving toast has arrived: a hundredth of a
		// pixel is below what a display shows.
		constexpr float kSettledEpsilon = 0.01f;

		// As Animation::HoverAmount caps it, for the same reason: the first frame after the
		// loop has slept carries the whole sleep in DeltaTime.
		constexpr float kLongestStepSeconds = 0.1f;

		// ImAnim keys a tween by item and channel; the toasts' resting y has a channel of its
		// own, so it never collides with a hover under the same id.
		ImGuiID RestingYChannel()
		{
			static const ImGuiID channel = ImHashStr("Core::Toasts::RestingY");
			return channel;
		}

		ImVec4 ToImVec4(const Color& color, float alpha = 1.0f)
		{
			return ImVec4(color.R, color.G, color.B, color.A * alpha);
		}

		const Color& AccentOf(const ToastPalette& palette, ToastKind kind)
		{
			switch (kind)
			{
			case ToastKind::Success:
				return palette.Success;
			case ToastKind::Warning:
				return palette.Warning;
			case ToastKind::Error:
				break;
			}

			return palette.Error;
		}

		const char* IconOf(ToastKind kind)
		{
			switch (kind)
			{
			case ToastKind::Success:
				return ICON_MDI_CHECK_CIRCLE;
			case ToastKind::Warning:
				return ICON_MDI_ALERT;
			case ToastKind::Error:
				break;
			}

			return ICON_MDI_ALERT_CIRCLE;
		}

		float Seconds(ToastClock::duration duration)
		{
			return std::chrono::duration<float>(duration).count();
		}

		// Every toast window's name starts with this; the number after it is the toast's id.
		constexpr std::string_view kWindowPrefix = "##toast";

		// A toast window, a child of one - the scrolling body - or a popup opened from one, its
		// Copy menu.
		bool BelongsToToast(const ImGuiWindow& window)
		{
			const ImGuiWindow* const root = window.RootWindowPopupTree;
			return root != nullptr && std::string_view(root->Name).starts_with(kWindowPrefix);
		}

		// Measured in the face the text is drawn in: the bold face is wider than the regular
		// one, so a title measured in the regular face would wrap later than it is drawn.
		float WrappedHeight(ImFont* font, const std::string& text, float wrap)
		{
			if (font != nullptr)
			{
				ImGui::PushFont(font, 0.0f);
			}

			const float height = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y;

			if (font != nullptr)
			{
				ImGui::PopFont();
			}

			return height;
		}
	}

	void Toasts::Show(ToastKind kind, std::string title, std::string message)
	{
		m_Stack.Show(kind, std::move(title), std::move(message), ToastClock::now());
	}

	void Toasts::Render(const ToastPalette& palette)
	{
		const ToastClock::time_point now = ToastClock::now();
		m_Stack.Update(now);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float width = Scaled(kMaxWidth);
		const float padding = Scaled(kPadding);
		const float shadowMargin = Scaled(kShadowMargin);
		const float buttonWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		const float closeWidth = style.ItemSpacing.x + buttonWidth;

		// The title and the message share one left edge, the column after the icon, so the
		// message reads under the title rather than under the icon. In card coordinates: 0 is
		// the card's left edge. The icons are measured in the regular face, the one the icon
		// font is merged into and the one they are drawn in.
		const float iconWidth = std::max({ImGui::CalcTextSize(ICON_MDI_ALERT_CIRCLE).x,
										  ImGui::CalcTextSize(ICON_MDI_ALERT).x,
										  ImGui::CalcTextSize(ICON_MDI_CHECK_CIRCLE).x});
		const float textLeft = padding + iconWidth + Scaled(kIconGap);

		// The message runs to the card's right padding; the close button shares the title's
		// row, so the title's wrap stops short of it.
		const float messageWidth = width - textLeft - padding;
		const float titleWrap = messageWidth - closeWidth;

		// Null when the bold face did not load: the title is then drawn in the current font.
		ImFont* const bold = ImGuiRenderer::GetBoldFont();

		// As Animation::HoverAmount takes it; see kLongestStepSeconds.
		const float step = std::min(ImGui::GetIO().DeltaTime, kLongestStepSeconds);

		// Bottom-up: the newest toast sits lowest, nearest the status bar, and older ones are
		// pushed up as new ones arrive. Laid out from where each toast rests, never from where
		// its tween has got to, so a move under way does not shift the toasts above it twice.
		float bottom = viewport->WorkPos.y + viewport->WorkSize.y - Scaled(kMargin);

		// Copied: SetHovered and Close below change the stack while it is walked.
		const std::vector<Toast> toasts = m_Stack.Get();

		// Rebuilt every frame, so a toast that has gone leaves no entry behind.
		std::unordered_map<std::uint64_t, float> restingY;
		bool moving = false;

		// A popup, a context menu or a modal is raised only on the frame it appears; a toast
		// raised every frame would cover it from its second frame on and take its hover - the
		// toast's own Copy menu included. So toasts go to the front only while none is open.
		const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

		bool toastFocused = false;

		for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
		{
			const Toast& toast = *it;

			// In: a fade with a slight rise, no overshoot. Out: a fade where it stands.
			const float enter = std::clamp(Seconds(now - toast.Shown) / Seconds(ToastStack::kEnter), 0.0f, 1.0f);
			const float entered = iam_eval_preset(iam_ease_out_cubic, enter);

			float alpha = entered;

			if (toast.LeavingSince.has_value())
			{
				const float leave =
					std::clamp(Seconds(now - *toast.LeavingSince) / Seconds(ToastStack::kLeave), 0.0f, 1.0f);
				alpha *= 1.0f - iam_eval_preset(iam_ease_in_quad, leave);
			}

			// The count follows the title in the regular face, so the title wraps short of it.
			const std::string count = toast.Count > 1 ? std::format("(x{})", toast.Count) : std::string();
			const float countWidth = count.empty() ? 0.0f : style.ItemSpacing.x + ImGui::CalcTextSize(count.c_str()).x;
			const float titleWidth = std::max(titleWrap - countWidth, 1.0f);

			// At least one line tall: the icon and the close button sit in the title's row.
			const float titleHeight = std::max(WrappedHeight(bold, toast.Title, titleWidth), ImGui::GetFontSize());

			// Only the message scrolls; the title stays above it, whatever the message's length.
			const float messageHeight = ImGui::CalcTextSize(toast.Message.c_str(), nullptr, false, messageWidth).y;
			const float bodyHeight = std::min(messageHeight, ImGui::GetFontSize() * kMaxBodyLines);
			const float height = padding + titleHeight + Scaled(kTitleGap) + bodyHeight + padding;

			const std::string name = std::format("{}{}", kWindowPrefix, toast.Id);

			// The resting place moves smoothly when a toast below comes or goes. A toast seen
			// for the first time is cut to its place - it rises in by the offset below instead -
			// whatever tween an earlier stack left under the same id.
			const float targetY = bottom - height;
			const ImGuiID tweenId = ImHashStr(name.c_str());
			float restY = targetY;

			if (const auto known = m_RestingY.find(toast.Id); known == m_RestingY.end())
			{
				// An init value off the target makes ImAnim create the channel rather than skip
				// it, so the next frame's move has a channel to start from.
				restY = iam_tween_float(tweenId, RestingYChannel(), targetY, 0.0f, iam_ease_preset(iam_ease_out_cubic),
										iam_policy_cut, step, targetY + 1.0f);
			}
			else
			{
				// Crossfade, as a hover does: a toast already moving turns towards a new place
				// from where it is.
				restY = iam_tween_float(tweenId, RestingYChannel(), targetY, kMoveSeconds,
										iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, step, known->second);
			}

			restingY[toast.Id] = targetY;
			moving = moving || std::fabs(restY - targetY) > kSettledEpsilon;

			const float cardX = viewport->WorkPos.x + viewport->WorkSize.x - Scaled(kMargin) - width;
			const float cardY = restY + (1.0f - entered) * Scaled(kRise);

			// The window holds the card and its shadow; the card sits inset by the margin.
			ImGui::SetNextWindowPos(ImVec2(cardX - shadowMargin, cardY - shadowMargin));
			ImGui::SetNextWindowSize(ImVec2(width + shadowMargin * 2.0f, height + shadowMargin * 2.0f));
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowBgAlpha(0.0f);

			// The alpha for everything the toast draws, its menu included; the padding and the
			// border only for the window's own Begin, which places its contents by hand and
			// draws its own card.
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
												ImGuiWindowFlags_NoSavedSettings |
												ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
												ImGuiWindowFlags_NoMove;

			const bool open = ImGui::Begin(name.c_str(), nullptr, kFlags);
			ImGui::PopStyleVar(2);

			bool hovered = false;

			if (open)
			{
				ImGuiWindow* const window = ImGui::GetCurrentWindow();

				// Over every window, floating ones clicked after the toast appeared included, yet
				// without focus: NoFocusOnAppearing keeps the keyboard in the editor.
				if (!popupOpen)
				{
					ImGui::BringWindowToDisplayFront(window);
				}

				// NoFocusOnAppearing covers only the appearing: a click on the body, its scrollbar
				// or the close button focuses the toast as it would any window, and the editor
				// loses the keyboard to a box that has nothing to type into. Handed back once no
				// button is down - during the press it would end a drag of the scrollbar - to the
				// window that held focus before the click. ImGui moves focus on a click only at
				// the end of the frame, so a frame with no toast focused still names that window.
				// The popup hierarchy is left out: the toast's own menu holds focus while open,
				// and refocusing the editor then would close it.
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy))
				{
					toastFocused = true;

					if (!ImGui::IsAnyMouseDown())
					{
						ImGuiWindow* const holder = ImGui::FindWindowByID(m_FocusBeforeToasts);

						// A window that has gone since - a popup the click closed - leaves the
						// keyboard to whichever window was focused before this toast.
						if (holder != nullptr && holder->WasActive)
						{
							ImGui::FocusWindow(holder);
						}
						else
						{
							ImGui::FocusTopMostWindowUnderOne(window, window, nullptr, ImGuiFocusRequestFlags_None);
						}
					}
				}

				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 windowPos = ImGui::GetWindowPos();
				const ImVec2 min(windowPos.x + shadowMargin, windowPos.y + shadowMargin);
				const ImVec2 max(min.x + width, min.y + height);
				const float rounding = Scaled(kRounding);

				// Outermost first, each layer grown and shifted down by one more step, its
				// corners concentric with the card's so no corner of the shadow runs out of line.
				// GetColorU32 multiplies in the toast's fade.
				for (std::size_t layer = std::size(kShadowAlphas); layer > 0; --layer)
				{
					const float grow = Scaled(kShadowStep) * static_cast<float>(layer);
					const ImVec4 shade(0.0f, 0.0f, 0.0f, kShadowAlphas[layer - 1]);

					draw->AddRectFilled(ImVec2(min.x - grow, min.y), ImVec2(max.x + grow, max.y + grow * 2.0f),
										ImGui::GetColorU32(shade), rounding + grow);
				}

				draw->AddRectFilled(min, max, ImGui::GetColorU32(ToImVec4(palette.Surface)), rounding);
				draw->AddRect(min, max, ImGui::GetColorU32(ToImVec4(palette.Border)), rounding, ImDrawFlags_None,
							  Scaled(kBorderWidth));

				// The kind's icon, in its accent colour, on the title's first line.
				draw->AddText(ImVec2(min.x + padding, min.y + padding),
							  ImGui::GetColorU32(ToImVec4(AccentOf(palette, toast.Kind))), IconOf(toast.Kind));

				// Cursor positions are in window coordinates: the card starts the margin in.
				const float rowY = shadowMargin + padding;
				const float left = shadowMargin + textLeft;

				// The title, in the bold face, wrapped short of the count and the close button.
				ImGui::SetCursorPos(ImVec2(left, rowY));
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));

				if (bold != nullptr)
				{
					ImGui::PushFont(bold, 0.0f);
				}

				ImGui::PushTextWrapPos(left + titleWidth);
				ImGui::TextUnformatted(toast.Title.c_str());
				ImGui::PopTextWrapPos();

				if (bold != nullptr)
				{
					ImGui::PopFont();
				}

				ImGui::PopStyleColor();

				if (!count.empty())
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.TextDim));
					ImGui::TextUnformatted(count.c_str());
					ImGui::PopStyleColor();
				}

				// On the title's row, outside the body, so it never scrolls away. No frame of its
				// own: only the style's hover and press colours light it up.
				ImGui::SetCursorPos(ImVec2(shadowMargin + width - padding - buttonWidth, rowY));
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

				if (ImGui::SmallButton(ICON_MDI_CLOSE "##close"))
				{
					m_Stack.Close(toast.Id, now);
				}

				ImGui::PopStyleColor();

				// The message, which scrolls past kMaxBodyLines.
				ImGui::SetCursorPos(ImVec2(left, rowY + titleHeight + Scaled(kTitleGap)));

				if (ImGui::BeginChild("body", ImVec2(messageWidth, bodyHeight)))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(toast.Message.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}

				ImGui::EndChild();

				// The card, not the window: the shadow margin around it is transparent. ImGui
				// hit-tests a window by its whole rect, so the margin still takes the pointer
				// from whatever lies under it - accepted, a dozen pixels beside the card - but
				// only the card pauses the toast or opens its menu.
				const bool overCard = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
															 ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
									  ImGui::IsMouseHoveringRect(min, max, false);

				// Opened from anywhere on the card - the title row as much as the body - rather
				// than through Core::BeginRightClickPopup, which answers only to the last item.
				// Otherwise the same: opened on release, and begun with NoMove for the reason
				// ContextMenu.h gives. The id lives in the toast window's own id stack.
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && overCard)
				{
					ImGui::OpenPopup("##toastMenu");
				}

				if (ImGui::BeginPopup("##toastMenu", ImGuiWindowFlags_NoMove))
				{
					// Raised every frame it is open: the toast under it is not raised while a
					// popup is open, but one raised on the frame the menu opened would cover it.
					ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

					if (ImGui::MenuItem("Copy"))
					{
						const std::string copied = toast.Title + "\n" + toast.Message;

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

				hovered = overCard;

				// Checked again here: a press on the close button or the body's scrollbar
				// focuses the toast inside the button's behaviour, after the check above has
				// already said no. Unseen, the end of the frame would record the toast as the
				// window to hand the keyboard back to, and the next frame would hand it to itself.
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy))
				{
					toastFocused = true;
				}
			}

			// Reported even when Begin returned false, so a stale hover cannot outlive the window.
			m_Stack.SetHovered(toast.Id, hovered, now);

			ImGui::End();
			ImGui::PopStyleVar();

			bottom = targetY - Scaled(kSpacing);
		}

		m_RestingY = std::move(restingY);

		// Remembered only while no toast holds focus: once one does, the focused window is the
		// toast itself, and the one to hand back to is the window recorded before the click.
		// Nor is a window of a toast's own ever recorded - the toast, its body, its Copy menu -
		// whatever the checks above missed: handed the keyboard back, a toast would keep it, so
		// the window from before the click stays recorded instead.
		if (!toastFocused)
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

		// A move under way asks for its own frames: it starts when a toast below goes, after
		// the stack's own schedule has nothing left to animate.
		if (moving)
		{
			Redraw::RequestIn(Redraw::Clock::duration::zero());
		}

		if (const std::optional<ToastClock::time_point> next = m_Stack.NextChange(now); next.has_value())
		{
			Redraw::RequestIn(std::max(*next - now, ToastClock::duration::zero()));
		}
	}
}
