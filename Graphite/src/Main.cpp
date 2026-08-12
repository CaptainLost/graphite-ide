#include "GraphiteLayer.h"

#include "Version.h"

#include "Core.h"
#include "Core/App/EntryPoint.h"

#include <cstddef>
#include <filesystem>
#include <utility>

namespace Graphite
{
    namespace
    {
        constexpr std::size_t kConsoleCapacity = 4096;
    }

    class GraphiteApplication final : public Core::Application
    {
    public:
        GraphiteApplication(Core::Ref<Core::RingBufferSink> sink,
                            std::filesystem::path initialFolder)
            : Core::Application(Core::ApplicationSpec{"Graphite IDE", 1600, 900, true}),
              m_Sink(std::move(sink)),
              m_InitialFolder(std::move(initialFolder))
        {
        }

    protected:
        void OnInit() override
        {
            PushLayer(Core::CreateScope<GraphiteLayer>(m_Sink, m_InitialFolder));
        }

        void OnShutdown() override
        {
            CORE_INFO("Graphite IDE closed");
        }

    private:
        Core::Ref<Core::RingBufferSink> m_Sink;
        std::filesystem::path m_InitialFolder;
    };
}

/// Defined here because EntryPoint.h owns main: everything specific to this executable —
/// which sinks the log writes to, what the window is called, which folder it opens —
/// belongs to the client, and the engine only asks for the application it should run.
Core::Scope<Core::Application> Core::CreateApplication(Core::ApplicationCommandLineArgs args)
{
    auto consoleSink = Core::CreateRef<Core::RingBufferSink>(Graphite::kConsoleCapacity);

#ifndef CORE_DIST
    Core::Logger::Get().AddSink(Core::CreateRef<Core::ConsoleSink>());
#endif
    Core::Logger::Get().AddSink(consoleSink);

    CORE_INFO("Graphite IDE {}", Graphite::Version::String());

    std::filesystem::path initialFolder;
    if (args.Count > 1)
        initialFolder = args[1];

    return Core::CreateScope<Graphite::GraphiteApplication>(consoleSink, std::move(initialFolder));
}
