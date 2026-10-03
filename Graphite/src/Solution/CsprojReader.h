#pragma once

#include "Solution/ProjectContentReader.h"

#include <filesystem>

namespace Graphite {

	class CsprojReader final : public ProjectContentReader
	{
	public:
		bool Handles(const std::filesystem::path& project) const override;
		Core::Result<ProjectContent> Read(
			const std::filesystem::path& project) const override;
	};
}