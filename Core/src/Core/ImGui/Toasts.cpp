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
#include <format>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Core {

	namespace {
		// The card's width, which is the window's.
		constexpr float kMaxWidth = 420.0f;
		constexpr float kMaxBodyLines = 8.0f;

		// Between the card and the edge of the work area, and between two cards.
		constexpr float kMargin = 12.0f;
		constexpr float kSpacing = 8.0f;

		// Inside the card: around its contents, between the kind's icon and the text, and
		// between the title and the message.
		constexpr float kPaddingX = 14.0f;
		constexpr float kPaddingY = 12.0f;
		constexpr float kIconGap = 12.0f;
		constexpr float kTitleGap = 2.0f;

		constexpr float kRounding = 8.0f;
		constexpr float kBorderWidth = 1.5f;

		// The kind's icon: a filled circle in its accent colour with the glyph cut out of it in
		// the surface colour.
		constexpr float kIconDiameter = 28.0f;

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
				return ICON_MDI_CHECK;
			case ToastKind::Warning:
				return ICON_MDI_EXCLAMATION;
			case ToastKind::Error:
				break;
			}

			return ICON_MDI_CLOSE;
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
		const float paddingX = Scaled(kPaddingX);
		const float paddingY = Scaled(kPaddingY);
		const float iconDiameter = Scaled(kIconDiameter);
		const float buttonWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		const float closeWidth = style.ItemSpacing.x + buttonWidth;

		// The title and the message share one left edge, the column after the icon, so the
		// message reads under the title rather than under the icon. In card coordinates, which
		// are the window's: 0 is the card's left edge.
		const float textLeft = paddingX + iconDiameter + Scaled(kIconGap);

		// The message runs to the card's right padding; the close button shares the title's
		// row, so the title's wrap stops short of it.
		const float messageWidth = width - textLeft - paddingX;
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
		std::unordered_set<std::uint64_t> drawn;
		bool moving = false;

		// Newest first, as they are drawn; raised in the other order once all are drawn.
		std::vector<ImGuiWindow*> raised;
		raised.reserve(toasts.size());

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

			// At least one line tall: the close button sits in the title's row.
			const float titleHeight = std::max(WrappedHeight(bold, toast.Title, titleWidth), ImGui::GetFontSize());

			// Only the message scrolls; the title stays above it, whatever the message's length.
			const float messageHeight = ImGui::CalcTextSize(toast.Message.c_str(), nullptr, false, messageWidth).y;
			const float bodyHeight = std::min(messageHeight, ImGui::GetFontSize() * kMaxBodyLines);
			const float textHeight = titleHeight + Scaled(kTitleGap) + bodyHeight;

			// The icon is centred on the text beside it. A block shorter than the icon grows the
			// card to the icon instead; one more than twice its height leaves it where it sits
			// beside a block of twice its height, near the title rather than adrift halfway
			// down a long message.
			const float iconTop =
				paddingY + std::clamp((textHeight - iconDiameter) * 0.5f, 0.0f, iconDiameter * 0.5f);
			const float height = paddingY + std::max(textHeight, iconDiameter) + paddingY;

			const std::string name = std::format("{}{}", kWindowPrefix, toast.Id);

			// The resting place moves smoothly when a toast below comes or goes. A toast seen
			// for the first time is cut to its place - it rises in by the offset below instead -
			// whatever tween an earlier stack left under the same id.
			const float targetY = bottom - height;
			const ImGuiID tweenId = ImHashStr(name.c_str());
			float restY = targetY;

			if (!m_Drawn.contains(toast.Id))
			{
				// An init value off the target makes ImAnim create the channel rather than skip
				// it, so the next frame's move has a channel to start from.
				restY = iam_tween_float(tweenId, RestingYChannel(), targetY, 0.0f, iam_ease_preset(iam_ease_out_cubic),
										iam_policy_cut, step, targetY + 1.0f);
			}
			else
			{
				// Crossfade, as a hover does: a toast already moving turns towards a new place
				// from where it is. The channel exists since the cut above, so the init value
				// goes unused.
				restY = iam_tween_float(tweenId, RestingYChannel(), targetY, kMoveSeconds,
										iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, step, targetY);
			}

			drawn.insert(toast.Id);
			moving = moving || std::fabs(restY - targetY) > kSettledEpsilon;

			const float cardX = viewport->WorkPos.x + viewport->WorkSize.x - Scaled(kMargin) - width;
			const float cardY = restY + (1.0f - entered) * Scaled(kRise);

			// The window is the card.
			ImGui::SetNextWindowPos(ImVec2(cardX, cardY));
			ImGui::SetNextWindowSize(ImVec2(width, height));
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
				raised.push_back(window);

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
				const ImVec2 min = ImGui::GetWindowPos();
				const ImVec2 max(min.x + width, min.y + height);
				const float rounding = Scaled(kRounding);
				const ImVec4 accent = ToImVec4(AccentOf(palette, toast.Kind));
				const ImVec4 surface = ToImVec4(palette.Surface);

				// GetColorU32 multiplies in the toast's fade.
				draw->AddRectFilled(min, max, ImGui::GetColorU32(surface), rounding);
				draw->AddRect(min, max, ImGui::GetColorU32(accent), rounding, ImDrawFlags_None, Scaled(kBorderWidth));

				// The kind's glyph, centred in its circle, in the regular face the icon font is
				// merged into.
				const ImVec2 iconCentre(min.x + paddingX + iconDiameter * 0.5f, min.y + iconTop + iconDiameter * 0.5f);
				const char* const glyph = IconOf(toast.Kind);
				const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);

				draw->AddCircleFilled(iconCentre, iconDiameter * 0.5f, ImGui::GetColorU32(accent));
				draw->AddText(ImVec2(std::floor(iconCentre.x - glyphSize.x * 0.5f),
									 std::floor(iconCentre.y - glyphSize.y * 0.5f)),
							  ImGui::GetColorU32(surface), glyph);

				const float rowY = paddingY;
				const float left = textLeft;

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
				// own: only the style's hover and press colours light it up, and the glyph goes
				// from the dimmed text colour to the text colour under the pointer - asked before
				// the button is drawn, so it is the pointer's place rather than ImGui's hover state.
				// The button is as tall as a line, so the title's first line centres it.
				const ImVec2 closeMin(min.x + width - paddingX - buttonWidth, min.y + rowY);
				const ImVec2 closeMax(closeMin.x + buttonWidth, closeMin.y + ImGui::GetFontSize());
				const bool overClose =
					ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && ImGui::IsMouseHoveringRect(closeMin, closeMax);

				ImGui::SetCursorPos(ImVec2(width - paddingX - buttonWidth, rowY));
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(overClose ? palette.Text : palette.TextDim));

				if (ImGui::SmallButton(ICON_MDI_CLOSE "##close"))
				{
					m_Stack.Close(toast.Id, now);
				}

				ImGui::PopStyleColor(2);

				// The message, dimmer than the title, which scrolls past kMaxBodyLines. On the
				// card's surface rather than a background of its own, and with no padding of its
				// own, so it starts at the title's x.
				ImGui::SetCursorPos(ImVec2(left, rowY + titleHeight + Scaled(kTitleGap)));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

				const bool bodyOpen = ImGui::BeginChild("body", ImVec2(messageWidth, bodyHeight));

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);

				if (bodyOpen)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.TextDim));
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(toast.Message.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}

				ImGui::EndChild();

				// Opened from anywhere on the toast - the title row as much as the body - rather
				// than through Core::BeginRightClickPopup, which answers only to the last item.
				// Otherwise the same: opened on release, and begun with NoMove for the reason
				// ContextMenu.h gives. The id lives in the toast window's own id stack.
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
					ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup))
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

				hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

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

		m_Drawn = std::move(drawn);

		// Over every window, floating ones clicked after the toast appeared included, yet
		// without focus: NoFocusOnAppearing keeps the keyboard in the editor. Oldest first, so
		// a newer toast sits in front of the older ones above it wherever they overlap - as
		// one rises in or glides into place. But a popup, a context menu or a modal is raised only on the frame it
		// appears; a toast raised every frame would cover it from its second frame on and take
		// its hover - the toast's own Copy menu included, opened in the loop above. So toasts go
		// to the front only while none is open.
		if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
		{
			for (auto it = raised.rbegin(); it != raised.rend(); ++it)
			{
				ImGui::BringWindowToDisplayFront(*it);
			}
		}

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
