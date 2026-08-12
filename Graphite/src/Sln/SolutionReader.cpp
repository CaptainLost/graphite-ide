#include "Sln/SolutionReader.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Sln/CsprojReader.h"
#include "Sln/ProjectContentReader.h"
#include "Sln/SlnParser.h"
#include "Sln/VcxprojReader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		std::string LowercaseExtension(const std::filesystem::path& path)
		{
			std::string extension = Core::ToUtf8(path.extension());

			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

			return extension;
		}

		std::vector<std::string> SplitFolderPath(std::string_view name)
		{
			std::vector<std::string> parts;
			std::size_t start = 0;

			while (start < name.size())
			{
				const std::size_t slash = name.find('/', start);
				const std::size_t end = slash == std::string_view::npos ? name.size() : slash;

				if (end > start)
					parts.emplace_back(name.substr(start, end - start));

				if (slash == std::string_view::npos)
					break;

				start = slash + 1;
			}

			return parts;
		}

		Folder& EnsureFolder(Folder& root, const std::vector<std::string>& parts)
		{
			Folder* current = &root;

			for (const std::string& part : parts)
			{
				const auto match = std::find_if(current->Folders.begin(), current->Folders.end(),
												[&part](const Folder& f) { return f.Name == part; });

				if (match != current->Folders.end())
				{
					current = &*match;
					continue;
				}

				Folder created;
				created.Name = part;
				current->Folders.push_back(std::move(created));
				current = &current->Folders.back();
			}

			return *current;
		}

		void ReadProjects(const pugi::xml_node& parent, Folder& folder,
						  const std::filesystem::path& solutionDirectory, std::size_t& count)
		{
			for (const pugi::xml_node& node : parent.children("Project"))
			{
				const char* declared = node.attribute("Path").value();

				if (declared[0] == '\0')
					continue;

				Project project;

				// Declared relative to the solution and usually with backslashes, which
				// std::filesystem accepts on Windows. Made absolute here, once, because
				// everything downstream opens the file rather than displaying the path.
				project.Path = (solutionDirectory / Core::FromUtf8(declared)).lexically_normal();
				project.Path.make_preferred();

				project.DisplayName = Core::ToUtf8(project.Path.stem());
				project.TypeId = node.attribute("Type").value();

				// One stat per project, and worth it: a solution naming a file that is
				// not there is a real and common problem, and the view is meant to show
				// it rather than to quietly render an ordinary-looking node.
				std::error_code exists;
				project.Exists = std::filesystem::is_regular_file(project.Path, exists);

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
						continue;

					auto content = reader->Read(project.Path);

					// A project that cannot be read does not sink the solution: the
					// structure around it is still true and still useful.
					if (!content)
					{
						CORE_TRACE("Cannot read '{}': {}", project.Path.string(),
								   content.GetError().Message);
						break;
					}

					project.Content = std::move(content).GetValue();
					project.ContentRead = true;
					break;
				}
			}

			for (Folder& child : folder.Folders)
				FillContent(child);
		}

		void SortSolution(Folder& folder)
		{
			std::sort(folder.Folders.begin(), folder.Folders.end(),
					  [](const Folder& left, const Folder& right) { return Core::CompareNames(left.Name, right.Name) < 0; });

			std::sort(folder.Projects.begin(), folder.Projects.end(),
					  [](const Project& left, const Project& right) { return Core::CompareNames(left.DisplayName, right.DisplayName) < 0; });

			for (Folder& child : folder.Folders)
				SortSolution(child);
		}

		Core::Result<Solution> ReadSlnx(const std::filesystem::path& path)
		{
			pugi::xml_document document;
			const pugi::xml_parse_result parsed = document.load_file(path.c_str());

			if (!parsed)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   std::format("cannot parse '{}': {}", path.string(),
											   parsed.description())};
			}

			const pugi::xml_node root = document.child("Solution");

			if (!root)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   std::format("'{}' has no Solution element", path.string())};
			}

			Solution solution;
			solution.Path = path;
			solution.Name = Core::ToUtf8(path.stem());

			const std::filesystem::path directory = path.parent_path();

			// Projects declared outside any Folder belong to the solution itself.
			ReadProjects(root, solution.Root, directory, solution.ProjectCount);

			for (const pugi::xml_node& node : root.children("Folder"))
			{
				const std::vector<std::string> parts = SplitFolderPath(node.attribute("Name").value());

				if (parts.empty())
					continue;

				Folder& folder = EnsureFolder(solution.Root, parts);
				ReadProjects(node, folder, directory, solution.ProjectCount);
			}

			return solution;
		}

		Core::Result<Solution> ReadSlnText(const std::filesystem::path& path)
		{
			std::ifstream stream{path, std::ios::binary};

			if (!stream.is_open())
			{
				return Core::Error{Core::ErrorCode::IoFailure,
								   std::format("cannot open '{}'", path.string())};
			}

			std::ostringstream buffer;
			buffer << stream.rdbuf();

			return ParseSln(path, buffer.str());
		}
	}

	bool IsSolutionFile(const std::filesystem::path& path)
	{
		const std::string extension = LowercaseExtension(path);

		return extension == ".slnx" || extension == ".sln";
	}

	Core::Result<Solution> ReadSolution(const std::filesystem::path& requested)
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(requested, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", requested.string())};
		}

		// Resolved once, here, rather than trusting the caller. Every project path is
		// built by appending to this one, and the model promises absolute paths — a
		// relative argument would quietly hand back relative projects that only resolve
		// while the process happens to sit in the right directory.
		std::filesystem::path path = std::filesystem::absolute(requested, code);

		if (code)
			path = requested;

		if (!IsSolutionFile(path))
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a solution format this build can read",
										   path.string())};
		}

		Core::Result<Solution> parsed =
			LowercaseExtension(path) == ".slnx" ? ReadSlnx(path) : ReadSlnText(path);

		if (!parsed)
			return parsed;

		Solution solution = std::move(parsed).GetValue();

		// Run after either parser, so the two formats cannot drift apart in what they
		// hand back.
		FillContent(solution.Root);
		SortSolution(solution.Root);

		return solution;
	}
}