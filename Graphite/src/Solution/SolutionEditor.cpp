#include "Solution/SolutionEditor.h"

#include "Core/Base/Base.h"

#include "Core/Base/Uuid.h"

#include "Core/FileSystem/FileSystem.h"

#include "Solution/SlnxEditor.h"

#include <algorithm>

#include <format>

#include <fstream>

#include <sstream>

#include <system_error>

namespace Graphite {

	std::string NewGuid()
	{
		// Both solution formats wrap the identifier in braces and write it uppercase.
		return std::format("{{{}}}", Core::Uuid::Generate().ToString());
	}

	std::string UniqueFolderName(const Folder& parent, std::string_view base)
	{
		const auto taken = [&parent](const std::string& name) {
			return std::any_of(parent.Folders.begin(), parent.Folders.end(),
							   [&name](const Folder& folder) { return Core::CompareNames(folder.Name, name) == 0; });
		};

		std::string name{base};

		for (int number = 2; taken(name); ++number)
		{
			name = std::format("{} {}", base, number);
		}

		return name;
	}

	bool SolutionEditor::CanEdit(const std::filesystem::path& solution)
	{
		// .sln is readable but not yet writable.
		return Core::LoweredExtension(solution) == ".slnx";
	}

	Core::Result<Core::Scope<SolutionEditor>> SolutionEditor::Open(
		const std::filesystem::path& solution)
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(solution, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", Core::ToUtf8(solution))};
		}

		// Refusing here keeps a caller from discovering that halfway through an edit.
		if (!CanEdit(solution))
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a format this build can edit",
										   Core::ToUtf8(solution))};
		}

		std::ifstream stream{solution, std::ios::binary};

		if (!stream.is_open())
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot open '{}'", Core::ToUtf8(solution))};
		}

		std::ostringstream buffer;
		buffer << stream.rdbuf();

		Core::Scope<SolutionEditor> editor =
			Core::CreateScope<SlnxEditor>(solution, buffer.str());

		return editor;
	}
}