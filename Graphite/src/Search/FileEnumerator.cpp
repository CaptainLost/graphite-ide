#include "Search/FileEnumerator.h"

#include "Sln/SolutionModel.h"

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
		std::error_code ec;

		// The non-throwing iterator: a directory that vanishes mid-walk, or one with no
		// permission, is an ordinary event on a tree someone else is also using.
		std::filesystem::recursive_directory_iterator at(
			input.Root, std::filesystem::directory_options::skip_permission_denied, ec);

		if (ec)
		{
			return;
		}

		const std::filesystem::recursive_directory_iterator end;

		while (at != end)
		{
			if (token.IsCancelled())
			{
				return;
			}

			const std::filesystem::directory_entry& entry = *at;

			const bool isDirectory = entry.is_directory(ec);
			const bool excluded = !ec && input.Ignored && input.Ignored(entry.path(), isDirectory);

			if (isDirectory)
			{
				if (excluded)
				{
					// Not increment-then-skip: disable_recursion_pending tells the iterator not
					// to enter this directory at all, which is the whole point of pruning.
					at.disable_recursion_pending();
				}
			}
			else if (!excluded && !ec && entry.is_regular_file(ec))
			{
				onFile(entry.path());
			}

			at.increment(ec);

			if (ec)
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
