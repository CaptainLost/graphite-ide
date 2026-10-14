#include "Language/Server/ProjectSearch.h"

#include "Language/Server/ServerCatalog.h"
#include "Language/Server/SolutionScope.h"
#include "Search/FileEnumerator.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::array<std::string_view, 5> kSkippedFolders{"node_modules", "bin", "obj", ".git", ".vs"};
		constexpr std::array<std::string_view, 2> kTestFolders{"test", "tests"};

		bool IsOneOf(std::string_view loweredName, std::span<const std::string_view> names)
		{
			for (const std::string_view name : names)
			{
				if (loweredName == name)
				{
					return true;
				}
			}

			return false;
		}

		bool IsSkippedFolder(const std::filesystem::path& directory)
		{
			const std::string name = Core::Lowered(Core::ToUtf8(directory.filename()));

			return name.starts_with('.') || IsOneOf(name, kSkippedFolders);
		}

		bool BelongsToAnotherServer(const std::filesystem::path& directory)
		{
			return IsProjectRoot(directory) || DirectoryHoldsSolution(directory);
		}

		bool LiesInTestFolder(const std::filesystem::path& project, const std::filesystem::path& root)
		{
			for (const std::filesystem::path& folder : project.parent_path().lexically_relative(root))
			{
				if (IsOneOf(Core::Lowered(Core::ToUtf8(folder)), kTestFolders))
				{
					return true;
				}
			}

			return false;
		}

		bool ComesFirstByPath(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::Lowered(Core::ToUtf8(left)) < Core::Lowered(Core::ToUtf8(right));
		}
	}

	ProjectsBelow FindProjectsBelow(const std::filesystem::path& root, std::string_view extension, std::size_t most,
									const Core::CancellationToken& token)
	{
		std::vector<std::filesystem::path> projects;

		EnumerationInput enumeration;
		enumeration.Root = root;
		enumeration.Ignored = [&root](const std::filesystem::path& path, bool isDirectory) {
			return isDirectory && path != root && (IsSkippedFolder(path) || BelongsToAnotherServer(path));
		};

		EnumerateFiles(enumeration, token, [&projects, extension](const std::filesystem::path& file) {
			if (Core::LoweredExtension(file) == extension)
			{
				projects.push_back(file);
			}
		});

		std::sort(projects.begin(), projects.end(), ComesFirstByPath);

		const std::size_t foundCount = projects.size();

		for (std::size_t index = projects.size(); index > 0 && projects.size() > most; --index)
		{
			if (LiesInTestFolder(projects[index - 1], root))
			{
				projects.erase(projects.begin() + static_cast<std::ptrdiff_t>(index - 1));
			}
		}

		if (projects.size() > most)
		{
			projects.erase(projects.begin() + static_cast<std::ptrdiff_t>(most), projects.end());
		}

		const std::size_t droppedCount = foundCount - projects.size();

		return ProjectsBelow{.Kept = std::move(projects), .DroppedAboveLimit = droppedCount};
	}
}
