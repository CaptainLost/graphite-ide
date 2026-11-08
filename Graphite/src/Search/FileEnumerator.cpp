#include "Search/FileEnumerator.h"

#include "Solution/SolutionModel.h"

#include <system_error>

namespace Graphite {

	namespace {

		void WalkFolder(const ContentFolder& folder,
						const std::function<void(const std::filesystem::path&)>& onFile)
		{
			for (const ContentFile& file : folder.Files)
			{
				onFile(file.Path);
			}

			for (const ContentFolder& child : folder.Folders)
			{
				WalkFolder(child, onFile);
			}
		}

		void WalkFolder(const Folder& folder,
						const std::function<void(const std::filesystem::path&)>& onFile)
		{
			for (const Project& project : folder.Projects)
			{
				WalkFolder(project.Content.Root, onFile);
			}

			for (const Folder& child : folder.Folders)
			{
				WalkFolder(child, onFile);
			}
		}
	}

	void EnumerateFiles(const EnumerationInput& input, const Core::CancellationToken& token,
						const std::function<void(const std::filesystem::path&)>& onFile)
	{
		std::error_code errorCode;

		std::filesystem::recursive_directory_iterator entryIterator(
			input.Root, std::filesystem::directory_options::skip_permission_denied, errorCode);

		if (errorCode)
		{
			return;
		}

		const std::filesystem::recursive_directory_iterator endIterator;

		while (entryIterator != endIterator)
		{
			if (token.IsCancelled())
			{
				return;
			}

			const std::filesystem::directory_entry& entry = *entryIterator;

			const bool isDirectory = entry.is_directory(errorCode);
			const bool isExcluded =
				!errorCode && input.Ignored && input.Ignored(entry.path(), isDirectory);

			if (isDirectory)
			{
				if (isExcluded)
				{
					// WARNING: Pruning must use disable_recursion_pending. Skipping the
					// directory's contents after entering it would defeat the pruning.
					entryIterator.disable_recursion_pending();
				}
			}
			else if (!isExcluded && !errorCode && entry.is_regular_file(errorCode))
			{
				onFile(entry.path());
			}

			entryIterator.increment(errorCode);

			// WARNING: On MSVC, when the directory about to be entered vanishes first,
			// increment sets the error and leaves the iterator equal to end() in the same
			// call. Incrementing an iterator already at end() is undefined behaviour, so
			// this return is the only safe exit.
			if (errorCode)
			{
				return;
			}
		}
	}

	void EnumerateProjectFiles(const Solution& solution,
							   const std::function<void(const std::filesystem::path&)>& onFile)
	{
		WalkFolder(solution.Root, onFile);
	}
}
