#include "corepch.h"

#include "Core/ImGui/Toasts.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <optional>
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
	}

	void Toasts::Show(ToastKind kind, std::string message)
	{
		m_Stack.Show(kind, std::move(message), ToastClock::now());
	}

	void Toasts::Render(const ToastPalette& palette)
	{
		const ToastClock::time_point now = ToastClock::now();
		m_Stack.Update(now);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = Scaled(kMaxWidth);
		const float padding = ImGui::GetStyle().WindowPadding.x;
		// The close button shares the row with the body, so its width is taken off the wrap.
		const ImGuiStyle& style = ImGui::GetStyle();
		const float closeWidth = style.ItemSpacing.x + ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		const float wrap = width - padding * 2.0f - Scaled(kAccentWidth) - closeWidth;

		// Bottom-up: the newest toast sits lowest, nearest the status bar, and older ones are
		// pushed up as new ones arrive.
		float bottom = viewport->WorkPos.y + viewport->WorkSize.y - Scaled(kMargin);

		// Copied: SetHovered and Close below change the stack while it is walked.
		const std::vector<Toast> toasts = m_Stack.Get();

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

			const std::string text =
				toast.Count > 1 ? std::format("{}  (x{})", toast.Message, toast.Count) : toast.Message;

			const float textHeight = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y;
			const float bodyHeight = std::min(textHeight, ImGui::GetFontSize() * kMaxBodyLines);
			const float height = bodyHeight + ImGui::GetStyle().WindowPadding.y * 2.0f;

			const float restX = viewport->WorkPos.x + viewport->WorkSize.x - Scaled(kMargin) - width;
			const float x = restX + (1.0f - slide) * (width + Scaled(kMargin) * 2.0f);
			const float y = bottom - height;

			ImGui::SetNextWindowPos(ImVec2(x, y));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowBgAlpha(0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

			const std::string name = std::format("##toast{}", toast.Id);
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
												ImGuiWindowFlags_NoSavedSettings |
												ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
												ImGuiWindowFlags_NoMove;

			bool hovered = false;

			if (ImGui::Begin(name.c_str(), nullptr, kFlags))
			{
				// Over every window, floating ones clicked after the toast appeared included, yet
				// without focus: NoFocusOnAppearing keeps the keyboard in the editor.
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 min = ImGui::GetWindowPos();
				const ImVec2 max(min.x + width, min.y + height);
				const float rounding = ImGui::GetStyle().WindowRounding;

				draw->AddRectFilled(min, max, ImGui::GetColorU32(ToImVec4(palette.Surface)), rounding);
				draw->AddRectFilled(min, ImVec2(min.x + Scaled(kAccentWidth), max.y),
									ImGui::GetColorU32(ToImVec4(AccentOf(palette, toast.Kind))), rounding,
									ImDrawFlags_RoundCornersLeft);

				// The body scrolls past kMaxBodyLines; the close button sits outside it so it
				// never scrolls away.
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Scaled(kAccentWidth));

				if (ImGui::BeginChild("body", ImVec2(wrap, bodyHeight)))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(text.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}

				ImGui::EndChild();

				if (Core::BeginRightClickPopup("##toastMenu"))
				{
					if (ImGui::MenuItem("Copy"))
					{
						if (OnCopied)
						{
							OnCopied(toast.Message);
						}
						else
						{
							ImGui::SetClipboardText(toast.Message.c_str());
						}
					}

					ImGui::EndPopup();
				}

				ImGui::SameLine();

				if (ImGui::SmallButton(ICON_MDI_CLOSE "##close"))
				{
					m_Stack.Close(toast.Id, now);
				}

				hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
			}

			// Reported even when Begin returned false, so a stale hover cannot outlive the window.
			m_Stack.SetHovered(toast.Id, hovered, now);

			ImGui::End();
			ImGui::PopStyleVar();

			bottom = y - Scaled(kSpacing);
		}

		if (const std::optional<ToastClock::time_point> next = m_Stack.NextChange(now); next.has_value())
		{
			Redraw::RequestIn(std::max(*next - now, ToastClock::duration::zero()));
		}
	}
}
