#include "corepch.h"

#include "Core/Sln/SlnxEditor.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Sln/SolutionReader.h"
#include "Core/Sln/TextEdit.h"

#include <pugixml.hpp>

#include <fstream>
#include <system_error>
#include <vector>

namespace core::sln
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
            std::string text = fs::ToUtf8(project.lexically_relative(solution.parent_path()));

            for (char& character : text)
            {
                if (character == '/')
                    character = '\\';
            }

            return text;
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
            return Error{ErrorCode::InvalidArgument, "the solution can no longer be parsed"};

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
                (directory / fs::FromUtf8(declared)).lexically_normal();
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
            return Error{ErrorCode::InvalidArgument, "the solution can no longer be parsed"};

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
        const std::string ending = text::LineEnding(m_text);
        const std::string unit = text::IndentUnit(m_text);

        // Indented one level past the container's own line.
        const std::size_t lineBegin = text::LineExtent(m_text, offset).first;
        std::string containerIndent;

        for (std::size_t i = lineBegin;
             i < m_text.size() && (m_text[i] == ' ' || m_text[i] == '\t'); ++i)
        {
            containerIndent += m_text[i];
        }

        const std::size_t tagEnd = text::OpeningTagEnd(m_text, offset);

        // A container with no children yet is written self-closing, so it has to become an
        // open/close pair before anything can go inside it.
        if (tagEnd >= 2 && m_text[tagEnd - 2] == '/')
        {
            // Name it from the text rather than guessing: this is used for both Solution
            // and Folder.
            std::size_t nameEnd = offset;

            while (nameEnd < m_text.size() && m_text[nameEnd] != ' ' && m_text[nameEnd] != '\t'
                   && m_text[nameEnd] != '>' && m_text[nameEnd] != '/')
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

        const auto container = FindContainer(folder);

        if (!container)
            return container.GetError();

        const std::string element = std::format(R"(<Project Path="{}" Id="{}" />)",
                                                RelativeProjectPath(m_path, project), NewGuid());

        InsertChild(container.Value(), element);

        return Result<void>::Ok();
    }

    Result<void> SlnxEditor::RemoveProject(const std::filesystem::path& project)
    {
        const auto found = FindProject(project);

        if (!found)
            return found.GetError();

        const std::size_t begin = found.Value() - 1;   // back over the '<'
        const std::size_t end = text::ElementEnd(m_text, found.Value(), "Project");

        // The whole line goes, so no blank one is left behind.
        const auto [lineBegin, lineEnd] = text::LineExtent(m_text, begin);
        const bool aloneOnItsLine = m_text.find_first_not_of(" \t", lineBegin) == begin
                                    && m_text.find_first_not_of(" \t\r\n", end) >= lineEnd;

        if (aloneOnItsLine)
            m_text.erase(lineBegin, lineEnd - lineBegin);
        else
            m_text.erase(begin, end - begin);

        return Result<void>::Ok();
    }

    // The four folder operations arrive with their tests in the next task. They are
    // declared now so the menu can be written against a complete interface.
    Result<void> SlnxEditor::AddFolder(const std::string& folder)
    {
        (void)folder;

        return Error{ErrorCode::Unsupported, "adding a solution folder is not implemented yet"};
    }

    Result<void> SlnxEditor::RemoveFolder(const std::string& folder)
    {
        (void)folder;

        return Error{ErrorCode::Unsupported, "removing a solution folder is not implemented yet"};
    }

    Result<void> SlnxEditor::RenameFolder(const std::string& folder, const std::string& name)
    {
        (void)folder;
        (void)name;

        return Error{ErrorCode::Unsupported, "renaming a solution folder is not implemented yet"};
    }

    Result<void> SlnxEditor::MoveProject(const std::filesystem::path& project,
                                         const std::string& folder)
    {
        (void)project;
        (void)folder;

        return Error{ErrorCode::Unsupported, "moving a project is not implemented yet"};
    }

    Result<void> SlnxEditor::Save()
    {
        // The temporary keeps the solution's own extension — "Sample.tmp.slnx" — because
        // the read-back below picks its parser by extension, and a plain ".tmp" would be
        // refused as a format this build cannot read.
        std::filesystem::path temporary = m_path;
        temporary.replace_extension();
        temporary += ".tmp";
        temporary += m_path.extension();

        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};

            if (!stream.is_open())
            {
                return Error{ErrorCode::IoFailure,
                             std::format("cannot write '{}'", temporary.string())};
            }

            stream << m_text;

            if (!stream.good())
            {
                stream.close();

                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);

                return Error{ErrorCode::IoFailure,
                             std::format("cannot write '{}'", temporary.string())};
            }
        }

        // Read back with this build's own parser before the file is replaced. A write that
        // produced something unreadable therefore never reaches the file the user's Visual
        // Studio has open — the only failure in this feature that genuinely hurts.
        const auto verified = ReadSolution(temporary);

        if (!verified)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);

            return Error{ErrorCode::IoFailure,
                         std::format("the edited solution could not be read back: {}",
                                     verified.GetError().message)};
        }

        std::error_code code;
        std::filesystem::rename(temporary, m_path, code);

        if (code)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);

            return Error{ErrorCode::IoFailure,
                         std::format("cannot replace '{}'", m_path.string())};
        }

        return Result<void>::Ok();
    }
}
