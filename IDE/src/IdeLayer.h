#pragma once

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Gui/DockSpaceHost.h"
#include "Core/Ui/PanelManager.h"

#include <memory>

namespace ide
{
    /// The layer that assembles the application: panel registry, dockspace, and menu bar.
    class IdeLayer final : public core::Layer
    {
    public:
        explicit IdeLayer(std::shared_ptr<core::RingBufferSink> sink);

        void OnAttach() override;
        void OnUpdate(float deltaSeconds) override;
        void OnUIRender() override;

    private:
        void RenderMenuBar();

        std::shared_ptr<core::RingBufferSink> m_sink;
        core::PanelManager m_panels;
        core::DockSpaceHost m_dockSpace;
        core::DockLayout m_layout;
    };
}
