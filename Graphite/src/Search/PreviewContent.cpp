#include "Search/PreviewContent.h"

#include "Core/Text/Codec.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace Graphite {

	namespace {

		// The same read the search itself does, deliberately: a preview that decoded a file
		// differently from the search that found the hit would disagree with the row above it.
		std::optional<std::string> ReadAll(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return std::nullopt;
			}

			return std::string(std::istreambuf_iterator<char>(in),
							   std::istreambuf_iterator<char>());
		}
	}

	void PreviewContent::Show(const std::filesystem::path& file, const Core::DocumentSet& documents)
	{
		if (file != m_Path)
		{
			m_Path = file;
			m_Loaded = Core::TextBuffer{};
			m_Buffer = nullptr;
			m_Message.clear();
			m_State = PreviewState::Empty;
		}

		// Re-resolved on every call rather than remembered: the set reallocates as tabs open
		// and close, so a pointer kept from last frame may not point at a document any more —
		// and a file opened since the last frame has to start winning over the copy on disk.
		if (const std::optional<std::size_t> index = documents.Find(m_Path))
		{
			const Core::OpenItem& item = documents.At(*index);

			if (item.Document)
			{
				m_Buffer = &item.Document->GetBuffer();
				m_Message.clear();
				m_State = PreviewState::Ready;
				return;
			}
		}

		// Already have our own copy of this file and nothing has opened it since. The path did
		// not change, so the copy is still of the right file.
		if (m_State == PreviewState::Ready && m_Buffer == &m_Loaded)
		{
			return;
		}

		LoadFromDisk();
	}

	void PreviewContent::Clear()
	{
		m_Path.clear();
		m_Loaded = Core::TextBuffer{};
		m_Buffer = nullptr;
		m_Message.clear();
		m_State = PreviewState::Empty;
	}

	void PreviewContent::LoadFromDisk()
	{
		m_Buffer = nullptr;

		const Core::Result<Core::FileProbe> probed = Core::ProbeFile(m_Path);

		if (!probed)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot read this file.";
			return;
		}

		const Core::FileProbe& probe = probed.GetValue();

		if (probe.Kind == Core::FileKind::Binary || probe.Kind == Core::FileKind::TooLarge)
		{
			m_State = PreviewState::Skipped;
			m_Message = probe.Kind == Core::FileKind::Binary ? "Binary file." : "File too large.";
			return;
		}

		const std::optional<std::string> bytes = ReadAll(m_Path);

		if (!bytes)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot read this file.";
			return;
		}

		const Core::Result<std::string> text = Core::Decode(*bytes, probe.FileEncoding);

		if (!text)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot decode this file.";
			return;
		}

		m_Loaded = Core::TextBuffer{text.GetValue()};
		m_Buffer = &m_Loaded;
		m_State = PreviewState::Ready;
		m_Message.clear();
	}
}
