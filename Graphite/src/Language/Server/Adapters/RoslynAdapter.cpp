#include "Language/Server/Adapters/RoslynAdapter.h"

#include "Language/Server/LspProtocol.h"

#include "Core/FileSystem/FileSystem.h"

namespace Graphite {

	void RoslynAdapter::AfterInitialized(const AdapterContext& context, std::vector<OutgoingNotification>& notifications) const
	{
		// Roslyn loads no project until it is told which solution to open.
		if (context.Solution)
		{
			notifications.push_back(
				OutgoingNotification{.Method = "solution/open", .Params = nlohmann::json{{"solution", ToFileUri(*context.Solution)}}});
		}
	}

	std::optional<ProjectProblem> RoslynAdapter::ProjectProblemAt(const AdapterContext& context) const
	{
		if (context.Solution)
		{
			return std::nullopt;
		}

		return ProjectProblem{.Summary = "no solution",
							  .Explanation = "No .sln or .slnx file was found for this folder, so Roslyn reads each C# file on its "
											 "own, without its project's references or the rest of its project."};
	}

	NotificationEffect RoslynAdapter::HandleNotification(const AdapterContext&, std::string_view method, const nlohmann::json&) const
	{
		return method == "workspace/projectInitializationComplete" ? NotificationEffect::ProjectsLoaded : NotificationEffect::None;
	}

	std::optional<std::string> RoslynAdapter::LoadsProjectsFrom(const AdapterContext& context) const
	{
		if (!context.Solution)
		{
			return std::nullopt;
		}

		return Core::ToUtf8(context.Solution->filename());
	}

	bool RoslynAdapter::FollowsSelectedSolution() const
	{
		return true;
	}
}
