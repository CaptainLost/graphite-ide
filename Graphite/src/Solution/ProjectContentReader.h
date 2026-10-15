#pragma once

#include "Core/Base/Result.h"
#include "Solution/SolutionModel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	class ProjectContentReader
	{
	public:
		virtual ~ProjectContentReader() = default;

		ProjectContentReader(const ProjectContentReader&) = delete;
		ProjectContentReader& operator=(const ProjectContentReader&) = delete;

		virtual bool Handles(const std::filesystem::path& project) const = 0;
		virtual Core::Result<ProjectContent> Read(
			const std::filesystem::path& project) const = 0;

	protected:
		ProjectContentReader() = default;
	};

	void SortContentByName(ContentFolder& folder);

	std::optional<std::string> ReferencedProjectName(std::string_view include);
}