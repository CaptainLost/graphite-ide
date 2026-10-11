#include "Language/Server/SolutionScope.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/FileSystem/FileSystem.h"

#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		bool IsSolutionMarker(std::string_view marker)
		{
			return marker == "*.sln" || marker == "*.slnx";
		}

		bool RootsAtSolution(const ServerDefinition& server)
		{
			for (const std::string_view marker : server.RootMarkers)
			{
				if (IsSolutionMarker(marker))
				{
					return true;
				}
			}

			return false;
		}

		bool IsSolutionFile(const std::filesystem::directory_entry& entry)
		{
			std::error_code kindError;
			const std::string extension = Core::LoweredExtension(entry.path());

			return entry.is_regular_file(kindError) && (extension == ".sln" || extension == ".slnx");
		}

		bool LiesBelow(const std::filesystem::path& directory, const std::filesystem::path& folder)
		{
			return Core::IsSameOrWithin(directory, folder) && !Core::IsSameOrWithin(folder, directory);
		}

		std::filesystem::path FolderOf(std::filesystem::path root, const std::filesystem::path& selectedSolution)
		{
			if (!selectedSolution.empty() && Core::IsSameOrWithin(selectedSolution, root))
			{
				return selectedSolution.parent_path();
			}

			return root;
		}
	}

	bool LanguageRootsAtSolution(LanguageId language)
	{
		for (const std::string_view serverId : DefinitionOf(language).Servers)
		{
			const ServerDefinition* server = FindServer(serverId);

			if (server != nullptr && RootsAtSolution(*server))
			{
				return true;
			}
		}

		return false;
	}

	SolutionScope::SolutionScope(std::filesystem::path root, const std::filesystem::path& selectedSolution)
		: m_Folder(FolderOf(std::move(root), selectedSolution))
	{
	}

	const std::filesystem::path& SolutionScope::GetFolder() const
	{
		return m_Folder;
	}

	bool SolutionScope::Includes(const std::filesystem::path& file)
	{
		if (!Core::IsSameOrWithin(file, m_Folder))
		{
			return false;
		}

		for (std::filesystem::path directory = file.parent_path(); LiesBelow(directory, m_Folder); directory = directory.parent_path())
		{
			if (HoldsSolution(directory))
			{
				return false;
			}
		}

		return true;
	}

	bool SolutionScope::HoldsSolution(const std::filesystem::path& directory)
	{
		const std::map<std::filesystem::path, bool>::const_iterator known = m_HoldsSolution.find(directory);

		if (known != m_HoldsSolution.end())
		{
			return known->second;
		}

		bool holds = false;
		std::error_code listError;
		std::filesystem::directory_iterator entries{directory, listError};

		for (; !listError && entries != std::filesystem::directory_iterator{}; entries.increment(listError))
		{
			if (IsSolutionFile(*entries))
			{
				holds = true;
				break;
			}
		}

		m_HoldsSolution.emplace(directory, holds);
		return holds;
	}
}
