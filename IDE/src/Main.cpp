#include "IdeLayer.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Version.h"

#include <exception>
#include <memory>

namespace
{
    constexpr std::size_t kConsoleCapacity = 4096;

    class IdeApplication final : public core::Application
    {
    public:
        explicit IdeApplication(std::shared_ptr<core::RingBufferSink> sink)
            : core::Application(core::ApplicationSpec{"IDE", 1600, 900}), m_sink(std::move(sink))
        {
        }

    protected:
        void OnInit() override { PushLayer(std::make_unique<ide::IdeLayer>(m_sink)); }

    private:
        std::shared_ptr<core::RingBufferSink> m_sink;
    };
}

int main()
{
    auto consoleSink = std::make_shared<core::RingBufferSink>(kConsoleCapacity);

#ifndef CORE_DIST
    core::Logger::Get().AddSink(std::make_shared<core::ConsoleSink>());
#endif
    core::Logger::Get().AddSink(consoleSink);

    try
    {
        CORE_INFO("IDE {}", core::Version::String());

        IdeApplication app{consoleSink};
        if (const auto result = app.Run(); !result)
        {
            CORE_ERROR("IDE terminated: {}", result.GetError().message);
            return 1;
        }

        CORE_INFO("IDE closed");
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
