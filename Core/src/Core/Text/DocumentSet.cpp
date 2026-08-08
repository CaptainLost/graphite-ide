#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/FileProbe.h"

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

        OpenItem item;
        item.path = path;
        item.preview = !permanent;

        const auto probe = ProbeFile(path);

        if (!probe)
        {
            item.failure = OpenFailure::Missing;
            m_items.push_back(std::move(item));
            Activate(m_items.size() - 1);

            return OpenOutcome::Failed;
        }

        item.size = probe.Value().size;
        item.encoding = probe.Value().encoding;

        // Asking has to happen before anything is added, so answering "no" leaves no
        // trace of the question.
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
                    item.document = std::move(document).Value();
                else
                    item.failure = OpenFailure::ReadFailed;
            }
            break;
        }

        const bool failed = !item.document.has_value();

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
}
