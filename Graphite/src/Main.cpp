#include "GraphiteLayer.h"

#include "Version.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"

#include <exception>
#include <filesystem>
#include <memory>

namespace
{
    constexpr std::size_t kConsoleCapacity = 4096;

    class GraphiteApplication final : public Core::Application
    {
    public:
        GraphiteApplication(std::shared_ptr<Core::RingBufferSink> sink,
                             std::filesystem::path initialFolder)
            : Core::Application(Core::ApplicationSpec{"Graphite IDE", 1600, 900, true}),
              m_sink(std::move(sink)),
              m_initialFolder(std::move(initialFolder))
        {
        }

    protected:
        void OnInit() override
        {
            PushLayer(std::make_unique<Graphite::GraphiteLayer>(m_sink, m_initialFolder));
        }

    private:
        std::shared_ptr<Core::RingBufferSink> m_sink;
        std::filesystem::path m_initialFolder;
    };
}

int main(int argc, char** argv)
{
    auto consoleSink = std::make_shared<Core::RingBufferSink>(kConsoleCapacity);

#ifndef CORE_DIST
    Core::Logger::Get().AddSink(std::make_shared<Core::ConsoleSink>());
#endif
    Core::Logger::Get().AddSink(consoleSink);

    try
    {
        CORE_INFO("Graphite IDE {}", Graphite::Version::String());

        std::filesystem::path initialFolder;
        if (argc > 1)
            initialFolder = argv[1];

        GraphiteApplication app{consoleSink, initialFolder};
        if (const auto result = app.Run(); !result)
        {
            CORE_ERROR("IDE terminated: {}", result.GetError().message);
            return 1;
        }

        CORE_INFO("Graphite IDE closed");
        return 0;
    }
    catch (const std::exception& e)
    {
        CORE_ERROR("Unhandled exception: {}", e.what());
        return 1;
    }
    catch (...)
    {
        CORE_ERROR("Unhandled unknown exception");
        return 1;
    }
}
