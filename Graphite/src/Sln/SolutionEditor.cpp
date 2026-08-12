#include "Core/Base/Base.h"

#include "Sln/SolutionEditor.h"

#include "Core/Base/Base.h"

#include "Core/Base/Uuid.h"
#include "Core/Base/Base.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Base/Base.h"

#include "Sln/SlnxEditor.h"

#include "Core/Base/Base.h"

#include <algorithm>
#include "Core/Base/Base.h"

#include <cctype>
#include "Core/Base/Base.h"

#include <format>
#include "Core/Base/Base.h"

#include <fstream>
#include "Core/Base/Base.h"

#include <sstream>
#include "Core/Base/Base.h"

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
		// Both solution formats wrap a GUID in braces and write it uppercase. That is the
		// whole of this function: the identifier itself is a plain version 4 UUID, and
		// Core::Uuid is where it is made and where the RFC is answered to.
		return std::format("{{{}}}", Core::Uuid::Generate().ToString());
	}

	Core::Result<Core::Scope<SolutionEditor>> SolutionEditor::Open(
		const std::filesystem::path& solution)
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(solution, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", solution.string())};
		}

		// .sln is readable but not yet writable; a second plan adds it behind this same
		// call. Refusing here is what keeps a caller from discovering that halfway
		// through an edit.
		if (LowercaseExtension(solution) != ".slnx")
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a format this build can edit",
										   solution.string())};
		}

		std::ifstream stream{solution, std::ios::binary};

		if (!stream.is_open())
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot open '{}'", solution.string())};

		std::ostringstream buffer;
		buffer << stream.rdbuf();

		Core::Scope<SolutionEditor> editor =
			Core::CreateScope<SlnxEditor>(solution, buffer.str());

		return editor;
	}
}