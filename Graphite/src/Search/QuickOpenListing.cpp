#include "Search/QuickOpenListing.h"

#include "Git/GitIgnoreRules.h"
#include "Search/FileEnumerator.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::size_t kHandOverEvery = 2048;

		bool IsInsideGitDirectory(const std::filesystem::path& path)
		{
			for (const std::filesystem::path& part : path)
			{
				if (part == ".git")
				{
					return true;
				}
			}

			return false;
		}

		void ListFromDisk(const std::filesystem::path& root, const Core::CancellationToken& token, QuickOpenListing& listing)
		{
			const GitIgnoreRules ignoreRules{root};

			EnumerationInput input;
			input.Root = root;
			input.Ignored = [&ignoreRules](const std::filesystem::path& path, bool) {
				return IsInsideGitDirectory(path) || ignoreRules.IsIgnored(path);
			};

			std::vector<std::string> found;

			EnumerateFiles(input, token, [&](const std::filesystem::path& file) {
				if (found.size() >= kLargestQuickOpenListing)
				{
					return;
				}

				const std::optional<std::filesystem::path> relative = Core::RelativeTo(file, root);
				found.push_back(Core::ToGenericUtf8(relative ? *relative : file));

				if (found.size() % kHandOverEvery == 0)
				{
					const std::lock_guard<std::mutex> lock(listing.Mutex);
					listing.Paths = found;
					Core::Redraw::Request();
				}
			});

			const std::lock_guard<std::mutex> lock(listing.Mutex);
			listing.Paths = std::move(found);
		}

		void ListThrough(FolderSearch& folder, const std::filesystem::path& root, const Core::CancellationToken& token, QuickOpenListing& listing)
		{
			Core::Result<std::vector<std::string>> files = folder.ListFiles(true, token);

			if (!files)
			{
				if (!token.IsCancelled())
				{
					APP_WARN("Cannot list the files of '{}': {}", Core::ToUtf8(root), files.GetError().Message);
				}

				return;
			}

			std::vector<std::string>& found = files.GetValue();

			if (found.size() > kLargestQuickOpenListing)
			{
				found.resize(kLargestQuickOpenListing);
			}

			const std::lock_guard<std::mutex> lock(listing.Mutex);
			listing.Paths = std::move(found);
		}
	}

	void ListFolderFiles(const std::filesystem::path& root, FolderSearch* folder, const Core::CancellationToken& token, QuickOpenListing& listing)
	{
		if (folder)
		{
			ListThrough(*folder, root, token, listing);
		}
		else
		{
			ListFromDisk(root, token, listing);
		}

		const std::lock_guard<std::mutex> lock(listing.Mutex);
		listing.Done = true;
		Core::Redraw::Request();
	}
}
