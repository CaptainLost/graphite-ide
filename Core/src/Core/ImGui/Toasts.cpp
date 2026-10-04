#include "corepch.h"

#include "Core/ImGui/Toasts.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	namespace {
		constexpr float kMaxWidth = 420.0f;
		constexpr float kMaxBodyLines = 8.0f;
		constexpr float kMargin = 12.0f;
		constexpr float kSpacing = 8.0f;
		constexpr float kAccentWidth = 4.0f;

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
		const float width = Scaled(kMaxWidth);
		const ImGuiStyle& style = ImGui::GetStyle();
		const float padding = style.WindowPadding.x;
		const float buttonWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		const float closeWidth = style.ItemSpacing.x + buttonWidth;
		// The message has the whole width; the close button shares the title's row, so the
		// title's wrap stops short of it.
		const float inner = width - padding * 2.0f - Scaled(kAccentWidth);
		const float titleWrap = inner - closeWidth;

		// Null when the bold face did not load: the title is then drawn in the current font.
		ImFont* const bold = ImGuiRenderer::GetBoldFont();

		// Bottom-up: the newest toast sits lowest, nearest the status bar, and older ones are
		// pushed up as new ones arrive.
		float bottom = viewport->WorkPos.y + viewport->WorkSize.y - Scaled(kMargin);

		// Copied: SetHovered and Close below change the stack while it is walked.
		const std::vector<Toast> toasts = m_Stack.Get();

		// A popup, a context menu or a modal is raised only on the frame it appears; a toast
		// raised every frame would cover it from its second frame on and take its hover - the
		// toast's own Copy menu included. So toasts go to the front only while none is open.
		const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

		bool toastFocused = false;

		for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
		{
			const Toast& toast = *it;

			const float enter = std::clamp(Seconds(now - toast.Shown) / Seconds(ToastStack::kEnter), 0.0f, 1.0f);
			const float slide = iam_eval_preset(iam_ease_out_back, enter);

			float alpha = 1.0f;

			if (toast.LeavingSince.has_value())
			{
				const float leave =
					std::clamp(Seconds(now - *toast.LeavingSince) / Seconds(ToastStack::kLeave), 0.0f, 1.0f);
				alpha = 1.0f - iam_eval_preset(iam_ease_in_quad, leave);
			}

			// The count follows the title in the regular face, so the title wraps short of it.
			const std::string count = toast.Count > 1 ? std::format("(x{})", toast.Count) : std::string();
			const float countWidth = count.empty() ? 0.0f : style.ItemSpacing.x + ImGui::CalcTextSize(count.c_str()).x;
			const float titleWidth = std::max(titleWrap - countWidth, 1.0f);

			// At least one line tall: the close button sits in the title's row.
			const float titleHeight = std::max(WrappedHeight(bold, toast.Title, titleWidth), ImGui::GetFontSize());

			// Only the message scrolls; the title stays above it, whatever the message's length.
			const float messageHeight = ImGui::CalcTextSize(toast.Message.c_str(), nullptr, false, inner).y;
			const float bodyHeight = std::min(messageHeight, ImGui::GetFontSize() * kMaxBodyLines);
			const float height = titleHeight + style.ItemSpacing.y + bodyHeight + style.WindowPadding.y * 2.0f;

			const float restX = viewport->WorkPos.x + viewport->WorkSize.x - Scaled(kMargin) - width;
			const float x = restX + (1.0f - slide) * (width + Scaled(kMargin) * 2.0f);
			const float y = bottom - height;

			ImGui::SetNextWindowPos(ImVec2(x, y));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowBgAlpha(0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

			const std::string name = std::format("{}{}", kWindowPrefix, toast.Id);
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
												ImGuiWindowFlags_NoSavedSettings |
												ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
												ImGuiWindowFlags_NoMove;

			bool hovered = false;

			if (ImGui::Begin(name.c_str(), nullptr, kFlags))
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
				const ImVec2 min = ImGui::GetWindowPos();
				const ImVec2 max(min.x + width, min.y + height);
				const float rounding = ImGui::GetStyle().WindowRounding;

				draw->AddRectFilled(min, max, ImGui::GetColorU32(ToImVec4(palette.Surface)), rounding);
				draw->AddRectFilled(min, ImVec2(min.x + Scaled(kAccentWidth), max.y),
									ImGui::GetColorU32(ToImVec4(AccentOf(palette, toast.Kind))), rounding,
									ImDrawFlags_RoundCornersLeft);

				const ImVec2 origin = ImGui::GetCursorPos();
				const float left = origin.x + Scaled(kAccentWidth);

				// The title, in the bold face, wrapped short of the count and the close button.
				ImGui::SetCursorPos(ImVec2(left, origin.y));
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

				if (!count.empty())
				{
					ImGui::SameLine();
					ImGui::TextUnformatted(count.c_str());
				}

				ImGui::PopStyleColor();

				// On the title's row, outside the body, so it never scrolls away.
				ImGui::SetCursorPos(ImVec2(width - padding - buttonWidth, origin.y));

				if (ImGui::SmallButton(ICON_MDI_CLOSE "##close"))
				{
					m_Stack.Close(toast.Id, now);
				}

				// The message, which scrolls past kMaxBodyLines.
				ImGui::SetCursorPos(ImVec2(left, origin.y + titleHeight + style.ItemSpacing.y));

				if (ImGui::BeginChild("body", ImVec2(inner, bodyHeight)))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(toast.Message.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}

				ImGui::EndChild();

				if (Core::BeginRightClickPopup("##toastMenu"))
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

			bottom = y - Scaled(kSpacing);
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

		if (const std::optional<ToastClock::time_point> next = m_Stack.NextChange(now); next.has_value())
		{
			Redraw::RequestIn(std::max(*next - now, ToastClock::duration::zero()));
		}
	}
}
