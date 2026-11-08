#include "Git/Changes/IgnoreEntry.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <system_error>

namespace Graphite {

	std::string IgnoreLineFor(const std::filesystem::path& relative, bool isDirectory)
	{
		std::string line = "/" + Core::ToGenericUtf8(relative);

		if (isDirectory && !line.ends_with('/'))
		{
			line += '/';
		}

		return line;
	}

	std::string WithIgnoreLine(std::string_view existing, std::string_view line)
	{
		std::size_t lineStart = 0;

		while (lineStart <= existing.size())
		{
			const std::size_t lineEnd = existing.find('\n', lineStart);
			std::string_view present = existing.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);

			if (present.ends_with('\r'))
			{
				present.remove_suffix(1);
			}

			if (present == line)
			{
				return std::string{existing};
			}

			if (lineEnd == std::string_view::npos)
			{
				break;
			}

			lineStart = lineEnd + 1;
		}

		const bool usesCrlf = existing.find("\r\n") != std::string_view::npos;
		const std::string_view lineEnding = usesCrlf ? "\r\n" : "\n";

		std::string result{existing};

		if (!result.empty() && !result.ends_with('\n'))
		{
			result += lineEnding;
		}

		result += line;
		result += lineEnding;
		return result;
	}

	Core::Result<void> AddToGitIgnore(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative,
									  bool isDirectory)
	{
		const std::filesystem::path ignoreFile = workingDirectory / ".gitignore";
		std::string existing;

		std::error_code existsError;

		if (std::filesystem::exists(ignoreFile, existsError))
		{
			Core::Result<std::string> read = Core::ReadFileBytes(ignoreFile);

			if (!read)
			{
				return read.GetError();
			}

			existing = std::move(read).GetValue();
		}

		return Core::WriteFileAtomically(ignoreFile, WithIgnoreLine(existing, IgnoreLineFor(relative, isDirectory)));
	}
}
