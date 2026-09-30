#include "App/GraphiteLayer.h"

#include "App/Version.h"

#include "Settings/AppSettings.h"
#include "Settings/UserDirectories.h"

#include "Core.h"
#include "Core/App/EntryPoint.h"

#include <cstddef>
#include <filesystem>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kConsoleCapacity = 4096;
		constexpr std::size_t kLogFileSize = 5u * 1024u * 1024u;
		constexpr std::size_t kLogFileCount = 3;
	}

	class GraphiteApplication final : public Core::Application
	{
	public:
		GraphiteApplication(Core::Ref<Core::RingBufferSink> sink,
							std::filesystem::path initialFolder)
			: Core::Application(Core::ApplicationSpec{.Name = "Graphite IDE",
													  .Width = 1600,
													  .Height = 900,
													  .CustomTitleBar = true,
													  .Placement = AppSettings::LoadGlobalState().Window,
													  .LayoutFile = UserDirectories::LayoutFile()}),
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
			APP_INFO("Graphite IDE closed");
		}

	private:
		Core::Ref<Core::RingBufferSink> m_Sink;
		std::filesystem::path m_InitialFolder;
	};
}
Core::LogSpec Core::CreateLogSpec()
{
	Core::LogSpec spec;
	spec.FilePath = Graphite::UserDirectories::LogFile();
	spec.MaxFileSize = Graphite::kLogFileSize;
	spec.MaxFiles = Graphite::kLogFileCount;
	spec.ConsoleCapacity = Graphite::kConsoleCapacity;
	return spec;
}

Core::Scope<Core::Application> Core::CreateApplication(Core::ApplicationCommandLineArgs args)
{
	Core::Ref<Core::RingBufferSink> consoleSink = Core::Log::GetConsoleSink();

	APP_INFO("Graphite IDE {}", Graphite::Version::String());

	std::filesystem::path initialFolder;
	if (args.Count > 1)
		initialFolder = args[1];

	return Core::CreateScope<Graphite::GraphiteApplication>(consoleSink, std::move(initialFolder));
}
