#pragma once

#include "Core/App/LayerStack.h"
#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <memory>
#include <string>
#include <vector>

namespace Core
{
    /// argc/argv as the client sees them. Handed to CreateApplication by EntryPoint.h,
    /// so nothing below main has to reach for globals to read what it was launched with.
    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        const char* operator[](int index) const
        {
            CORE_ASSERT(index < Count, "Command line argument index out of range");
            return Args[index];
        }
    };

    struct ApplicationSpec
    {
        std::string name = "IDE";
        int width = 1600;
        int height = 900;

        /// Draw the title bar in the application instead of letting the system draw one.
        bool customTitleBar = false;
    };

    /// Owner of the window, the ImGui layer, and the main loop.
    class Application : NonCopyable
    {
    public:
        explicit Application(ApplicationSpec spec);
        virtual ~Application();

        /// Runs the main loop until the window is closed or Close() is called.
        /// Returns an error if the window or the ImGui renderer failed to initialize.
        Result<void> Run();
        void Close();

        Layer& PushLayer(Scope<Layer> layer);

        [[nodiscard]] Window& GetWindow() { return *m_Window; }
        [[nodiscard]] const ApplicationSpec& GetSpec() const noexcept { return m_Spec; }

        static Application& Get();

    protected:
        /// Called after the window and ImGui context are created, before the first frame.
        virtual void OnInit() {}
        virtual void OnShutdown() {}

    private:
        ApplicationSpec m_Spec;
        Scope<Window> m_Window;
        ImGuiRenderer m_Gui;
        LayerStack m_Layers;
        std::vector<Layer*> m_FrameLayers;
        bool m_Running = false;
        double m_LastFrameTime = 0.0;

        static Application* s_Instance;
    };

    /// To be defined by the client. EntryPoint.h owns main and asks for the application
    /// here, so the executable never spells out a startup sequence of its own.
    Scope<Application> CreateApplication(ApplicationCommandLineArgs args);
}
