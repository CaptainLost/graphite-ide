#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Result.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Graphite {

	std::string NewGuid();

	class SolutionEditor
	{
	public:
		virtual ~SolutionEditor() = default;

		SolutionEditor(const SolutionEditor&) = delete;
		SolutionEditor& operator=(const SolutionEditor&) = delete;

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