#include "corepch.h"

#include "Core/Sln/SlnxEditor.h"

#include "Core/Fs/AtomicWrite.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Sln/SolutionReader.h"
#include "Core/Sln/TextEdit.h"

#include <pugixml.hpp>

#include <fstream>
#include <system_error>
#include <vector>

namespace Core
{
    namespace
    {
        /// Parses a copy of the text in place, so offsets mean something. The buffer must
        /// outlive every use of the document, which is why the two are constructed
        /// together and never handed out separately.
        struct Parsed
        {
            explicit Parsed(const std::string& text)
            {
                buffer.assign(text.begin(), text.end());
                buffer.push_back('\0');

                ok = static_cast<bool>(document.load_buffer_inplace(
                    buffer.data(), buffer.size() - 1,
                    pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_comments));
            }

            std::vector<char> buffer;
            pugi::xml_document document;
            bool ok = false;
        };

        /// The path a solution writes for a project: relative to the solution's own
        /// directory, with backslashes, because that is what both formats and Visual
        /// Studio use. An absolute path breaks the moment the repository is cloned
        /// somewhere else.
        std::string RelativeProjectPath(const std::filesystem::path& solution,
                                        const std::filesystem::path& project)
        {
            std::string text = ToUtf8(project.lexically_relative(solution.parent_path()));

            for (char& character : text)
            {
                if (character == '/')
                    character = '\\';
            }

            return text;
        }

        /// Splits "App/Sub" into {"App", "Sub"}, dropping empty components so a stray
        /// slash cannot produce a folder with no name.
        std::vector<std::string> SplitLogicalPath(const std::string& folder)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;

            while (start < folder.size())
            {
                const std::size_t slash = folder.find('/', start);
                const std::size_t end = slash == std::string::npos ? folder.size() : slash;

                if (end > start)
                    parts.push_back(folder.substr(start, end - start));

                if (slash == std::string::npos)
                    break;

                start = slash + 1;
            }

            return parts;
        }

        /// Removes the element beginning at `offset` — pugixml's, one past the '<' — and
        /// the lines it occupies with it when nothing else shares them, so no blank line is
        /// left behind.
        void EraseElement(std::string& text, std::size_t offset, std::string_view name)
        {
            const std::size_t begin = offset - 1;   // back over the '<'
            const std::size_t end = ElementEnd(text, offset, name);

            // Both ends are needed: an element with children spans several lines, and
            // measuring the span from its first line alone erases that one line and leaves
            // the children and the orphaned closing tag behind.
            const std::size_t lineBegin = LineExtent(text, begin).first;
            const std::size_t lineEnd = LineExtent(text, end).second;

            const bool aloneOnItsLines = text.find_first_not_of(" \t", lineBegin) == begin
                                         && text.find_first_not_of(" \t\r\n", end) >= lineEnd;

            if (aloneOnItsLines)
                text.erase(lineBegin, lineEnd - lineBegin);
            else
                text.erase(begin, end - begin);
        }
    }

    SlnxEditor::SlnxEditor(std::filesystem::path path, std::string text)
        : m_path(std::move(path)), m_text(std::move(text))
    {
    }

    Result<std::size_t> SlnxEditor::FindProject(const std::filesystem::path& project) const
    {
        Parsed parsed{m_text};

        if (!parsed.ok)
            return Error{ErrorCode::InvalidArgument, "the solution is not well-formed XML"};

        std::filesystem::path wanted = project.lexically_normal();
        wanted.make_preferred();

        const std::filesystem::path directory = m_path.parent_path();

        // Every Project element, wherever it sits: the format allows them at the root and
        // inside any Folder, and a caller naming a file should not have to say which.
        for (const pugi::xpath_node& node : parsed.document.select_nodes("//Project"))
        {
            const char* declared = node.node().attribute("Path").value();

            if (declared[0] == '\0')
                continue;

            std::filesystem::path candidate =
                (directory / FromUtf8(declared)).lexically_normal();
            candidate.make_preferred();

            if (candidate == wanted)
                return static_cast<std::size_t>(node.node().offset_debug());
        }

        return Error{ErrorCode::NotFound,
                     std::format("'{}' is not in the solution", project.string())};
    }

    Result<std::size_t> SlnxEditor::FindContainer(const std::string& folder) const
    {
        Parsed parsed{m_text};

        if (!parsed.ok)
            return Error{ErrorCode::InvalidArgument, "the solution is not well-formed XML"};

        const pugi::xml_node root = parsed.document.child("Solution");

        if (!root)
            return Error{ErrorCode::InvalidArgument, "the file has no Solution element"};

        if (folder.empty())
            return static_cast<std::size_t>(root.offset_debug());

        // Folders are flat elements under the root; the nesting lives in the name, with a
        // slash at each end.
        const std::string wanted = "/" + folder + "/";

        for (const pugi::xml_node& node : root.children("Folder"))
        {
            if (wanted == node.attribute("Name").value())
                return static_cast<std::size_t>(node.offset_debug());
        }

        return Error{ErrorCode::NotFound, std::format("the solution has no folder '{}'", folder)};
    }

    void SlnxEditor::InsertChild(std::size_t offset, const std::string& element)
    {
        const std::string ending = DetectLineEnding(m_text);
        const std::string unit = IndentUnit(m_text);

        // Indented one level past the container's own line.
        const std::size_t lineBegin = LineExtent(m_text, offset).first;
        std::string containerIndent;

        for (std::size_t i = lineBegin;
             i < m_text.size() && (m_text[i] == ' ' || m_text[i] == '\t'); ++i)
        {
            containerIndent += m_text[i];
        }

        const std::size_t tagEnd = OpeningTagEnd(m_text, offset);

        // A container with no children yet is written self-closing, so it has to become an
        // open/close pair before anything can go inside it.
        if (tagEnd >= 2 && m_text[tagEnd - 2] == '/')
        {
            // Name it from the text rather than guessing: this is used for both Solution
            // and Folder.
            std::size_t nameEnd = offset;

            // A container written with its attributes on the next line ends its name at the
            // line break, so both terminators belong here too.
            while (nameEnd < m_text.size() && m_text[nameEnd] != ' ' && m_text[nameEnd] != '\t'
                   && m_text[nameEnd] != '\r' && m_text[nameEnd] != '\n' && m_text[nameEnd] != '>'
                   && m_text[nameEnd] != '/')
            {
                ++nameEnd;
            }

            const std::string name = m_text.substr(offset, nameEnd - offset);

            std::string replacement = ">";
            replacement += ending + containerIndent + unit + element;
            replacement += ending + containerIndent + "</" + name + ">";

            // Replaces the "/>" that ended the empty element.
            m_text.replace(tagEnd - 2, 2, replacement);
            return;
        }

        m_text.insert(tagEnd, ending + containerIndent + unit + element);
    }

    Result<void> SlnxEditor::AddProject(const std::filesystem::path& project,
                                        const std::string& folder)
    {
        // A solution names its projects relatively, and there is no relative path from one
        // drive to another: lexically_relative gives back nothing at all. Writing that
        // empty string produces a Path="" entry the reader then skips, so the garbage
        // survives every check that follows.
        const std::string relative = RelativeProjectPath(m_path, project);

        if (relative.empty())
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' is not under the same root as the solution, so the "
                                     "solution cannot name it",
                                     project.string())};
        }

        std::error_code code;

        if (!std::filesystem::is_regular_file(project, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no project file at '{}'", project.string())};
        }

        // Checked before anything is spliced, so a refusal leaves the text untouched.
        if (FindProject(project).HasValue())
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' is already in the solution", project.string())};
        }

        const std::string id = NewGuid();

        auto container = FindContainer(folder);

        // A folder the caller names but the solution does not have yet is created rather
        // than refused: the folder is scaffolding for the project, not a thing the caller
        // has to build first.
        if (!container && container.GetError().code == ErrorCode::NotFound)
        {
            const auto created = AddFolder(folder);

            if (!created)
                return created.GetError();

            container = FindContainer(folder);
        }

        if (!container)
            return container.GetError();

        const std::string element =
            std::format(R"(<Project Path="{}" Id="{}" />)", relative, id);

        InsertChild(container.Value(), element);

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::RemoveProject(const std::filesystem::path& project)
    {
        const auto found = FindProject(project);

        if (!found)
            return found.GetError();

        EraseElement(m_text, found.Value(), "Project");

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::AddFolder(const std::string& folder)
    {
        const std::vector<std::string> parts = SplitLogicalPath(folder);

        if (parts.empty())
            return Error{ErrorCode::InvalidArgument, "a solution folder needs a name"};

        // Checked before anything is spliced, so a refusal leaves the text untouched.
        if (FindContainer(folder).HasValue())
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("the solution already has a folder '{}'", folder)};
        }

        // Every ancestor that is missing has to be written too: nesting lives in the name,
        // so a folder whose parent has no element of its own has nothing to hang from.
        std::vector<std::string> missing;
        std::string prefix;

        for (const std::string& part : parts)
        {
            prefix += prefix.empty() ? part : "/" + part;

            if (!FindContainer(prefix).HasValue())
                missing.push_back(prefix);
        }

        // Written deepest first, because each insertion goes in as the container's first
        // child — so the shallowest ends up first in the file, as a reader expects.
        for (std::size_t i = missing.size(); i > 0; --i)
        {
            const auto root = FindContainer("");

            if (!root)
                return root.GetError();

            InsertChild(root.Value(), std::format(R"(<Folder Name="/{}/" />)", missing[i - 1]));
        }

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::RemoveFolder(const std::string& folder)
    {
        if (folder.empty())
            return Error{ErrorCode::InvalidArgument, "the solution root is not a folder"};

        const auto found = FindContainer(folder);

        if (!found)
            return found.GetError();

        {
            Parsed parsed{m_text};

            if (!parsed.ok)
                return Error{ErrorCode::InvalidArgument, "the solution is not well-formed XML"};

            const std::string wanted = "/" + folder + "/";

            for (const pugi::xml_node& node : parsed.document.child("Solution").children("Folder"))
            {
                const std::string current = node.attribute("Name").value();

                // A folder that still holds something is refused rather than emptied by
                // implication, and the refusal names what was found so the caller can act
                // on it.
                if (current == wanted)
                {
                    const pugi::xml_node child = node.child("Project");

                    if (child)
                    {
                        return Error{ErrorCode::InvalidArgument,
                                     std::format("the folder '{}' still holds the project '{}'",
                                                 folder, child.attribute("Path").value())};
                    }

                    continue;
                }

                if (current.rfind(wanted, 0) == 0)
                {
                    return Error{ErrorCode::InvalidArgument,
                                 std::format("the folder '{}' still holds the folder '{}'", folder,
                                             current)};
                }
            }
        }

        EraseElement(m_text, found.Value(), "Folder");

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::RenameFolder(const std::string& folder, const std::string& name)
    {
        if (name.empty() || name.find('/') != std::string::npos)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' is not a usable folder name", name)};
        }

        const std::string oldPath = "/" + folder + "/";

        // The parent's path is a prefix of every descendant's, so the new prefix is the
        // old one with its last component swapped.
        const std::size_t lastSlash = folder.find_last_of('/');
        const std::string parent =
            lastSlash == std::string::npos ? std::string{} : folder.substr(0, lastSlash + 1);
        const std::string newPath = "/" + parent + name + "/";

        Parsed parsed{m_text};

        if (!parsed.ok)
            return Error{ErrorCode::InvalidArgument, "the solution is not well-formed XML"};

        // Offset of the Name attribute's value, and how long it is.
        std::vector<std::pair<std::size_t, std::size_t>> edits;
        std::vector<std::string> replacements;

        for (const pugi::xml_node& node : parsed.document.child("Solution").children("Folder"))
        {
            const std::string current = node.attribute("Name").value();

            if (current.rfind(oldPath, 0) != 0)
                continue;

            // The attribute's value is not given an offset of its own, so it is found in
            // the element's own text — the first occurrence after the element start.
            const auto elementBegin = static_cast<std::size_t>(node.offset_debug());
            const std::size_t valueBegin = m_text.find(current, elementBegin);

            if (valueBegin == std::string::npos)
                continue;

            edits.emplace_back(valueBegin, current.size());
            replacements.push_back(newPath + current.substr(oldPath.size()));
        }

        if (edits.empty())
            return Error{ErrorCode::NotFound, std::format("the solution has no folder '{}'", folder)};

        // Applied from the last to the first: replacing a shorter name with a longer one
        // shifts every offset after it, and applying them forwards would corrupt the file
        // in a way that still parses.
        for (std::size_t i = edits.size(); i > 0; --i)
        {
            const auto& [begin, length] = edits[i - 1];
            m_text.replace(begin, length, replacements[i - 1]);
        }

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::MoveProject(const std::filesystem::path& project,
                                         const std::string& folder)
    {
        const auto found = FindProject(project);

        if (!found)
            return found.GetError();

        std::string declared;
        std::string id;

        {
            Parsed parsed{m_text};

            if (!parsed.ok)
                return Error{ErrorCode::InvalidArgument, "the solution is not well-formed XML"};

            for (const pugi::xpath_node& node : parsed.document.select_nodes("//Project"))
            {
                if (static_cast<std::size_t>(node.node().offset_debug()) != found.Value())
                    continue;

                declared = node.node().attribute("Path").value();
                id = node.node().attribute("Id").value();
                break;
            }
        }

        if (declared.empty())
            declared = RelativeProjectPath(m_path, project);

        // The identifier is carried over rather than regenerated: a new one would make the
        // moved project look like a different project to anything keying off it.
        if (id.empty())
            id = NewGuid();

        // The folder is created first, so the removal below is the last thing that can go
        // wrong — and it cannot, the element having just been found.
        if (!folder.empty() && !FindContainer(folder).HasValue())
        {
            const auto created = AddFolder(folder);

            if (!created)
                return created.GetError();
        }

        const auto removed = RemoveProject(project);

        if (!removed)
            return removed.GetError();

        // Looked up again after the removal, which moved every offset after it.
        const auto container = FindContainer(folder);

        if (!container)
            return container.GetError();

        InsertChild(container.Value(), std::format(R"(<Project Path="{}" Id="{}" />)", declared, id));

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::Save()
    {
        // A scratch file to read back before anything touches the target — not the
        // temporary WriteFileAtomically itself uses below. It keeps the solution's own
        // extension, "Sample.tmp.slnx", because ReadSolution picks its parser by
        // extension and a plain ".tmp" would be refused as a format this build cannot
        // read. Collision avoidance goes through the same helper WriteFileAtomically uses
        // for its own temporary, rather than a second hand-rolled version of that check —
        // a leftover scratch file from a crashed earlier save must not be clobbered.
        std::filesystem::path stem = m_path;
        stem.replace_extension();
        const std::filesystem::path extension = m_path.extension();

        const Result<std::filesystem::path> picked = PickAvailablePath(
            [&stem, &extension](int attempt)
            {
                std::filesystem::path candidate = stem;
                candidate += attempt == 0 ? std::string{".tmp"} : std::format(".tmp{}", attempt);
                candidate += extension;
                return candidate;
            });

        if (!picked)
            return picked.GetError();

        const std::filesystem::path& scratch = picked.Value();

        {
            std::ofstream stream{scratch, std::ios::binary | std::ios::trunc};

            if (!stream.is_open())
                return Error{ErrorCode::IoFailure, std::format("cannot write '{}'", ToUtf8(scratch))};

            stream << m_text;

            if (!stream.good())
            {
                stream.close();

                std::error_code ignored;
                std::filesystem::remove(scratch, ignored);

                return Error{ErrorCode::IoFailure,
                             std::format("cannot write '{}'", ToUtf8(scratch))};
            }
        }

        // Read back with this build's own parser before the file is replaced. A write that
        // produced something unreadable therefore never reaches the file the user's Visual
        // Studio has open — the only failure in this feature that genuinely hurts.
        const auto verified = ReadSolution(scratch);

        {
            std::error_code ignored;
            std::filesystem::remove(scratch, ignored);
        }

        if (!verified)
        {
            return Error{ErrorCode::IoFailure,
                         std::format("the edited solution could not be read back: {}",
                                     verified.GetError().message)};
        }

        return WriteFileAtomically(m_path, m_text);
    }
}
