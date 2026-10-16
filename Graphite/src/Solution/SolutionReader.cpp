#include "Solution/SolutionReader.h"

#include "Core/Base/Log.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"
#include "Solution/CsprojReader.h"
#include "Solution/ProjectContentReader.h"
#include "Solution/SlnParser.h"
#include "Solution/VcxprojReader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		void ReadProjects(const pugi::xml_node& parent, Folder& folder,
						  const std::filesystem::path& solutionDirectory, std::size_t& count)
		{
			for (const pugi::xml_node& node : parent.children("Project"))
			{
				const char* declaredPath = node.attribute("Path").value();

				if (declaredPath[0] == '\0')
				{
					continue;
				}

				Project project;

				// WARNING: The format writes project paths relative to the solution, usually with
				// backslashes, which std::filesystem accepts on Windows.
				project.Path = (solutionDirectory / Core::FromUtf8(declaredPath)).lexically_normal();
				project.Path.make_preferred();

				project.DisplayName = Core::ToUtf8(project.Path.stem());
				project.TypeId = node.attribute("Type").value();

				std::error_code existsError;
				project.Exists = std::filesystem::is_regular_file(project.Path, existsError);

				folder.Projects.push_back(std::move(project));
				++count;
			}
		}

		void FillContent(Folder& folder)
		{
			static const VcxprojReader kVcxproj;
			static const CsprojReader kCsproj;
			static const ProjectContentReader* const kReaders[] = {&kVcxproj, &kCsproj};

			for (Project& project : folder.Projects)
			{
				for (const ProjectContentReader* reader : kReaders)
				{
					if (!reader->Handles(project.Path))
					{
						continue;
					}

					Core::Result<ProjectContent> content = reader->Read(project.Path);

					if (!content)
					{
						APP_TRACE("Cannot read '{}': {}", Core::ToUtf8(project.Path),
								  content.GetError().Message);
						break;
					}

					project.Content = std::move(content).GetValue();
					project.ContentRead = true;
					break;
				}
			}

			for (Folder& child : folder.Folders)
			{
				FillContent(child);
			}
		}

		bool IsFolderNameBefore(const Folder& left, const Folder& right)
		{
			return Core::CompareNames(left.Name, right.Name) < 0;
		}

		bool IsProjectNameBefore(const Project& left, const Project& right)
		{
			return Core::CompareNames(left.DisplayName, right.DisplayName) < 0;
		}

		void SortSolution(Folder& folder)
		{
			std::sort(folder.Folders.begin(), folder.Folders.end(), IsFolderNameBefore);
			std::sort(folder.Projects.begin(), folder.Projects.end(), IsProjectNameBefore);

			for (Folder& child : folder.Folders)
			{
				SortSolution(child);
			}
		}

		Core::Result<Solution> ReadSlnx(const std::filesystem::path& path)
		{
			pugi::xml_document document;
			const pugi::xml_parse_result parseResult = document.load_file(path.c_str());

			if (!parseResult)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   std::format("cannot parse '{}': {}", Core::ToUtf8(path),
											   parseResult.description())};
			}

			const pugi::xml_node root = document.child("Solution");

			if (!root)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   std::format("'{}' has no Solution element", Core::ToUtf8(path))};
			}

			Solution solution;
			solution.Path = path;
			solution.Name = Core::ToUtf8(path.stem());

			const std::filesystem::path solutionDirectory = path.parent_path();

			ReadProjects(root, solution.Root, solutionDirectory, solution.ProjectCount);

			for (const pugi::xml_node& node : root.children("Folder"))
			{
				const std::vector<std::string> parts =
					Core::SplitSkippingEmpty(node.attribute("Name").value(), '/');

				if (parts.empty())
				{
					continue;
				}

				Folder& folder = EnsureFolderPath(solution.Root, parts);
				ReadProjects(node, folder, solutionDirectory, solution.ProjectCount);
			}

			return solution;
		}

		Core::Result<Solution> ReadSlnText(const std::filesystem::path& path)
		{
			const Core::Result<std::string> text = Core::ReadFileBytes(path);

			if (!text)
			{
				return text.GetError();
			}

			return ParseSln(path, text.GetValue());
		}
	}

	bool IsSolutionFile(const std::filesystem::path& path)
	{
		const std::string extension = Core::LoweredExtension(path);

		return extension == ".slnx" || extension == ".sln";
	}

	Core::Result<Solution> ReadSolution(const std::filesystem::path& requested)
	{
		std::error_code errorCode;

		if (!std::filesystem::is_regular_file(requested, errorCode))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", Core::ToUtf8(requested))};
		}

		std::filesystem::path path = std::filesystem::absolute(requested, errorCode);

		if (errorCode)
		{
			path = requested;
		}

		if (!IsSolutionFile(path))
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a solution format this build can read",
										   Core::ToUtf8(path))};
		}

		Core::Result<Solution> readResult =
			Core::LoweredExtension(path) == ".slnx" ? ReadSlnx(path) : ReadSlnText(path);

		if (!readResult)
		{
			return readResult;
		}

		Solution solution = std::move(readResult).GetValue();

		FillContent(solution.Root);
		SortSolution(solution.Root);

		return solution;
	}
}