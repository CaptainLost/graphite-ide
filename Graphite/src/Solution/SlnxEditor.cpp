#include "Solution/SlnxEditor.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Solution/SolutionReader.h"
#include "Solution/TextEdit.h"

#include <pugixml.hpp>

#include <format>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		struct Parsed
		{
			explicit Parsed(const std::string& text)
			{
				Buffer.assign(text.begin(), text.end());
				Buffer.push_back('\0');

				Ok = static_cast<bool>(Document.load_buffer_inplace(
					Buffer.data(), Buffer.size() - 1,
					pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_comments));
			}

			std::vector<char> Buffer;
			pugi::xml_document Document;
			bool Ok = false;
		};

		std::string RelativeProjectPath(const std::filesystem::path& solution,
										const std::filesystem::path& project)
		{
			std::string text = Core::ToUtf8(project.lexically_relative(solution.parent_path()));

			for (char& character : text)
			{
				if (character == '/')
				{
					character = '\\';
				}
			}

			return text;
		}

		std::vector<std::string> SplitLogicalPath(const std::string& folder)
		{
			std::vector<std::string> parts;
			std::size_t start = 0;

			while (start < folder.size())
			{
				const std::size_t slash = folder.find('/', start);
				const std::size_t end = slash == std::string::npos ? folder.size() : slash;

				if (end > start)
				{
					parts.push_back(folder.substr(start, end - start));
				}

				if (slash == std::string::npos)
				{
					break;
				}

				start = slash + 1;
			}

			return parts;
		}

		void EraseElement(std::string& text, std::size_t offset, std::string_view name)
		{
			// pugixml reports an element's offset just past its '<'.
			const std::size_t begin = offset - 1;
			const std::size_t end = ElementEnd(text, offset, name);

			// An element with children spans several lines, so the span has to be measured
			// from both ends or the children and the closing tag are left behind.
			const std::size_t lineBegin = LineExtent(text, begin).first;
			const std::size_t lineEnd = LineExtent(text, end).second;

			const bool aloneOnItsLines = text.find_first_not_of(" \t", lineBegin) == begin && text.find_first_not_of(" \t\r\n", end) >= lineEnd;

			if (aloneOnItsLines)
			{
				text.erase(lineBegin, lineEnd - lineBegin);
			}
			else
			{
				text.erase(begin, end - begin);
			}
		}

		std::string IndentOfLineAt(const std::string& text, std::size_t offset)
		{
			const std::size_t lineBegin = LineExtent(text, offset).first;
			std::string indent;

			for (std::size_t i = lineBegin; i < text.size() && (text[i] == ' ' || text[i] == '\t');
				 ++i)
			{
				indent += text[i];
			}

			return indent;
		}

		std::string ElementNameAt(const std::string& text, std::size_t offset)
		{
			std::size_t end = offset;

			while (end < text.size() && text[end] != ' ' && text[end] != '\t' &&
				   text[end] != '\r' && text[end] != '\n' && text[end] != '>' && text[end] != '/')
			{
				++end;
			}

			return text.substr(offset, end - offset);
		}

		Core::Result<void> RequireFolderEmpty(const std::string& text, const std::string& folder)
		{
			Parsed parsed{text};

			if (!parsed.Ok)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument,
								   "the solution is not well-formed XML"};
			}

			const std::string wanted = "/" + folder + "/";

			for (const pugi::xml_node& node : parsed.Document.child("Solution").children("Folder"))
			{
				const std::string current = node.attribute("Name").value();

				if (current == wanted)
				{
					if (const pugi::xml_node child = node.child("Project"))
					{
						return Core::Error{Core::ErrorCode::InvalidArgument,
										   std::format("the folder '{}' still holds the project '{}'",
													   folder, child.attribute("Path").value())};
					}

					continue;
				}

				if (current.rfind(wanted, 0) == 0)
				{
					return Core::Error{Core::ErrorCode::InvalidArgument,
									   std::format("the folder '{}' still holds the folder '{}'",
												   folder, current)};
				}
			}

			return Core::Result<void>::Ok();
		}

		struct NameEdit
		{
			std::size_t Begin = 0;
			std::size_t Length = 0;
			std::string NewValue;
		};

		struct ProjectAttributes
		{
			std::string Path;
			std::string Id;
			bool WellFormed = false;
		};

		ProjectAttributes ReadProjectAt(const std::string& text, std::size_t offset)
		{
			Parsed parsed{text};

			ProjectAttributes attributes;
			attributes.WellFormed = parsed.Ok;

			if (!parsed.Ok)
			{
				return attributes;
			}

			for (const pugi::xpath_node& node : parsed.Document.select_nodes("//Project"))
			{
				if (static_cast<std::size_t>(node.node().offset_debug()) != offset)
				{
					continue;
				}

				attributes.Path = node.node().attribute("Path").value();
				attributes.Id = node.node().attribute("Id").value();
				break;
			}

			return attributes;
		}

		Core::Result<std::filesystem::path> PickScratchPath(const std::filesystem::path& path)
		{
			std::filesystem::path stem = path;
			stem.replace_extension();
			const std::filesystem::path extension = path.extension();

			// "Sample.tmp.slnx", keeping the solution's own extension: ReadSolution picks
			// its parser by extension and would refuse a plain ".tmp".
			return Core::PickAvailablePath([&stem, &extension](int attempt) {
				std::filesystem::path candidate = stem;
				candidate += attempt == 0 ? std::string{".tmp"} : std::format(".tmp{}", attempt);
				candidate += extension;
				return candidate;
			});
		}

		Core::Result<void> WriteWholeFile(const std::filesystem::path& path, const std::string& text)
		{
			const Core::Error failure{Core::ErrorCode::IoFailure,
									  std::format("cannot write '{}'", Core::ToUtf8(path))};

			std::ofstream stream{path, std::ios::binary | std::ios::trunc};

			if (!stream.is_open())
			{
				return failure;
			}

			stream << text;

			if (!stream.good())
			{
				stream.close();

				std::error_code ignored;
				std::filesystem::remove(path, ignored);

				return failure;
			}

			return Core::Result<void>::Ok();
		}
	}

	SlnxEditor::SlnxEditor(std::filesystem::path path, std::string text)
		: m_Path(std::move(path)), m_Text(std::move(text))
	{
	}

	Core::Result<std::size_t> SlnxEditor::FindProject(const std::filesystem::path& project) const
	{
		Parsed parsed{m_Text};

		if (!parsed.Ok)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the solution is not well-formed XML"};
		}

		std::filesystem::path wanted = project.lexically_normal();
		wanted.make_preferred();

		const std::filesystem::path directory = m_Path.parent_path();

		// The format allows a Project element at the root and inside any Folder.
		for (const pugi::xpath_node& node : parsed.Document.select_nodes("//Project"))
		{
			const char* declared = node.node().attribute("Path").value();

			if (declared[0] == '\0')
			{
				continue;
			}

			std::filesystem::path candidate =
				(directory / Core::FromUtf8(declared)).lexically_normal();
			candidate.make_preferred();

			if (candidate == wanted)
			{
				return static_cast<std::size_t>(node.node().offset_debug());
			}
		}

		return Core::Error{Core::ErrorCode::NotFound,
						   std::format("'{}' is not in the solution", Core::ToUtf8(project))};
	}

	Core::Result<std::size_t> SlnxEditor::FindContainer(const std::string& folder) const
	{
		Parsed parsed{m_Text};

		if (!parsed.Ok)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the solution is not well-formed XML"};
		}

		const pugi::xml_node root = parsed.Document.child("Solution");

		if (!root)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the file has no Solution element"};
		}

		if (folder.empty())
		{
			return static_cast<std::size_t>(root.offset_debug());
		}

		// Folders are flat elements under the root; the nesting lives in the Name attribute,
		// which carries a slash at each end.
		const std::string wanted = "/" + folder + "/";

		for (const pugi::xml_node& node : root.children("Folder"))
		{
			if (wanted == node.attribute("Name").value())
			{
				return static_cast<std::size_t>(node.offset_debug());
			}
		}

		return Core::Error{Core::ErrorCode::NotFound,
						   std::format("the solution has no folder '{}'", folder)};
	}

	void SlnxEditor::InsertChild(std::size_t offset, const std::string& element)
	{
		const std::string ending = DetectLineEnding(m_Text);
		const std::string unit = IndentUnit(m_Text);
		const std::string containerIndent = IndentOfLineAt(m_Text, offset);
		const std::size_t tagEnd = OpeningTagEnd(m_Text, offset);

		// A container with no children yet is written self-closing, so it has to become an
		// open/close pair before anything can go inside it.
		if (EndsSelfClosingTag(m_Text, tagEnd))
		{
			const std::string name = ElementNameAt(m_Text, offset);

			std::string replacement = ">";
			replacement += ending + containerIndent + unit + element;
			replacement += ending + containerIndent + "</" + name + ">";

			m_Text.replace(tagEnd - 2, 2, replacement);
			return;
		}

		m_Text.insert(tagEnd, ending + containerIndent + unit + element);
	}

	Core::Result<void> SlnxEditor::AddProject(const std::filesystem::path& project,
											  const std::string& folder)
	{
		// lexically_relative gives back nothing at all across drives, and the Path="" entry
		// that would write is one the reader silently skips.
		const std::string relative = RelativeProjectPath(m_Path, project);

		if (relative.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' is not under the same root as the solution, so the "
										   "solution cannot name it",
										   Core::ToUtf8(project))};
		}

		std::error_code code;

		if (!std::filesystem::is_regular_file(project, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no project file at '{}'", Core::ToUtf8(project))};
		}

		if (FindProject(project).HasValue())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' is already in the solution", Core::ToUtf8(project))};
		}

		const std::string id = NewGuid();

		auto container = FindContainer(folder);

		if (!container && container.GetError().Code == Core::ErrorCode::NotFound)
		{
			const auto created = AddFolder(folder);

			if (!created)
			{
				return created.GetError();
			}

			container = FindContainer(folder);
		}

		if (!container)
		{
			return container.GetError();
		}

		const std::string element =
			std::format(R"(<Project Path="{}" Id="{}" />)", relative, id);

		InsertChild(container.GetValue(), element);

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::RemoveProject(const std::filesystem::path& project)
	{
		const auto found = FindProject(project);

		if (!found)
		{
			return found.GetError();
		}

		EraseElement(m_Text, found.GetValue(), "Project");

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::AddFolder(const std::string& folder)
	{
		const std::vector<std::string> parts = SplitLogicalPath(folder);

		if (parts.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "a solution folder needs a name"};
		}

		if (FindContainer(folder).HasValue())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("the solution already has a folder '{}'", folder)};
		}

		// Every missing ancestor needs an element of its own: nesting lives in the name, so
		// a folder whose parent has no element has nothing to hang from.
		std::vector<std::string> missing;
		std::string prefix;

		for (const std::string& part : parts)
		{
			prefix += prefix.empty() ? part : "/" + part;

			if (!FindContainer(prefix).HasValue())
			{
				missing.push_back(prefix);
			}
		}

		// Deepest first, because each insertion goes in as the container's first child, so
		// the shallowest ends up first in the file.
		for (std::size_t i = missing.size(); i > 0; --i)
		{
			const auto root = FindContainer("");

			if (!root)
			{
				return root.GetError();
			}

			InsertChild(root.GetValue(), std::format(R"(<Folder Name="/{}/" />)", missing[i - 1]));
		}

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::RemoveFolder(const std::string& folder)
	{
		if (folder.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the solution root is not a folder"};
		}

		const auto found = FindContainer(folder);

		if (!found)
		{
			return found.GetError();
		}

		if (const auto empty = RequireFolderEmpty(m_Text, folder); !empty)
		{
			return empty.GetError();
		}

		EraseElement(m_Text, found.GetValue(), "Folder");

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::RenameFolder(const std::string& folder, const std::string& name)
	{
		if (name.empty() || name.find('/') != std::string::npos)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' is not a usable folder name", name)};
		}

		const std::string oldPath = "/" + folder + "/";

		// The parent's path is a prefix of every descendant's, so the new prefix is the
		// old one with its last component swapped.
		const std::size_t lastSlash = folder.find_last_of('/');
		const std::string parent =
			lastSlash == std::string::npos ? std::string{} : folder.substr(0, lastSlash + 1);
		const std::string newPath = "/" + parent + name + "/";

		Parsed parsed{m_Text};

		if (!parsed.Ok)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the solution is not well-formed XML"};
		}

		std::vector<NameEdit> edits;

		for (const pugi::xml_node& node : parsed.Document.child("Solution").children("Folder"))
		{
			const std::string current = node.attribute("Name").value();

			if (current.rfind(oldPath, 0) != 0)
			{
				continue;
			}

			// pugixml gives an attribute value no offset of its own, so it is found as the
			// first occurrence after the element start.
			const auto elementBegin = static_cast<std::size_t>(node.offset_debug());
			const std::size_t valueBegin = m_Text.find(current, elementBegin);

			if (valueBegin == std::string::npos)
			{
				continue;
			}

			edits.push_back(NameEdit{valueBegin, current.size(),
									 newPath + current.substr(oldPath.size())});
		}

		if (edits.empty())
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("the solution has no folder '{}'", folder)};
		}

		// Applied last to first: a longer replacement shifts every offset after it, and
		// applying them forwards would corrupt the file in a way that still parses.
		for (std::size_t i = edits.size(); i > 0; --i)
		{
			const NameEdit& edit = edits[i - 1];
			m_Text.replace(edit.Begin, edit.Length, edit.NewValue);
		}

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::MoveProject(const std::filesystem::path& project,
											   const std::string& folder)
	{
		const auto found = FindProject(project);

		if (!found)
		{
			return found.GetError();
		}

		ProjectAttributes attributes = ReadProjectAt(m_Text, found.GetValue());

		if (!attributes.WellFormed)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   "the solution is not well-formed XML"};
		}

		if (attributes.Path.empty())
		{
			attributes.Path = RelativeProjectPath(m_Path, project);
		}

		// Carried over rather than regenerated: a new identifier would make the moved
		// project look like a different project to anything keying off it.
		if (attributes.Id.empty())
		{
			attributes.Id = NewGuid();
		}

		// The folder is created first, so the removal below is the last thing that can go
		// wrong — and it cannot, the element having just been found.
		if (!folder.empty() && !FindContainer(folder).HasValue())
		{
			const auto created = AddFolder(folder);

			if (!created)
			{
				return created.GetError();
			}
		}

		const auto removed = RemoveProject(project);

		if (!removed)
		{
			return removed.GetError();
		}

		// Looked up again after the removal, which moved every offset after it.
		const auto container = FindContainer(folder);

		if (!container)
		{
			return container.GetError();
		}

		InsertChild(container.GetValue(),
					std::format(R"(<Project Path="{}" Id="{}" />)", attributes.Path, attributes.Id));

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::Save()
	{
		const Core::Result<std::filesystem::path> scratch = PickScratchPath(m_Path);

		if (!scratch)
		{
			return scratch.GetError();
		}

		if (const auto written = WriteWholeFile(scratch.GetValue(), m_Text); !written)
		{
			return written.GetError();
		}

		// The edited text never reaches the file Visual Studio has open until this build's
		// own reader has proved it can read it back.
		const auto verified = ReadSolution(scratch.GetValue());

		std::error_code ignored;
		std::filesystem::remove(scratch.GetValue(), ignored);

		if (!verified)
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("the edited solution could not be read back: {}",
										   verified.GetError().Message)};
		}

		return Core::WriteFileAtomically(m_Path, m_Text);
	}
}