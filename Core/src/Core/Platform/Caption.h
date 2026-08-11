#pragma once

namespace core
{
    /// What the window should do with a point the system is asking about.
    enum class CaptionHit
    {
        Client,
        Caption,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    /// Where the application-drawn title bar is, in physical pixels of the client area.
    /// The window is told this once per frame; the system asks about a point between
    /// frames, so the answer is always one frame old — invisible at frame rate.
    struct CaptionMetrics
    {
        /// Height of the strip that behaves like a title bar. Zero means the application
        /// draws no caption, which leaves the whole window client area.
        float height = 0.0f;

        /// Thickness of the strip along each edge that resizes the window instead.
        float resizeBorder = 8.0f;

        /// The half-open span [dragBegin, dragEnd) of the caption that no widget owns —
        /// between whatever fills the left of the row and the window buttons on its
        /// right. An empty span (dragEnd <= dragBegin) means no part of the row drags the
        /// window. Deliberately geometry and not "what is hovered right now": the instant
        /// this function answers Caption, the system takes the pointer into the
        /// non-client area, the application stops being told where the cursor is, and an
        /// answer derived from the cursor would flip straight back.
        float dragBegin = 0.0f;
        float dragEnd = 0.0f;

        /// Refuses the drag for as long as the application needs the next click for
        /// itself — an open menu, which that click should close. Independent of where the
        /// cursor is, so it survives the pointer leaving the client area.
        bool dragSuppressed = false;
    };

    /// Decides what a point in client coordinates means. Corners win over the two edges
    /// that meet there, and the resize border wins over the caption — a title bar
    /// starting at the very top edge would otherwise leave no way to resize from above.
    /// A maximized window has no edges to drag, so it reports none. Inside the caption
    /// only the free span reads as Caption, so the menus and the window buttons keep
    /// their clicks.
    [[nodiscard]] CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth,
                                      float clientHeight, float x, float y, bool maximized);
}
