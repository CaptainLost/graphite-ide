#include "corepch.h"

#include "Core/Gui/CaptionLayout.h"

namespace core
{
    CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth,
                                float versionWidth, float versionGap, float contentLeft)
    {
        CaptionLayout layout;

        layout.close = CaptionRect{barWidth - buttonWidth, 0.0f, buttonWidth, barHeight};
        layout.maximize = CaptionRect{layout.close.x - buttonWidth, 0.0f, buttonWidth, barHeight};
        layout.minimize = CaptionRect{layout.maximize.x - buttonWidth, 0.0f, buttonWidth, barHeight};

        if (layout.minimize.x < 0.0f)
        {
            const float shift = -layout.minimize.x;
            layout.minimize.x += shift;
            layout.maximize.x += shift;
            layout.close.x += shift;
        }

        const float versionX = layout.minimize.x - versionGap - versionWidth;
        layout.version = CaptionRect{versionX, 0.0f, versionWidth, barHeight};
        layout.versionVisible = versionX >= contentLeft;

        return layout;
    }
}
