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

    class GraphiteApplication final : public core::Application
    {
    public:
        GraphiteApplication(std::shared_ptr<core::RingBufferSink> sink,
                             std::filesystem::path initialFolder)
            : core::Application(core::ApplicationSpec{"Graphite IDE", 1600, 900, true}),
              m_sink(std::move(sink)),
              m_initialFolder(std::move(initialFolder))
        {
        }

    protected:
        void OnInit() override
        {
            PushLayer(std::make_unique<graphite::GraphiteLayer>(m_sink, m_initialFolder));
        }

    private:
        std::shared_ptr<core::RingBufferSink> m_sink;
        std::filesystem::path m_initialFolder;
    };
}

int main(int argc, char** argv)
{
    auto consoleSink = std::make_shared<core::RingBufferSink>(kConsoleCapacity);

#ifndef CORE_DIST
    core::Logger::Get().AddSink(std::make_shared<core::ConsoleSink>());
#endif
    core::Logger::Get().AddSink(consoleSink);

    try
    {
        CORE_INFO("Graphite IDE {}", graphite::Version::String());

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
