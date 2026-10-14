#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Result.h"
#include "Solution/SolutionModel.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Graphite {

	std::string NewGuid();

	std::string UniqueFolderName(const Folder& parent, std::string_view base);

	class SolutionEditor
	{
	public:
		virtual ~SolutionEditor() = default;

		SolutionEditor(const SolutionEditor&) = delete;
		SolutionEditor& operator=(const SolutionEditor&) = delete;

		static bool CanEdit(const std::filesystem::path& solution);

		static Core::Result<Core::Scope<SolutionEditor>> Open(
			const std::filesystem::path& solution);

		virtual Core::Result<void> AddProject(const std::filesystem::path& project,
											  const std::string& folder) = 0;

		virtual Core::Result<void> RemoveProject(
			const std::filesystem::path& project) = 0;

		virtual Core::Result<void> AddFolder(const std::string& folder) = 0;

		virtual Core::Result<void> RemoveFolder(const std::string& folder) = 0;

		virtual Core::Result<void> RenameFolder(const std::string& folder,
												const std::string& name) = 0;

		virtual Core::Result<void> MoveProject(const std::filesystem::path& project,
											   const std::string& folder) = 0;

		virtual Core::Result<void> Save() = 0;

	protected:
		SolutionEditor() = default;
	};
}