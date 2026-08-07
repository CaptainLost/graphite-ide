#include "IdeLayer.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Version.h"

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

    core::Logger::Get().AddSink(std::make_shared<core::ConsoleSink>());
    core::Logger::Get().AddSink(consoleSink);

    CORE_INFO("IDE {}", core::Version::String());

    IdeApplication app{consoleSink};
    app.Run();

    CORE_INFO("IDE closed");
    return 0;
}
