#include "Core/Base/Base.h"

#include "Sln/SolutionEditor.h"

#include "Core/Base/Uuid.h"

#include "Core/Fs/FileSystem.h"

#include "Sln/SlnxEditor.h"

#include <algorithm>

#include <cctype>

#include <format>

#include <fstream>

#include <sstream>

#include <system_error>

namespace Graphite {

	namespace {
		std::string LowercaseExtension(const std::filesystem::path& path)
		{
			std::string extension = Core::ToUtf8(path.extension());

			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

			return extension;
		}
	}

	std::string NewGuid()
	{
		// Both solution formats wrap the identifier in braces and write it uppercase.
		return std::format("{{{}}}", Core::Uuid::Generate().ToString());
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

		// .sln is readable but not yet writable, and refusing here keeps a caller from
		// discovering that halfway through an edit.
		if (LowercaseExtension(solution) != ".slnx")
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a format this build can edit",
										   Core::ToUtf8(solution))};
		}

		std::ifstream stream{solution, std::ios::binary};

		if (!stream.is_open())
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot open '{}'", Core::ToUtf8(solution))};

		std::ostringstream buffer;
		buffer << stream.rdbuf();

		Core::Scope<SolutionEditor> editor =
			Core::CreateScope<SlnxEditor>(solution, buffer.str());

		return editor;
	}
}