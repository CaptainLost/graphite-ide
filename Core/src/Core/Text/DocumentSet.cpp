#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Columns.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/Indent.h"

#include <algorithm>
#include <utility>

namespace core::text
{
    namespace
    {
        [[nodiscard]] std::string Megabytes(std::uint64_t bytes)
        {
            return std::to_string(bytes / (1024 * 1024)) + " MB";
        }

        [[nodiscard]] std::string Kilobytes(std::uint64_t bytes)
        {
            return std::to_string(bytes / 1024) + " KB";
        }
    }

    std::string FailureMessage(const OpenItem& item)
    {
        if (!item.detail.empty())
            return item.detail;

        switch (item.failure)
        {
        case OpenFailure::Binary:
            return "Binary file — " + Kilobytes(item.size);
        case OpenFailure::TooLarge:
            return "This file is " + Megabytes(item.size) + ", too large to open";
        case OpenFailure::UnsupportedEncoding:
            return std::string{EncodingName(item.encoding)} + " file, not supported yet";
        case OpenFailure::Missing:
            return "This file no longer exists on disk";
        case OpenFailure::ReadFailed:
            return "This file could not be read";
        case OpenFailure::None:
            break;
        }

        return {};
    }

    std::optional<std::size_t> DocumentSet::Find(const std::filesystem::path& path) const
    {
        for (std::size_t index = 0; index < m_items.size(); ++index)
        {
            if (m_items[index].path == path)
                return index;
        }

        return std::nullopt;
    }

    void DocumentSet::Activate(std::size_t index)
    {
        if (index < m_items.size())
            m_active = index;
    }

    OpenOutcome DocumentSet::Open(const std::filesystem::path& path, bool permanent)
    {
        return Add(path, permanent, false);
    }

    OpenOutcome DocumentSet::OpenConfirmed(const std::filesystem::path& path, bool permanent)
    {
        return Add(path, permanent, true);
    }

    OpenOutcome DocumentSet::Add(const std::filesystem::path& path, bool permanent, bool confirmed)
    {
        if (const auto existing = Find(path))
        {
            Activate(*existing);

            if (permanent)
                m_items[*existing].preview = false;

            return OpenOutcome::Activated;
        }

        // At most one preview at a time: a single click replaces the last one rather than
        // stacking tabs up. Called right before an item is actually pushed — not any
        // earlier — so a path that adds nothing (NeedsConfirmation, answered by never
        // calling OpenConfirmed) never costs the existing preview its slot.
        const auto evictPreview = [this, permanent]
        {
            if (!permanent)
            {
                if (const auto preview = Preview())
                    Close(*preview);
            }
        };

        OpenItem item;
        item.path = path;
        item.preview = !permanent;

        const auto probe = ProbeFile(path);

        if (!probe)
        {
            // NotFound is the only code that actually means "gone" — a directory, a
            // locked file and an unmeasurable one are all still there, so claiming
            // otherwise would send the user looking for a file sitting right in front
            // of them. ProbeFile's own message says which one it was.
            item.failure = probe.GetError().code == core::ErrorCode::NotFound
                               ? OpenFailure::Missing
                               : OpenFailure::ReadFailed;
            item.detail = probe.GetError().message;

            evictPreview();
            m_items.push_back(std::move(item));
            Activate(m_items.size() - 1);

            return OpenOutcome::Failed;
        }

        item.size = probe.Value().size;
        item.encoding = probe.Value().encoding;

        // Asking has to happen before anything is added, so answering "no" leaves no
        // trace of the question — including the preview slot, which stays exactly as it
        // was since evictPreview has not run yet.
        if (probe.Value().kind == FileKind::LargeText && !confirmed)
            return OpenOutcome::NeedsConfirmation;

        switch (probe.Value().kind)
        {
        case FileKind::Binary:
            item.failure = OpenFailure::Binary;
            break;
        case FileKind::TooLarge:
            item.failure = OpenFailure::TooLarge;
            break;
        case FileKind::Text:
        case FileKind::LargeText:
            if (item.encoding == Encoding::Utf16Le || item.encoding == Encoding::Utf16Be)
            {
                item.failure = OpenFailure::UnsupportedEncoding;
                break;
            }

            {
                auto document = TextDocument::Load(path);

                if (document)
                {
                    item.document = std::move(document).Value();
                    item.widestColumn =
                        WidestColumn(item.document->Buffer(), DefaultTabWidth);
                    item.indentDepths =
                        IndentDepths(item.document->Buffer(), DefaultTabWidth);
                }
                else
                {
                    item.failure = OpenFailure::ReadFailed;
                }
            }
            break;
        }

        const bool failed = !item.document.has_value();

        evictPreview();
        m_items.push_back(std::move(item));
        Activate(m_items.size() - 1);

        return failed ? OpenOutcome::Failed : OpenOutcome::Opened;
    }

    void DocumentSet::Close(std::size_t index)
    {
        if (index >= m_items.size())
            return;

        m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));

        if (m_items.empty())
        {
            m_active.reset();
            return;
        }

        if (!m_active.has_value())
            return;

        // An item before the active one shifts it down; closing the active one falls back
        // to whatever now occupies that slot, or the last item when it was the last.
        if (*m_active > index)
            m_active = *m_active - 1;
        else if (*m_active == index)
            m_active = std::min(index, m_items.size() - 1);
    }

    void DocumentSet::CloseAll()
    {
        m_items.clear();
        m_active.reset();
    }

    std::optional<std::size_t> DocumentSet::Preview() const
    {
        for (std::size_t index = 0; index < m_items.size(); ++index)
        {
            if (m_items[index].preview)
                return index;
        }

        return std::nullopt;
    }

    void DocumentSet::Promote(std::size_t index)
    {
        if (index < m_items.size())
            m_items[index].preview = false;
    }

    std::string DocumentSet::DisplayTitle(std::size_t index) const
    {
        if (index >= m_items.size())
            return {};

        const std::filesystem::path& path = m_items[index].path;
        const std::string name = fs::ToUtf8(path.filename());

        for (std::size_t other = 0; other < m_items.size(); ++other)
        {
            if (other == index)
                continue;

            if (fs::ToUtf8(m_items[other].path.filename()) != name)
                continue;

            // Forward slash regardless of platform: this is a label, not a path anyone
            // will open.
            return fs::ToUtf8(path.parent_path().filename()) + "/" + name;
        }

        return name;
    }

    void DocumentSet::RewritePaths(const std::filesystem::path& from,
                                   const std::filesystem::path& to)
    {
        for (OpenItem& item : m_items)
        {
            const auto relative = item.path.lexically_relative(from);

            // lexically_relative yields a path starting with ".." when the target is not
            // below `from`, and an empty path when it cannot express the relationship.
            if (relative.empty() || *relative.begin() == "..")
                continue;

            // relative is "." when the item path equals `from` exactly — an item can be
            // the workspace root itself (opening a directory produces a ReadFailed item
            // at that path). `to / "."` would keep a trailing separator-dot that compares
            // unequal to `to` itself, breaking Find and duplicate detection, so that case
            // becomes `to` directly instead of going through the append.
            item.path = (relative == ".") ? to : to / relative;
        }
    }

    void DocumentSet::MarkMissing(const std::filesystem::path& path)
    {
        const auto index = Find(path);

        if (!index)
            return;

        m_items[*index].failure = OpenFailure::Missing;

        // Whatever detail the previous failure carried — a sharing violation, an
        // unreadable directory — no longer describes what is wrong. Left in place,
        // FailureMessage would keep preferring it over "This file no longer exists".
        m_items[*index].detail.clear();
    }
}
