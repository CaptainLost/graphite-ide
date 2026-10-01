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

	// The one copy of graphite.json for the whole process. The application owns it rather than
	// GraphiteLayer because the window placement it holds is needed to open the window, before any
	// layer exists; the layer is handed a reference.
	//
	// A base listed before Core::Application rather than a member: bases are constructed in order,
	// so the placement can be read from it while Core::Application is built, and destroyed in
	// reverse, so it outlives Core::Application's LayerStack. That matters when Run is left by an
	// exception: the LayerStack then detaches GraphiteLayer from its own destructor, and the
	// layer's OnDetach flushes these settings.
	struct ApplicationSettings
	{
		AppSettings Settings;
	};

	class GraphiteApplication final : private ApplicationSettings, public Core::Application
	{
	public:
		GraphiteApplication(Core::Ref<Core::RingBufferSink> sink, std::filesystem::path initialFolder,
							AppSettings settings)
			: ApplicationSettings{std::move(settings)},
			  Core::Application(Core::ApplicationSpec{.Name = "Graphite IDE",
													  .Width = 1600,
													  .Height = 900,
													  .CustomTitleBar = true,
													  .Placement = Settings.GetGlobalState().Window,
													  .LayoutFile = UserDirectories::LayoutFile()}),
			  m_Sink(std::move(sink)),
			  m_InitialFolder(std::move(initialFolder))
		{
		}

	protected:
		void OnInit() override
		{
			PushLayer(Core::CreateScope<GraphiteLayer>(m_Sink, m_InitialFolder, Settings));
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

	return Core::CreateScope<Graphite::GraphiteApplication>(
		consoleSink, std::move(initialFolder),
		Graphite::AppSettings{Graphite::UserDirectories::SettingsFile()});
}
