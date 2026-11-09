#include "corepch.h"

#include "Files/FileRequests.h"

#include "Files/Trash.h"

#include "Core/FileSystem/AtomicWrite.h"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>

namespace Agent {

	namespace {

		Core::Result<std::filesystem::path> PathArgument(const nlohmann::json& arguments, const char* key)
		{
			const auto found = arguments.find(key);

			if (found == arguments.end() || !found->is_string())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no \"{}\"", key)};
			}

			std::filesystem::path path{found->get<std::string>()};

			if (!path.is_absolute())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("\"{}\" is not an absolute path", key)};
			}

			return path;
		}

		Core::ErrorCode CodeOf(const std::error_code& error)
		{
			if (error == std::errc::no_such_file_or_directory)
			{
				return Core::ErrorCode::NotFound;
			}

			if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
			{
				return Core::ErrorCode::AccessDenied;
			}

			return Core::ErrorCode::IoFailure;
		}

		Core::Error Failed(std::string_view what, const std::filesystem::path& path, const std::error_code& error)
		{
			return Core::Error{CodeOf(error), std::format("cannot {} '{}': {}", what, path.string(), error.message())}; // not a path conversion
		}
	}

	Core::Result<nlohmann::json> WriteFile(const nlohmann::json& arguments, std::string_view payload)
	{
		const Core::Result<std::filesystem::path> path = PathArgument(arguments, "path");

		if (!path)
		{
			return path.GetError();
		}

		const Core::Result<void> written = Core::WriteFileAtomically(path.GetValue(), payload);

		if (!written)
		{
			return written.GetError();
		}

		return nlohmann::json::object();
	}

	Core::Result<nlohmann::json> Rename(const nlohmann::json& arguments)
	{
		const Core::Result<std::filesystem::path> from = PathArgument(arguments, "from");
		const Core::Result<std::filesystem::path> to = PathArgument(arguments, "to");

		if (!from)
		{
			return from.GetError();
		}

		if (!to)
		{
			return to.GetError();
		}

		// renameat2 with RENAME_NOREPLACE refuses atomically where rename(2) would replace; a
		// file system without it (EINVAL) falls back to a check and a rename. Called through
		// syscall(2): musl declares the wrapper only under _GNU_SOURCE.
		constexpr unsigned int kRenameNoReplace = 1; // RENAME_NOREPLACE, <linux/fs.h>

		if (::syscall(SYS_renameat2, AT_FDCWD, from.GetValue().c_str(), AT_FDCWD, to.GetValue().c_str(), kRenameNoReplace) == 0)
		{
			return nlohmann::json::object();
		}

		const int renameError = errno;

		if (renameError == EEXIST)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' already exists", to.GetValue().string())}; // not a path conversion
		}

		if (renameError != EINVAL && renameError != ENOSYS)
		{
			return Failed("rename", from.GetValue(), std::error_code{renameError, std::generic_category()});
		}

		std::error_code error;

		if (std::filesystem::exists(std::filesystem::symlink_status(to.GetValue(), error)))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' already exists", to.GetValue().string())}; // not a path conversion
		}

		std::filesystem::rename(from.GetValue(), to.GetValue(), error);

		if (error)
		{
			return Failed("rename", from.GetValue(), error);
		}

		return nlohmann::json::object();
	}

	Core::Result<nlohmann::json> MakeDirectory(const nlohmann::json& arguments)
	{
		const Core::Result<std::filesystem::path> path = PathArgument(arguments, "path");

		if (!path)
		{
			return path.GetError();
		}

		std::error_code error;
		std::filesystem::create_directories(path.GetValue(), error);

		if (error)
		{
			return Failed("create the folder", path.GetValue(), error);
		}

		return nlohmann::json::object();
	}

	Core::Result<nlohmann::json> CreateFile(const nlohmann::json& arguments)
	{
		const Core::Result<std::filesystem::path> path = PathArgument(arguments, "path");

		if (!path)
		{
			return path.GetError();
		}

		constexpr mode_t kReadWriteForAllLessUmask = 0666;
		const int descriptor = ::open(path.GetValue().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, kReadWriteForAllLessUmask);

		if (descriptor < 0)
		{
			const int openError = errno;

			if (openError == EEXIST)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   std::format("'{}' already exists", path.GetValue().string())}; // not a path conversion
			}

			return Failed("create", path.GetValue(), std::error_code{openError, std::generic_category()});
		}

		::close(descriptor);
		return nlohmann::json::object();
	}

	Core::Result<nlohmann::json> MoveToTrash(const nlohmann::json& arguments)
	{
		const Core::Result<std::filesystem::path> path = PathArgument(arguments, "path");

		if (!path)
		{
			return path.GetError();
		}

		const Core::Result<std::filesystem::path> trashed = MoveToTrash(path.GetValue(), HomeTrash());

		if (!trashed)
		{
			return trashed.GetError();
		}

		return nlohmann::json{{"trashedTo", trashed.GetValue().string()}}; // not a path conversion
	}
}
