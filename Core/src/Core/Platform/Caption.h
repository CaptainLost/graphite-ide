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

        /// Whether the cursor currently stands over a part of the caption no widget owns.
        /// False while a menu or a window button is under it, so those keep their clicks.
        /// This describes the cursor, and Classify is only ever asked about the cursor.
        bool cursorOverDragArea = false;
    };

    /// Decides what a point in client coordinates means. Corners win over the two edges
    /// that meet there, and the resize border wins over the caption — a title bar
    /// starting at the very top edge would otherwise leave no way to resize from above.
    /// A maximized window has no edges to drag, so it reports none.
    [[nodiscard]] CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth,
                                      float clientHeight, float x, float y, bool maximized);
}
