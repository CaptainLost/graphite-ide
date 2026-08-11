#include "TitleBar.h"

#include "Core/Gui/CaptionLayout.h"
#include "Core/Platform/Caption.h"
#include "Core/Platform/Window.h"
#include "Core/Version.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <cmath>
#include <format>

namespace graphite
{
    namespace
    {
        constexpr float kButtonWidth = 46.0f;
        constexpr float kVersionGap = 12.0f;

        /// The one colour the system caption has that the theme does not: a close button
        /// that goes red under the pointer. Everything else follows the theme.
        constexpr ImVec4 kCloseHovered{0.75f, 0.16f, 0.16f, 1.0f};

        /// A window button drawn rather than themed: an invisible item for the interaction,
        /// one rectangle on the draw list for the background, and the glyph centred in it.
        ///
        /// `systemOwnsPointer` is what an ImGui::Button cannot know. Along the top and the
        /// right edges the resize border runs straight across all three buttons, and there
        /// the pointer belongs to Windows: the hit test answers a resize corner, the press
        /// arrives as non-client input the application never sees, and the native caption
        /// highlights nothing. A Button would still light up under a cursor it will never
        /// be clicked by. Disabled is precisely the state wanted for that strip — ImGui
        /// reports the item neither hovered nor active, refuses it the press, and releases
        /// the active id if it happened to hold one. The dimming BeginDisabled brings with
        /// it costs nothing here, because every pixel below is drawn with an explicit
        /// colour rather than through the style.
        bool CaptionButton(const char* id, const char* glyph, const core::CaptionRect& rect,
                           ImU32 hoveredColour, ImU32 heldColour, bool systemOwnsPointer)
        {
            ImGui::SetCursorPos(ImVec2(rect.x, rect.y));

            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + rect.width, min.y + rect.height);

            ImGui::BeginDisabled(systemOwnsPointer);
            const bool pressed = ImGui::InvisibleButton(id, ImVec2(rect.width, rect.height));
            const bool held = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();

            ImDrawList* draw = ImGui::GetWindowDrawList();

            if (held || hovered)
            {
                draw->AddRectFilled(min, max, held ? heldColour : hoveredColour);
            }

            // Floored, not rounded: CalcTextSize rounds up, so a centred glyph placed on a
            // half pixel would be resampled and read blurrier than the menu text beside it.
            const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
            const ImVec2 at(std::floor(min.x + (rect.width - glyphSize.x) * 0.5f),
                            std::floor(min.y + (rect.height - glyphSize.y) * 0.5f));

            draw->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), glyph);

            return pressed;
        }
    }

    void TitleBar::Render(core::Window& window)
    {
        if (!window.HasCustomTitleBar())
        {
            return;
        }

        const std::string version = std::format("Graphite {}", core::Version::String());
        const float versionWidth = ImGui::CalcTextSize(version.c_str()).x;

        // Where the menus stopped: the layout has to know what it may not cover.
        const float contentLeft = ImGui::GetCursorPosX();

        // BeginMenuBar() does not open a child window — it repositions the cursor of the
        // dockspace host window that is already current, so GetWindowWidth() here answers
        // for that host window (which DockSpaceHost::Begin sizes to the whole work area),
        // not for some narrower menu-bar-only window. That is exactly the width the row
        // spans, so it is what the button layout needs.
        const float barWidth = ImGui::GetWindowWidth();

        const core::CaptionLayout layout = core::LayOutCaption(
            barWidth, kCaptionHeight, kButtonWidth, versionWidth, kVersionGap, contentLeft);

        if (layout.versionVisible)
        {
            // Not a centring formula on y — BeginMenuBar() already called
            // AlignTextToFramePadding(), and that base offset survives across SetCursorPos
            // (TextEx draws at CursorPos.y + CurrLineTextBaseOffset, which SetCursorPos
            // does not touch). It was computed from the same enlarged FramePadding this
            // row uses, so it already centres a text line in kCaptionHeight; adding a
            // second centring offset on top of it would double-count and push the label
            // low.
            ImGui::SetCursorPos(ImVec2(layout.version.x, 0.0f));
            ImGui::TextDisabled("%s", version.c_str());
        }

        // What is left of the row between the menus and the window buttons is what the
        // system may treat as a title bar — reported as the span itself, not as "the
        // cursor is over it". The difference is not stylistic: the moment the hit test
        // answers HTCAPTION the pointer belongs to the non-client area, GLFW gets
        // WM_MOUSELEAVE and the ImGui backend pushes an invalid mouse position, so any
        // answer computed from GetMousePos() or IsAnyItemHovered() would collapse the
        // frame after it was given and flip the hit test back and forth under a cursor
        // that never moved.
        //
        // These x values are window-local, the same space SetCursorPos below uses, while
        // the hit test asks in client coordinates. They coincide because the dockspace
        // host sits at the main viewport's WorkPos, which the status bar (docked at the
        // bottom) leaves equal to the viewport's Pos, i.e. the client's top-left. A bar
        // docked along the top or the left would break that — as it would already break
        // measuring the caption's height from the client top — and both would then need
        // the offset between GetWindowPos() and the viewport's Pos added here.
        core::CaptionMetrics metrics;
        metrics.height = kCaptionHeight;
        metrics.dragBegin = contentLeft;
        metrics.dragEnd = layout.minimize.x;

        // An open menu is the one thing a free-caption click has to reach the application
        // for: it should close the menu, and a drag would swallow it. Unlike a hover test
        // this says nothing about where the cursor is, so it stays true while the pointer
        // is off in the non-client area.
        metrics.dragSuppressed = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

        // The buttons then ask that very same function what the pointer is over, so the
        // highlight and the hit test cannot disagree: anything Classify does not call
        // Client is the system's this frame. An invalid mouse position — what the backend
        // reports while the pointer is away in the non-client area — lands far outside the
        // client rectangle and reads as an edge, which is the wanted answer anyway: a
        // pointer the application cannot see hovers nothing.
        int clientWidth = 0;
        int clientHeight = 0;
        window.FramebufferSize(clientWidth, clientHeight);

        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 pointer = ImGui::GetMousePos();

        const bool systemOwnsPointer =
            core::Classify(metrics, static_cast<float>(clientWidth),
                           static_cast<float>(clientHeight), pointer.x - origin.x,
                           pointer.y - origin.y,
                           window.IsMaximized()) != core::CaptionHit::Client;

        const ImU32 hovered = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
        const ImU32 held = ImGui::GetColorU32(ImGuiCol_ButtonActive);

        if (CaptionButton("CaptionMinimize", ICON_MDI_WINDOW_MINIMIZE, layout.minimize,
                          hovered, held, systemOwnsPointer))
        {
            window.Minimize();
        }

        const char* middle =
            window.IsMaximized() ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE;

        if (CaptionButton("CaptionMaximize", middle, layout.maximize, hovered, held,
                          systemOwnsPointer))
        {
            window.ToggleMaximize();
        }

        // Through the window's close flag rather than Application::Close(), for the same
        // reason the File > Exit item does: it is the only path that asks the layers
        // whether anything is unsaved.
        const ImU32 closeColour = ImGui::GetColorU32(kCloseHovered);

        if (CaptionButton("CaptionClose", ICON_MDI_CLOSE, layout.close, closeColour,
                          closeColour, systemOwnsPointer))
        {
            window.RequestClose();
        }

        window.SetCaptionMetrics(metrics);
    }
}
