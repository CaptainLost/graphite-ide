#include "corepch.h"

#include "Core/ImGui/DetachedWindows.h"

#include "Core/Base/Base.h"
#include "Core/Base/Log.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/WindowFrame.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {

	namespace {
		constexpr float kButtonWidth = 42.0f;
		constexpr float kGlyphSize = 14.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kFullyHovered = 1.0f;
		constexpr float kIconGreyShareOfDisabled = 0.21f;
		constexpr int kButtonCount = 3;
		constexpr float kWholePixel = 1.0f;
		constexpr ImU32 kCloseHoveredFill = IM_COL32(196, 43, 28, 255);
		constexpr ImU32 kCloseHoveredGlyph = IM_COL32(255, 255, 255, 255);

		constexpr unsigned char kPrivateUseLeadLow = 0xEE;
		constexpr unsigned char kPrivateUseLeadHigh = 0xEF;
		constexpr int kPrivateUseBytes = 3;

		void (*g_BackendCreateWindow)(ImGuiViewport*) = nullptr;
		void (*g_BackendDestroyWindow)(ImGuiViewport*) = nullptr;
		void (*g_BackendSetWindowTitle)(ImGuiViewport*, const char*) = nullptr;

		std::unordered_map<ImGuiID, Scope<WindowFrame>> g_Frames;

		std::vector<ImGuiID> g_PendingMinimizes;
		std::unordered_map<ImGuiID, ImRect> g_RestoreRects;

		// The backend cuts a position and a size to whole pixels when it hands them to the system.
		bool WithinAPixel(ImVec2 system, ImVec2 own)
		{
			return std::fabs(system.x - own.x) < kWholePixel && std::fabs(system.y - own.y) < kWholePixel;
		}

		bool FillsWorkArea(const ImGuiViewportP& viewport, const ImGuiWindow& window)
		{
			const ImVector<ImGuiPlatformMonitor>& monitors = ImGui::GetPlatformIO().Monitors;

			if (viewport.PlatformMonitor < 0 || viewport.PlatformMonitor >= monitors.Size)
			{
				return false;
			}

			const ImGuiPlatformMonitor& monitor = monitors[viewport.PlatformMonitor];

			return window.Pos.x == monitor.WorkPos.x && window.Pos.y == monitor.WorkPos.y && window.Size.x == monitor.WorkSize.x &&
				   window.Size.y == monitor.WorkSize.y;
		}

		void ToggleMaximized(ImGuiViewportP& viewport)
		{
			ImGuiWindow& window = *viewport.Window;

			if (viewport.PlatformMonitor < 0 || viewport.PlatformMonitor >= ImGui::GetPlatformIO().Monitors.Size)
			{
				return;
			}

			if (FillsWorkArea(viewport, window))
			{
				if (const auto restore = g_RestoreRects.find(viewport.ID); restore != g_RestoreRects.end())
				{
					ImGui::SetWindowPos(&window, restore->second.Min, ImGuiCond_Always);
					ImGui::SetWindowSize(&window, restore->second.GetSize(), ImGuiCond_Always);
					g_RestoreRects.erase(restore);
				}

				return;
			}

			const ImGuiPlatformMonitor& monitor = ImGui::GetPlatformIO().Monitors[viewport.PlatformMonitor];

			g_RestoreRects[viewport.ID] = ImRect(window.Pos, ImVec2(window.Pos.x + window.Size.x, window.Pos.y + window.Size.y));
			ImGui::SetWindowPos(&window, monitor.WorkPos, ImGuiCond_Always);
			ImGui::SetWindowSize(&window, monitor.WorkSize, ImGuiCond_Always);
		}

		std::string WithoutIconGlyphs(const char* title)
		{
			std::string text;

			for (const char* cursor = title; *cursor != '\0';)
			{
				const unsigned char lead = static_cast<unsigned char>(*cursor);

				if (lead == kPrivateUseLeadLow || lead == kPrivateUseLeadHigh)
				{
					cursor += kPrivateUseBytes;
					continue;
				}

				text.push_back(*cursor++);
			}

			const std::size_t firstLetter = text.find_first_not_of(' ');

			return firstLetter == std::string::npos ? std::string{} : text.substr(firstLetter);
		}

		void CreateFramedWindow(ImGuiViewport* viewport)
		{
			g_BackendCreateWindow(viewport);

			const bool isShortLived = (viewport->Flags & (ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_TopMost)) != 0;

			if (viewport->PlatformHandle == nullptr || isShortLived)
			{
				return;
			}

			Result<Scope<WindowFrame>> frame = WindowFrame::HideCaptionOf(viewport->PlatformHandle, Theme::GetBorderColor(), FrameBorder::Flush);

			if (!frame)
			{
				CORE_WARN("A detached window keeps the system title bar: {}", frame.GetError().Message);
				return;
			}

			g_Frames[viewport->ID] = std::move(frame).GetValue();
		}

		void DestroyFramedWindow(ImGuiViewport* viewport)
		{
			g_Frames.erase(viewport->ID);

			g_BackendDestroyWindow(viewport);
		}

		void SetWindowTitleWithoutIcons(ImGuiViewport* viewport, const char* title)
		{
			g_BackendSetWindowTitle(viewport, WithoutIconGlyphs(title).c_str());
		}

		ImU32 GlyphColour()
		{
			const ImVec4* colours = ImGui::GetStyle().Colors;

			return ImGui::GetColorU32(ImLerp(colours[ImGuiCol_Text], colours[ImGuiCol_TextDisabled], kIconGreyShareOfDisabled));
		}

		struct ButtonLook
		{
			ImU32 HoveredFill = 0;
			ImU32 Glyph = 0;
			ImU32 HoveredGlyph = 0;
		};

		bool DrawButton(const char* id, const char* glyph, const ImRect& rect, const ButtonLook& look)
		{
			ImGui::SetCursorScreenPos(rect.Min);

			const bool pressed = ImGui::InvisibleButton(id, rect.GetSize());
			const bool held = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();
			const float hoverAmount = Animation::HoverAmount(ImGui::GetItemID(), hovered);

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			if (hoverAmount > 0.0f || held)
			{
				ImVec4 fill = ImGui::ColorConvertU32ToFloat4(look.HoveredFill);
				fill.w *= held ? kFullyHovered : hoverAmount;
				drawList->AddRectFilled(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(fill));
			}

			const float glyphHover = held ? kFullyHovered : hoverAmount;
			const ImVec4 glyphColour = Animation::Blend(ImGui::ColorConvertU32ToFloat4(look.Glyph),
														ImGui::ColorConvertU32ToFloat4(look.HoveredGlyph), glyphHover);

			PushInterfaceFont(kGlyphSize);

			const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
			const ImVec2 glyphPosition(std::floor(rect.Min.x + (rect.GetWidth() - glyphSize.x) * kHalf),
									   std::floor(rect.Min.y + (rect.GetHeight() - glyphSize.y) * kHalf));

			drawList->AddText(glyphPosition, ImGui::ColorConvertFloat4ToU32(glyphColour), glyph);

			ImGui::PopFont();

			return pressed;
		}
	}

	void InstallDetachedWindowHooks(ImGuiPlatformIO& platformIo)
	{
		g_BackendCreateWindow = platformIo.Platform_CreateWindow;
		g_BackendDestroyWindow = platformIo.Platform_DestroyWindow;
		g_BackendSetWindowTitle = platformIo.Platform_SetWindowTitle;

		platformIo.Platform_CreateWindow = &CreateFramedWindow;
		platformIo.Platform_DestroyWindow = &DestroyFramedWindow;
		platformIo.Platform_SetWindowTitle = &SetWindowTitleWithoutIcons;
	}

	void SetDetachedWindowBorderColor(const Color& color)
	{
		for (auto& [id, frame] : g_Frames)
		{
			frame->SetBorderColor(color);
		}
	}

	namespace {
		struct TopBar
		{
			ImVec2 Position;
			float Right = 0.0f;
			float Height = 0.0f;
		};

		std::optional<TopBar> TopBarOf(ImGuiWindow& window)
		{
			if (ImGuiDockNode* const node = window.DockNodeAsHost; node != nullptr)
			{
				float height = 0.0f;

				if (node->TabBar != nullptr && !node->IsHiddenTabBar() && node->TabBar->CurrFrameVisible == ImGui::GetFrameCount())
				{
					height = node->TabBar->BarRect.GetHeight();
				}
				else
				{
					for (const ImGuiWindow* docked : node->Windows)
					{
						height = ImMax(height, docked->TitleBarHeight);
					}
				}

				return TopBar{.Position = node->Pos, .Right = node->Pos.x + node->Size.x, .Height = height};
			}

			if ((window.Flags & ImGuiWindowFlags_NoTitleBar) != 0)
			{
				return std::nullopt;
			}

			return TopBar{.Position = window.Pos, .Right = window.Pos.x + window.Size.x, .Height = window.TitleBarHeight};
		}

		void DrawButtonsOver(ImGuiViewportP& viewport, const TopBar& bar)
		{
			const float width = Scaled(kButtonWidth);
			const ImVec2 size(width * kButtonCount, bar.Height);
			const ImVec2 position(bar.Right - size.x, bar.Position.y);

			const std::string name = "##DetachedWindowButtons" + std::to_string(viewport.ID);

			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
												ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
												ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

			ImGui::SetNextWindowViewport(viewport.ID);
			ImGui::SetNextWindowPos(position);
			ImGui::SetNextWindowSize(size);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			const bool shown = ImGui::Begin(name.c_str(), nullptr, kFlags);
			ImGui::PopStyleVar(3);

			if (shown)
			{
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

				const float border = ImGui::GetStyle().WindowBorderSize;
				const float hairline = 1.0f;

				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(position.x, position.y + border),
														  ImVec2(position.x + size.x - border, position.y + size.y - hairline),
														  ImGui::GetColorU32(ImGuiCol_TitleBgActive));

				const ImGuiStyle& style = ImGui::GetStyle();
				const ImVec4 hovered = style.Colors[ImGuiCol_ButtonHovered];

				const ButtonLook plain{.HoveredFill = ImGui::ColorConvertFloat4ToU32(hovered), .Glyph = GlyphColour(), .HoveredGlyph = GlyphColour()};
				const ButtonLook close{.HoveredFill = kCloseHoveredFill, .Glyph = GlyphColour(), .HoveredGlyph = kCloseHoveredGlyph};

				const auto slot = [&](int index) {
					return ImRect(ImVec2(position.x + width * index, position.y), ImVec2(position.x + width * (index + 1), position.y + bar.Height));
				};

				if (DrawButton("##DetachedMinimize", ICON_LC_MINUS, slot(0), plain))
				{
					g_PendingMinimizes.push_back(viewport.ID);
				}

				const bool maximized = FillsWorkArea(viewport, *viewport.Window);

				if (DrawButton("##DetachedMaximize", maximized ? ICON_LC_COPY : ICON_LC_SQUARE, slot(1), plain))
				{
					ToggleMaximized(viewport);
				}

				if (DrawButton("##DetachedClose", ICON_LC_X, slot(2), close))
				{
					viewport.PlatformRequestClose = true;
				}
			}

			ImGui::End();
		}
	}

	void DrawDetachedWindowButtons()
	{
		const ImGuiViewport* const main = ImGui::GetMainViewport();

		for (ImGuiViewport* platformViewport : ImGui::GetPlatformIO().Viewports)
		{
			ImGuiViewportP* const viewport = static_cast<ImGuiViewportP*>(platformViewport);
			ImGuiWindow* const owner = viewport->Window;

			if (viewport == main || owner == nullptr || owner->Viewport != viewport || !owner->WasActive || !g_Frames.contains(viewport->ID))
			{
				continue;
			}

			const std::optional<TopBar> bar = TopBarOf(*owner);

			if (bar.has_value() && bar->Height > 0.0f)
			{
				DrawButtonsOver(*viewport, *bar);
			}
		}
	}

	bool AskDetachedWindowsForTheirSystemRects(ImGuiPlatformIO& platformIo)
	{
		if (platformIo.Platform_GetWindowPos == nullptr || platformIo.Platform_GetWindowSize == nullptr)
		{
			return false;
		}

		const ImGuiViewport* const main = ImGui::GetMainViewport();
		bool askedForAny = false;

		for (ImGuiViewport* const viewport : platformIo.Viewports)
		{
			ImGuiViewportP& detached = *static_cast<ImGuiViewportP*>(viewport);
			const bool isMinimized = (viewport->Flags & ImGuiViewportFlags_IsMinimized) != 0;

			if (viewport == main || !detached.PlatformWindowCreated || isMinimized)
			{
				continue;
			}

			if (!WithinAPixel(platformIo.Platform_GetWindowPos(viewport), viewport->Pos))
			{
				detached.PlatformRequestMove = true;
				askedForAny = true;
			}

			if (!WithinAPixel(platformIo.Platform_GetWindowSize(viewport), viewport->Size))
			{
				detached.PlatformRequestResize = true;
				askedForAny = true;
			}
		}

		return askedForAny;
	}

	void ApplyPendingDetachedWindowActions()
	{
		const std::vector<ImGuiID> minimizes = std::move(g_PendingMinimizes);
		g_PendingMinimizes.clear();

		for (const ImGuiID id : minimizes)
		{
			const ImGuiViewport* const viewport = ImGui::FindViewportByID(id);

			if (viewport != nullptr && viewport->PlatformHandle != nullptr)
			{
				glfwIconifyWindow(static_cast<GLFWwindow*>(viewport->PlatformHandle));
			}
		}
	}
}
