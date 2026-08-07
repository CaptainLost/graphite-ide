#pragma once

#include "Core/App/LayerStack.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <memory>
#include <string>
#include <vector>

namespace core
{
    struct ApplicationSpec
    {
        std::string name = "IDE";
        int width = 1600;
        int height = 900;
    };

    /// Owner of the window, the ImGui layer, and the main loop.
    class Application : NonCopyable
    {
    public:
        explicit Application(ApplicationSpec spec);
        virtual ~Application();

        void Run();
        void Close();

        Layer& PushLayer(std::unique_ptr<Layer> layer);

        [[nodiscard]] Window& GetWindow() { return *m_window; }
        [[nodiscard]] const ApplicationSpec& Spec() const noexcept { return m_spec; }

        static Application& Get();

    protected:
        /// Called after the window and ImGui context are created, before the first frame.
        virtual void OnInit() {}
        virtual void OnShutdown() {}

    private:
        ApplicationSpec m_spec;
        std::unique_ptr<Window> m_window;
        ImGuiRenderer m_gui;
        LayerStack m_layers;
        std::vector<Layer*> m_frameLayers;
        bool m_running = false;
        double m_lastFrameTime = 0.0;

        static Application* s_instance;
    };
}
