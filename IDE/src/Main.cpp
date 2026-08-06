#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Version.h"

#include <imgui.h>

#include <memory>

namespace
{
    class DemoLayer final : public core::Layer
    {
    public:
        DemoLayer() : core::Layer("Demo") {}

        void OnUIRender() override { ImGui::ShowDemoWindow(); }
    };

    class IdeApplication final : public core::Application
    {
    public:
        IdeApplication() : core::Application(core::ApplicationSpec{"IDE", 1600, 900}) {}

    protected:
        void OnInit() override { PushLayer(std::make_unique<DemoLayer>()); }
    };
}

int main()
{
    core::Logger::Get().AddSink(std::make_shared<core::ConsoleSink>());
    CORE_INFO("IDE {}", core::Version::String());

    IdeApplication app;
    app.Run();

    CORE_INFO("IDE zamkniete");
    return 0;
}
