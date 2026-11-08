#include "Solution/SlnxEditor.h"

#include "Core/Base/Strings.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Solution/SolutionReader.h"
#include "Solution/TextEdit.h"

#include <pugixml.hpp>

#include <format>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr unsigned int kXmlParseOptions =
			pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_comments;
		constexpr std::size_t kSelfClosingEndLength = 2;
		constexpr std::size_t kElementOffsetToTagStart = 1;
		constexpr const char* kAnyProjectXPath = "//Project";

		struct Parsed
		{
			explicit Parsed(const std::string& text)
			{
				Buffer.assign(text.begin(), text.end());
				Buffer.push_back('\0');

				IsWellFormed = static_cast<bool>(
					Document.load_buffer_inplace(Buffer.data(), Buffer.size() - 1, kXmlParseOptions));
			}

			std::vector<char> Buffer;
			pugi::xml_document Document;
			bool IsWellFormed = false;
		};

		Core::Error NotWellFormedError()
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   "the solution is not well-formed XML"};
		}

		std::size_t OffsetOf(const pugi::xml_node& node)
		{
			return static_cast<std::size_t>(node.offset_debug());
		}

		// WARNING: lexically_relative gives back an empty path across drives, and a Path=""
		// entry is one the reader silently skips.
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

		std::string ProjectElement(const std::string& path, const std::string& identifier)
		{
			return std::format(R"(<Project Path="{}" Id="{}" />)", path, identifier);
		}

		std::string FolderElement(const std::string& logicalPath)
		{
			return std::format(R"(<Folder Name="/{}/" />)", logicalPath);
		}

		std::string FolderAttributeValue(const std::string& logicalPath)
		{
			return "/" + logicalPath + "/";
		}

		void EraseElement(std::string& text, std::size_t offset, std::string_view name)
		{
			const std::size_t begin = offset - kElementOffsetToTagStart;
			const std::size_t end = ElementEnd(text, offset, name);

			[[maybe_unused]] const auto [firstLineBegin, firstLineEnd] = LineExtent(text, begin);
			[[maybe_unused]] const auto [lastLineBegin, lastLineEnd] = LineExtent(text, end);

			const bool onlyWhitespaceBefore = text.find_first_not_of(" \t", firstLineBegin) == begin;
			const bool onlyWhitespaceAfter = text.find_first_not_of(" \t\r\n", end) >= lastLineEnd;
			const bool aloneOnItsLines = onlyWhitespaceBefore && onlyWhitespaceAfter;

			if (aloneOnItsLines)
			{
				text.erase(firstLineBegin, lastLineEnd - firstLineBegin);
			}
			else
			{
				text.erase(begin, end - begin);
			}
		}

		// The element's text with each line after the first moved from one indent to another, so a project with children
		// keeps its shape at the depth it lands at.
		std::string Reindented(const std::string& element, const std::string& from, const std::string& to)
		{
			std::string result;
			std::size_t lineStart = 0;

			while (lineStart <= element.size())
			{
				const std::size_t lineEnd = element.find('\n', lineStart);
				const std::size_t lineStop = lineEnd == std::string::npos ? element.size() : lineEnd + 1;
				std::string_view line{element.data() + lineStart, lineStop - lineStart};

				if (lineStart != 0 && line.starts_with(from))
				{
					result += to;
					line.remove_prefix(from.size());
				}

				result += line;

				if (lineEnd == std::string::npos)
				{
					break;
				}

				lineStart = lineEnd + 1;
			}

			return result;
		}

		std::string IndentOfLineAt(const std::string& text, std::size_t offset)
		{
			[[maybe_unused]] const auto [lineBegin, lineEnd] = LineExtent(text, offset);

			std::string indent;

			for (std::size_t characterIndex = lineBegin;
				 characterIndex < text.size() &&
				 (text[characterIndex] == ' ' || text[characterIndex] == '\t');
				 ++characterIndex)
			{
				indent += text[characterIndex];
			}

			return indent;
		}

		bool EndsElementName(char character)
		{
			return character == ' ' || character == '\t' || character == '\r' ||
				   character == '\n' || character == '>' || character == '/';
		}

		std::string ElementNameAt(const std::string& text, std::size_t offset)
		{
			std::size_t end = offset;

			while (end < text.size() && !EndsElementName(text[end]))
			{
				++end;
			}

			return text.substr(offset, end - offset);
		}

		Core::Result<void> RequireFolderEmpty(const std::string& text, const std::string& folder)
		{
			Parsed parsed{text};

			if (!parsed.IsWellFormed)
			{
				return NotWellFormedError();
			}

			const std::string wantedName = FolderAttributeValue(folder);

			for (const pugi::xml_node& node : parsed.Document.child("Solution").children("Folder"))
			{
				const std::string folderName = node.attribute("Name").value();

				if (folderName == wantedName)
				{
					if (const pugi::xml_node child = node.child("Project"))
					{
						return Core::Error{Core::ErrorCode::InvalidArgument,
										   std::format("the folder '{}' still holds the project '{}'",
													   folder, child.attribute("Path").value())};
					}

					continue;
				}

				if (folderName.starts_with(wantedName))
				{
					return Core::Error{Core::ErrorCode::InvalidArgument,
									   std::format("the folder '{}' still holds the folder '{}'",
												   folder, folderName)};
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
			attributes.WellFormed = parsed.IsWellFormed;

			if (!parsed.IsWellFormed)
			{
				return attributes;
			}

			for (const pugi::xpath_node& projectNode : parsed.Document.select_nodes(kAnyProjectXPath))
			{
				if (OffsetOf(projectNode.node()) != offset)
				{
					continue;
				}

				attributes.Path = projectNode.node().attribute("Path").value();
				attributes.Id = projectNode.node().attribute("Id").value();
				break;
			}

			return attributes;
		}

		Core::Result<std::filesystem::path> PickScratchPath(const std::filesystem::path& path)
		{
			std::filesystem::path stem = path;
			stem.replace_extension();
			const std::filesystem::path extension = path.extension();

			// WARNING: The scratch file keeps the solution's own extension ("Sample.tmp.slnx"),
			// because ReadSolution picks its parser by extension and would refuse a plain ".tmp".
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

		if (!parsed.IsWellFormed)
		{
			return NotWellFormedError();
		}

		std::filesystem::path wanted = project.lexically_normal();
		wanted.make_preferred();

		const std::filesystem::path directory = m_Path.parent_path();

		for (const pugi::xpath_node& projectNode : parsed.Document.select_nodes(kAnyProjectXPath))
		{
			const char* declaredPath = projectNode.node().attribute("Path").value();

			if (declaredPath[0] == '\0')
			{
				continue;
			}

			std::filesystem::path candidate =
				(directory / Core::FromUtf8(declaredPath)).lexically_normal();
			candidate.make_preferred();

			if (candidate == wanted)
			{
				return OffsetOf(projectNode.node());
			}
		}

		return Core::Error{Core::ErrorCode::NotFound,
						   std::format("'{}' is not in the solution", Core::ToUtf8(project))};
	}

	Core::Result<std::size_t> SlnxEditor::FindContainer(const std::string& folder) const
	{
		Parsed parsed{m_Text};

		if (!parsed.IsWellFormed)
		{
			return NotWellFormedError();
		}

		const pugi::xml_node root = parsed.Document.child("Solution");

		if (!root)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the file has no Solution element"};
		}

		if (folder.empty())
		{
			return OffsetOf(root);
		}

		const std::string wantedName = FolderAttributeValue(folder);

		for (const pugi::xml_node& node : root.children("Folder"))
		{
			if (wantedName == node.attribute("Name").value())
			{
				return OffsetOf(node);
			}
		}

		return Core::Error{Core::ErrorCode::NotFound,
						   std::format("the solution has no folder '{}'", folder)};
	}

	void SlnxEditor::InsertChild(std::size_t offset, const std::string& element)
	{
		const std::string lineEnding = DetectLineEnding(m_Text);
		const std::string indentUnit = IndentUnit(m_Text);
		const std::string containerIndent = IndentOfLineAt(m_Text, offset);
		const std::size_t tagEnd = OpeningTagEnd(m_Text, offset);

		const bool containerIsEmpty = EndsSelfClosingTag(m_Text, tagEnd);

		if (containerIsEmpty)
		{
			const std::string containerName = ElementNameAt(m_Text, offset);

			std::string replacement = ">";
			replacement += lineEnding + containerIndent + indentUnit + element;
			replacement += lineEnding + containerIndent + "</" + containerName + ">";

			m_Text.replace(tagEnd - kSelfClosingEndLength, kSelfClosingEndLength, replacement);
			return;
		}

		m_Text.insert(tagEnd, lineEnding + containerIndent + indentUnit + element);
	}

	Core::Result<void> SlnxEditor::AddProject(const std::filesystem::path& project,
											  const std::string& folder)
	{
		const std::string relativePath = RelativeProjectPath(m_Path, project);

		if (relativePath.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' is not under the same root as the solution, so the "
										   "solution cannot name it",
										   Core::ToUtf8(project))};
		}

		std::error_code fileStatusError;

		if (!std::filesystem::is_regular_file(project, fileStatusError))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no project file at '{}'", Core::ToUtf8(project))};
		}

		if (FindProject(project).HasValue())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' is already in the solution", Core::ToUtf8(project))};
		}

		const std::string identifier = NewGuid();

		Core::Result<std::size_t> container = FindContainer(folder);

		if (!container && container.GetError().Code == Core::ErrorCode::NotFound)
		{
			const Core::Result<void> created = AddFolder(folder);

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

		InsertChild(container.GetValue(), ProjectElement(relativePath, identifier));

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::RemoveProject(const std::filesystem::path& project)
	{
		const Core::Result<std::size_t> found = FindProject(project);

		if (!found)
		{
			return found.GetError();
		}

		EraseElement(m_Text, found.GetValue(), "Project");

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::AddFolder(const std::string& folder)
	{
		const std::vector<std::string> parts = Core::SplitSkippingEmpty(folder, '/');

		if (parts.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "a solution folder needs a name"};
		}

		if (FindContainer(folder).HasValue())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("the solution already has a folder '{}'", folder)};
		}

		std::vector<std::string> missingFromShallowestToDeepest;
		std::string ancestorPath;

		for (const std::string& part : parts)
		{
			ancestorPath += ancestorPath.empty() ? part : "/" + part;

			if (!FindContainer(ancestorPath).HasValue())
			{
				missingFromShallowestToDeepest.push_back(ancestorPath);
			}
		}

		const std::string solutionRoot;

		for (std::size_t remainingCount = missingFromShallowestToDeepest.size(); remainingCount > 0;
			 --remainingCount)
		{
			const std::string& missingPath = missingFromShallowestToDeepest[remainingCount - 1];
			const Core::Result<std::size_t> root = FindContainer(solutionRoot);

			if (!root)
			{
				return root.GetError();
			}

			InsertChild(root.GetValue(), FolderElement(missingPath));
		}

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::RemoveFolder(const std::string& folder)
	{
		if (folder.empty())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the solution root is not a folder"};
		}

		const Core::Result<std::size_t> found = FindContainer(folder);

		if (!found)
		{
			return found.GetError();
		}

		if (const Core::Result<void> empty = RequireFolderEmpty(m_Text, folder); !empty)
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

		const std::string oldPath = FolderAttributeValue(folder);

		const std::size_t lastSlash = folder.find_last_of('/');
		const std::string parentPrefix =
			lastSlash == std::string::npos ? std::string{} : folder.substr(0, lastSlash + 1);
		const std::string newPath = "/" + parentPrefix + name + "/";

		Parsed parsed{m_Text};

		if (!parsed.IsWellFormed)
		{
			return NotWellFormedError();
		}

		std::vector<NameEdit> edits;

		for (const pugi::xml_node& node : parsed.Document.child("Solution").children("Folder"))
		{
			const std::string folderName = node.attribute("Name").value();

			if (!folderName.starts_with(oldPath))
			{
				continue;
			}

			// WARNING: pugixml gives an attribute value no offset of its own, so the value is
			// found as the first occurrence after the element start.
			const std::size_t elementBegin = OffsetOf(node);
			const std::size_t valueBegin = m_Text.find(folderName, elementBegin);

			if (valueBegin == std::string::npos)
			{
				continue;
			}

			edits.push_back(NameEdit{valueBegin, folderName.size(),
									 newPath + folderName.substr(oldPath.size())});
		}

		if (edits.empty())
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("the solution has no folder '{}'", folder)};
		}

		// WARNING: The edits are applied last to first. A longer replacement shifts every offset
		// after it, and applying them forwards would corrupt the file in a way that still parses.
		for (std::size_t remainingCount = edits.size(); remainingCount > 0; --remainingCount)
		{
			const NameEdit& edit = edits[remainingCount - 1];
			m_Text.replace(edit.Begin, edit.Length, edit.NewValue);
		}

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::MoveProject(const std::filesystem::path& project,
											   const std::string& folder)
	{
		const Core::Result<std::size_t> found = FindProject(project);

		if (!found)
		{
			return found.GetError();
		}

		ProjectAttributes attributes = ReadProjectAt(m_Text, found.GetValue());

		if (!attributes.WellFormed)
		{
			return NotWellFormedError();
		}

		// WARNING: The element moves as it is written, every attribute and child with it: one rebuilt from Path and Id
		// alone dropped DefaultStartup and the project's BuildDependency elements. Only an element with no Path, which
		// cannot be moved as it is, is written anew.
		const std::size_t elementBegin = found.GetValue() - kElementOffsetToTagStart;
		const std::size_t elementEnd = ElementEnd(m_Text, found.GetValue(), "Project");
		const std::string elementIndent = IndentOfLineAt(m_Text, elementBegin);
		std::string element = m_Text.substr(elementBegin, elementEnd - elementBegin);

		if (attributes.Path.empty())
		{
			attributes.Path = RelativeProjectPath(m_Path, project);
			attributes.Id = attributes.Id.empty() ? NewGuid() : attributes.Id;
			element = ProjectElement(attributes.Path, attributes.Id);
		}

		const bool folderMustBeCreated = !folder.empty() && !FindContainer(folder).HasValue();

		if (folderMustBeCreated)
		{
			const Core::Result<void> created = AddFolder(folder);

			if (!created)
			{
				return created.GetError();
			}
		}

		const Core::Result<void> removed = RemoveProject(project);

		if (!removed)
		{
			return removed.GetError();
		}

		const Core::Result<std::size_t> containerAfterRemoval = FindContainer(folder);

		if (!containerAfterRemoval)
		{
			return containerAfterRemoval.GetError();
		}

		const std::string landingIndent = IndentOfLineAt(m_Text, containerAfterRemoval.GetValue()) + IndentUnit(m_Text);

		InsertChild(containerAfterRemoval.GetValue(), Reindented(element, elementIndent, landingIndent));

		return Core::Result<void>::Ok();
	}

	Core::Result<void> SlnxEditor::Save()
	{
		const Core::Result<std::filesystem::path> scratch = PickScratchPath(m_Path);

		if (!scratch)
		{
			return scratch.GetError();
		}

		if (const Core::Result<void> written = WriteWholeFile(scratch.GetValue(), m_Text); !written)
		{
			return written.GetError();
		}

		const Core::Result<Solution> readBack = ReadSolution(scratch.GetValue());

		std::error_code ignored;
		std::filesystem::remove(scratch.GetValue(), ignored);

		if (!readBack)
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("the edited solution could not be read back: {}",
										   readBack.GetError().Message)};
		}

		return Core::WriteFileAtomically(m_Path, m_Text);
	}
}
