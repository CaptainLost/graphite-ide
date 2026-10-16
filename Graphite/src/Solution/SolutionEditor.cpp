#include "Solution/SolutionEditor.h"

#include "Core/Base/Base.h"
#include "Core/Base/Uuid.h"
#include "Core/FileSystem/FileSystem.h"
#include "Solution/SlnxEditor.h"

#include <format>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		bool HasFolderNamed(const Folder& parent, const std::string& name)
		{
			for (const Folder& folder : parent.Folders)
			{
				if (Core::CompareNames(folder.Name, name) == 0)
				{
					return true;
				}
			}

			return false;
		}
	}

	std::string NewGuid()
	{
		return std::format("{{{}}}", Core::Uuid::Generate().ToString());
	}

	std::string UniqueFolderName(const Folder& parent, std::string_view base)
	{
		std::string name{base};

		for (int suffixNumber = 2; HasFolderNamed(parent, name); ++suffixNumber)
		{
			name = std::format("{} {}", base, suffixNumber);
		}

		return name;
	}

	bool SolutionEditor::CanEdit(const std::filesystem::path& solution)
	{
		return Core::LoweredExtension(solution) == ".slnx";
	}

	Core::Result<Core::Scope<SolutionEditor>> SolutionEditor::Open(
		const std::filesystem::path& solution)
	{
		std::error_code fileStatusError;

		if (!std::filesystem::is_regular_file(solution, fileStatusError))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", Core::ToUtf8(solution))};
		}

		if (!CanEdit(solution))
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a format this build can edit",
										   Core::ToUtf8(solution))};
		}

		Core::Result<std::string> text = Core::ReadFileBytes(solution);

		if (!text)
		{
			return text.GetError();
		}

		Core::Scope<SolutionEditor> editor = Core::CreateScope<SlnxEditor>(solution, std::move(text).GetValue());

		return editor;
	}
}
