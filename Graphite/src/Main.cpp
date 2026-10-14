#include "App/CommandLine.h"
#include "App/GraphiteLayer.h"

#include "App/Version.h"

#include "Settings/AppSettings.h"
#include "Settings/UserDirectories.h"

#include "Core.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/App/EntryPoint.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::size_t kConsoleCapacity = 4096;
		constexpr int kInitialWindowWidth = 1600;
		constexpr int kInitialWindowHeight = 900;
		constexpr std::size_t kLogFileSize = 5u * 1024u * 1024u;
		constexpr std::size_t kLogFileCount = 3;

		const CommandLine& ProcessCommandLine()
		{
			static const CommandLine parsed = ParseCommandLine(ReadProcessCommandLine());
			return parsed;
		}
	}

	// WARNING: This is a base listed before Core::Application, not a member. Bases are constructed
	// in order, so the window placement can be read while Core::Application is built, and they are
	// destroyed in reverse, so the settings outlive the LayerStack that flushes them from
	// GraphiteLayer::OnDetach when Run is left by an exception.
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
													  .Width = kInitialWindowWidth,
													  .Height = kInitialWindowHeight,
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
	const Graphite::CommandLine& commandLine = Graphite::ProcessCommandLine();
	if (commandLine.UserData)
	{
		Graphite::UserDirectories::Redirect(*commandLine.UserData);
	}

	Core::LogSpec spec;
	spec.FilePath = Graphite::UserDirectories::LogFile();
	spec.MaximumFileSize = Graphite::kLogFileSize;
	spec.RotatedFilesKept = Graphite::kLogFileCount;
	spec.ConsoleCapacity = Graphite::kConsoleCapacity;
	return spec;
}

Core::Scope<Core::Application> Core::CreateApplication(Core::ApplicationCommandLineArgs args)
{
	Core::Ref<Core::RingBufferSink> consoleSink = Core::Log::GetConsoleSink();

	APP_INFO("Graphite IDE {}", Graphite::Version::String());

	if (const std::optional<std::filesystem::path> redirectedRoot = Graphite::UserDirectories::RedirectedRoot())
	{
		APP_INFO("User data redirected to '{}'", Core::ToUtf8(*redirectedRoot));
	}

	if (!Graphite::ProcessCommandLine().UserData)
	{
		const std::vector<std::wstring> arguments = Graphite::ReadProcessCommandLine();
		for (const std::wstring& argument : arguments)
		{
			if (argument == L"--user-data")
			{
				APP_WARN("--user-data given without a directory; ignored");
				break;
			}
		}
	}

	static_cast<void>(args);

	return Core::CreateScope<Graphite::GraphiteApplication>(
		consoleSink, Graphite::ProcessCommandLine().Folder,
		Graphite::AppSettings{Graphite::UserDirectories::SettingsFile()});
}
