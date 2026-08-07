#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <vector>

namespace core
{
    /// Opis domyslnego rozmieszczenia paneli. Identyfikatory musza odpowiadac Panel::Id().
    struct DockLayout
    {
        std::vector<std::string> left;
        std::vector<std::string> bottom;
        std::vector<std::string> center;

        float leftRatio = 0.20f;
        float bottomRatio = 0.25f;
    };

    /// Niewidoczne okno na cala powierzchnie viewportu, w ktorym zyje dockspace.
    /// Begin nalezy wolac przed renderowaniem paneli, End po nim.
    class DockSpaceHost final : NonCopyable
    {
    public:
        void Begin(const DockLayout& layout);
        void End();

        /// Przy nastepnej klatce odtwarza uklad domyslny.
        void RequestLayoutReset() { m_layoutBuilt = false; }

    private:
        bool m_layoutBuilt = false;
    };
}
