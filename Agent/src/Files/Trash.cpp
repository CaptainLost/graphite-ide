#include "corepch.h"

#include "Files/Trash.h"

#include "Core/FileSystem/AtomicWrite.h"

#include <sys/stat.h>

#include <cstdlib>
#include <ctime>
#include <format>
#include <system_error>

namespace Agent {

	namespace {

		bool IsUnreserved(char character)
		{
			const bool isLetter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
			const bool isDigit = character >= '0' && character <= '9';
			return isLetter || isDigit || character == '-' || character == '_' || character == '.' || character == '~' ||
				   character == '/';
		}

		std::string LocalTimeNow()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{};
			::localtime_r(&now, &local);

			char text[32] = {};
			std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
			return text;
		}

		// st_dev of the folder holding path, or empty when it cannot be read.
		std::optional<dev_t> DeviceOf(const std::filesystem::path& path)
		{
			struct stat status{};

			if (::stat(path.c_str(), &status) != 0)
			{
				return std::nullopt;
			}

			return status.st_dev;
		}

		std::string FreeName(const std::filesystem::path& trashRoot, const std::string& name)
		{
			std::error_code ignored;

			for (int attempt = 1;; ++attempt)
			{
				const std::string candidate = attempt == 1 ? name : std::format("{} {}", name, attempt);
				const bool takenInFiles = std::filesystem::exists(trashRoot / "files" / candidate, ignored);
				const bool takenInInfo = std::filesystem::exists(trashRoot / "info" / (candidate + ".trashinfo"), ignored);

				if (!takenInFiles && !takenInInfo)
				{
					return candidate;
				}
			}
		}
	}

	std::filesystem::path HomeTrash()
	{
		if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome != nullptr && dataHome[0] == '/')
		{
			return std::filesystem::path{dataHome} / "Trash";
		}

		const char* home = std::getenv("HOME");
		return std::filesystem::path{home != nullptr ? home : "/"} / ".local" / "share" / "Trash";
	}

	std::string PercentEncodePath(std::string_view path)
	{
		std::string encoded;
		encoded.reserve(path.size());

		for (const char character : path)
		{
			if (IsUnreserved(character))
			{
				encoded.push_back(character);
				continue;
			}

			encoded += std::format("%{:02X}", static_cast<unsigned char>(character));
		}

		return encoded;
	}

	Core::Result<std::filesystem::path> MoveToTrash(const std::filesystem::path& item, const std::filesystem::path& trashRoot)
	{
		std::error_code error;

		if (!std::filesystem::exists(std::filesystem::symlink_status(item, error)))
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("'{}' is not there", item.string())}; // not a path conversion
		}

		std::filesystem::create_directories(trashRoot / "files", error);
		std::filesystem::create_directories(trashRoot / "info", error);

		const std::optional<dev_t> itemDevice = DeviceOf(item.parent_path());
		const std::optional<dev_t> trashDevice = DeviceOf(trashRoot);

		if (!itemDevice || !trashDevice || *itemDevice != *trashDevice)
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not on the home file system, so it cannot go to the trash", item.string())}; // not a path conversion
		}

		const std::string name = FreeName(trashRoot, item.filename().string()); // not a path conversion
		const std::filesystem::path infoFile = trashRoot / "info" / (name + ".trashinfo");
		const std::filesystem::path trashedTo = trashRoot / "files" / name;

		// The note goes first: a crash between the two leaves a note without a file, which the
		// trash ignores, rather than a file nobody can restore.
		const std::string info = std::format("[Trash Info]\nPath={}\nDeletionDate={}\n",
											 PercentEncodePath(std::filesystem::absolute(item).string()), LocalTimeNow()); // not a path conversion

		const Core::Result<void> noted = Core::WriteFileAtomically(infoFile, info);

		if (!noted)
		{
			return noted.GetError();
		}

		std::filesystem::rename(item, trashedTo, error);

		if (error)
		{
			std::filesystem::remove(infoFile, error);
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot move '{}' to the trash: {}", item.string(), error.message())}; // not a path conversion
		}

		return trashedTo;
	}
}
