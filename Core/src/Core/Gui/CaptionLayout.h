#pragma once

namespace core
{
    /// A rectangle in the caption's own coordinates: x grows right, y down, origin at the
    /// bar's top-left. Not ImVec2-based, so this header stays usable without ImGui — the
    /// tests build headless.
    struct CaptionRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    /// Where everything on the right-hand side of the caption goes.
    struct CaptionLayout
    {
        CaptionRect minimize;
        CaptionRect maximize;
        CaptionRect close;
        CaptionRect version;

        /// False when the version label would collide with whatever occupies the left of
        /// the bar. The buttons are never dropped — a window must stay closable.
        bool versionVisible = false;
    };

    /// Packs the window buttons against the right edge in the order Windows uses, and
    /// puts the version label one gap to their left. `contentLeft` is the x the menus
    /// reach; the label gives way to them rather than overlapping.
    [[nodiscard]] CaptionLayout LayOutCaption(float barWidth, float barHeight,
                                              float buttonWidth, float versionWidth,
                                              float versionGap, float contentLeft);
}
