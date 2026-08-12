#include "App/GraphiteLayer.h"

#include "App/Version.h"

#include "Core.h"
#include "Core/App/EntryPoint.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kConsoleCapacity = 4096;
		constexpr std::size_t kLogFileSize = 5u * 1024u * 1024u;
		constexpr std::size_t kLogFileCount = 3;

		// The per-user application data directory: a place this program may write without
		// asking, unlike the directory it was installed into. Read as wide characters so a
		// user name outside the machine's ANSI codepage still resolves.
		std::filesystem::path LocalAppData()
		{
			wchar_t* value = nullptr;
			std::size_t size = 0;
			if (_wdupenv_s(&value, &size, L"LOCALAPPDATA") != 0 || !value)
				return {};

			std::filesystem::path path{value};
			std::free(value);
			return path;
		}

		std::filesystem::path LogFilePath()
		{
			std::filesystem::path root = LocalAppData();
			if (root.empty())
			{
				std::error_code code;
				root = std::filesystem::temp_directory_path(code);
				if (code)
					return {};
			}

			return root / "Graphite" / "logs" / "graphite.log";
		}
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
	spec.FilePath = Graphite::LogFilePath();
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
