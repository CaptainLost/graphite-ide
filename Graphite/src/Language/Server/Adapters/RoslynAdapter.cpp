#include "Language/Server/Adapters/RoslynAdapter.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <format>
#include <system_error>

namespace Graphite {

	namespace {

		bool IsProjectFile(const std::filesystem::directory_entry& entry)
		{
			std::error_code kindError;

			return entry.is_regular_file(kindError) && Core::LoweredExtension(entry.path()) == ".csproj";
		}

		bool ComesFirstByName(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::Lowered(Core::ToUtf8(left.filename())) < Core::Lowered(Core::ToUtf8(right.filename()));
		}

		nlohmann::json FileUrisOf(const std::vector<std::filesystem::path>& projects)
		{
			nlohmann::json uris = nlohmann::json::array();

			for (const std::filesystem::path& project : projects)
			{
				uris.push_back(ToFileUri(project));
			}

			return uris;
		}
	}

	void RoslynAdapter::AfterInitialized(const AdapterContext& context, std::vector<OutgoingNotification>& notifications) const
	{
		// Roslyn loads no project until it is told which solution or which projects to open.
		if (context.Solution)
		{
			notifications.push_back(
				OutgoingNotification{.Method = "solution/open", .Params = nlohmann::json{{"solution", ToFileUri(*context.Solution)}}});
			return;
		}

		if (!context.Projects.empty())
		{
			notifications.push_back(
				OutgoingNotification{.Method = "project/open", .Params = nlohmann::json{{"projects", FileUrisOf(context.Projects)}}});
		}
	}

	std::vector<std::filesystem::path> RoslynAdapter::ProjectsAt(const AdapterContext& context) const
	{
		std::vector<std::filesystem::path> projects;
		std::error_code listError;
		std::filesystem::directory_iterator entries{context.Root, listError};

		for (; !listError && entries != std::filesystem::directory_iterator{}; entries.increment(listError))
		{
			if (IsProjectFile(*entries))
			{
				projects.push_back(entries->path());
			}
		}

		std::sort(projects.begin(), projects.end(), ComesFirstByName);

		return projects;
	}

	std::optional<ProjectProblem> RoslynAdapter::ProjectProblemAt(const AdapterContext& context) const
	{
		if (context.Solution || !context.Projects.empty())
		{
			return std::nullopt;
		}

		return ProjectProblem{.Summary = "no project",
							  .Explanation = "No .sln, .slnx or .csproj file was found for this folder, so Roslyn reads each C# file "
											 "on its own, without its project's references or the rest of its project."};
	}

	NotificationEffect RoslynAdapter::HandleNotification(const AdapterContext&, std::string_view method, const nlohmann::json&) const
	{
		return method == "workspace/projectInitializationComplete" ? NotificationEffect::ProjectsLoaded : NotificationEffect::None;
	}

	std::optional<std::string> RoslynAdapter::LoadsProjectsFrom(const AdapterContext& context) const
	{
		if (context.Solution)
		{
			return Core::ToUtf8(context.Solution->filename());
		}

		if (context.Projects.size() == 1)
		{
			return Core::ToUtf8(context.Projects.front().filename());
		}

		if (!context.Projects.empty())
		{
			return std::format("{} projects", context.Projects.size());
		}

		return std::nullopt;
	}

	bool RoslynAdapter::FollowsSelectedSolution() const
	{
		return true;
	}
}
