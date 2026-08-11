#include "TitleBar.h"

#include "Core/Gui/CaptionLayout.h"
#include "Core/Platform/Caption.h"
#include "Core/Platform/Window.h"
#include "Core/Version.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

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

        bool CaptionButton(const char* id, const core::CaptionRect& rect)
        {
            ImGui::SetCursorPos(ImVec2(rect.x, rect.y));
            return ImGui::Button(id, ImVec2(rect.width, rect.height));
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

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        if (CaptionButton(ICON_MDI_WINDOW_MINIMIZE "###CaptionMinimize", layout.minimize))
        {
            window.Minimize();
        }

        const char* middle = window.IsMaximized()
                                 ? ICON_MDI_WINDOW_RESTORE "###CaptionMaximize"
                                 : ICON_MDI_WINDOW_MAXIMIZE "###CaptionMaximize";

        if (CaptionButton(middle, layout.maximize))
        {
            window.ToggleMaximize();
        }

        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCloseHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCloseHovered);

        // Through the window's close flag rather than Application::Close(), for the same
        // reason the File > Exit item does: it is the only path that asks the layers
        // whether anything is unsaved.
        if (CaptionButton(ICON_MDI_CLOSE "###CaptionClose", layout.close))
        {
            window.RequestClose();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // What is left of the row after the menus and these buttons is what the system
        // may treat as a title bar. Asking ImGui whether anything is hovered, or a popup
        // is open, is what keeps a click on a menu — or on one of the buttons above —
        // from being swallowed by the drag. Deliberately not IsAnyItemActive(): that is
        // g.ActiveId != 0 anywhere in the whole application, and ActiveId persists across
        // frames for a focused InputText — the explorer's two filter boxes and the
        // rename fields are exactly that. Leaving that check in place would make the
        // caption undraggable, and double-click-to-maximize dead, for as long as one of
        // those stayed focused, long after the click that focused it. IsAnyItemHovered()
        // resets every NewFrame and already covers the moment a click actually lands on a
        // button or a menu label, which is all this needs.
        //
        // GetMousePos() is in screen space (it has to be, viewports can sit anywhere on
        // the desktop), while kCaptionHeight is a row-local measure, the same space
        // SetCursorPos above places things in. GetWindowPos() converts between the two:
        // the dockspace host is positioned at the main viewport's WorkPos with no title
        // bar and no padding, so its window-space origin *is* the row's top-left. Using
        // the viewport's own Pos here would be wrong the moment a bottom-docked bar (the
        // status bar) trims WorkPos away from Pos, or if this ever ran on a monitor whose
        // origin is not (0, 0) — GetWindowPos() sidesteps both by asking the window that
        // actually owns this coordinate space rather than assuming it lines up with the
        // viewport.
        const float localMouseY = ImGui::GetMousePos().y - ImGui::GetWindowPos().y;

        core::CaptionMetrics metrics;
        metrics.height = kCaptionHeight;
        metrics.cursorOverDragArea =
            ImGui::IsMousePosValid() && localMouseY < kCaptionHeight &&
            !ImGui::IsAnyItemHovered() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

        window.SetCaptionMetrics(metrics);
    }
}
