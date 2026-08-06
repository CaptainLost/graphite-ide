#pragma once

#include "Core/App/LayerStack.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <memory>
#include <string>

namespace core
{
    struct ApplicationSpec
    {
        std::string name = "IDE";
        int width = 1600;
        int height = 900;
    };

    /// Wlasciciel okna, warstwy ImGui i petli glownej.
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
        /// Wolane po utworzeniu okna i kontekstu ImGui, przed pierwsza klatka.
        virtual void OnInit() {}
        virtual void OnShutdown() {}

    private:
        ApplicationSpec m_spec;
        std::unique_ptr<Window> m_window;
        ImGuiRenderer m_gui;
        LayerStack m_layers;
        bool m_running = false;
        double m_lastFrameTime = 0.0;

        static Application* s_instance;
    };
}
