#pragma once

#include "Core/Base/Color.h"

namespace Core
{
    class Theme
    {
    public:
        /// Dark IDE theme: neutral grays, accent in a cool blue.
        static void ApplyDark();

        /// The line the dark theme outlines panels and separators with. It is named
        /// because it also has to be said outside ImGui: the window's own outer border is
        /// drawn by the system, from a colour handed over before the ImGui context
        /// exists, and it is this one so the window's edge and the edges inside it match.
        /// ApplyDark paints ImGuiCol_Border from the very same value.
        [[nodiscard]] static Color BorderColor();
    };
}
