#pragma once

namespace core
{
    class Window;
}

namespace graphite
{
    /// Height of the row that carries the menus, the version and the window buttons.
    /// 32 px is what Windows itself uses, so the buttons keep their familiar hit size.
    inline constexpr float kCaptionHeight = 32.0f;

    /// The right-hand half of the menu bar: the dimmed product name and the three window
    /// buttons. Drawn inside the menu bar the dockspace host already opens, after the
    /// last menu, and also tells the window which part of the row drags it.
    class TitleBar
    {
    public:
        void Render(core::Window& window);
    };
}
