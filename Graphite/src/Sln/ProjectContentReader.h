#pragma once

#include "Core/Base/Result.h"
#include "Sln/SolutionModel.h"

#include <filesystem>

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
}