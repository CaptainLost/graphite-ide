#pragma once

#include "Solution/SolutionEditor.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace Graphite {

	class SlnxEditor final : public SolutionEditor
	{
	public:
		SlnxEditor(std::filesystem::path path, std::string text);

		Core::Result<void> AddProject(const std::filesystem::path& project,
									  const std::string& folder) override;
		Core::Result<void> RemoveProject(const std::filesystem::path& project) override;
		Core::Result<void> AddFolder(const std::string& folder) override;
		Core::Result<void> RemoveFolder(const std::string& folder) override;
		Core::Result<void> RenameFolder(const std::string& folder,
										const std::string& name) override;
		Core::Result<void> MoveProject(const std::filesystem::path& project,
									   const std::string& folder) override;
		Core::Result<void> Save() override;

	private:
		Core::Result<std::size_t> FindContainer(const std::string& folder) const;

		Core::Result<std::size_t> FindProject(const std::filesystem::path& project) const;

		void InsertChild(std::size_t offset, const std::string& element);

		std::filesystem::path m_Path;
		std::string m_Text;
	};
}