#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Core {

	enum class LineEnding
	{
		Lf,
		Crlf
	};

	std::string WithLineEnding(std::string_view text, LineEnding ending);

	struct NormalisedLineEndings
	{
		std::string Content;
		LineEnding Ending = LineEnding::Lf;
		bool Mixed = false;
	};

	NormalisedLineEndings NormaliseLineEndings(std::string_view text);

	class TextDocument
	{
	public:
		static Result<TextDocument> FromContent(std::string bytes, std::filesystem::path path);

		static Result<TextDocument> Load(const std::filesystem::path& path);

		TextBuffer& GetBuffer() { return m_Buffer; }
		const TextBuffer& GetBuffer() const { return m_Buffer; }

		const std::filesystem::path& GetPath() const { return m_Path; }

		void SetPath(std::filesystem::path path) { m_Path = std::move(path); }

		Encoding GetFileEncoding() const { return m_Encoding; }

		LineEnding GetDocumentLineEnding() const { return m_LineEnding; }

		bool HasMixedLineEndings() const { return m_WasMixed; }

		bool IsReadOnly() const { return m_ReadOnly; }

		std::string EncodeToBytes() const;

		bool IsModified() const
		{
			return m_Buffer.GetRevision() != m_SavedRevision;
		}

		void MarkSaved() { m_SavedRevision = m_Buffer.GetRevision(); }

		bool CheckChangedOnDisk() const;

		Result<void> Save();

		Result<void> ReloadFromDisk();

		void AcceptDiskState();

	private:
		TextBuffer m_Buffer;
		std::filesystem::path m_Path;
		Encoding m_Encoding = Encoding::Utf8;
		LineEnding m_LineEnding = LineEnding::Lf;
		bool m_WasMixed = false;
		bool m_ReadOnly = false;
		// Kept apart from m_Encoding: invalid UTF-8 behind a mark demotes m_Encoding to Unknown,
		// and the mark still has to be written back for the bytes to round-trip.
		bool m_HadByteOrderMark = false;
		Encoding m_ByteOrderMarkEncoding = Encoding::Utf8;
		std::size_t m_SavedRevision = 0;

		struct DiskStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;
		};

		// Taken before the content is read, never after: a later stamp describes a newer file than
		// the one read, and that change is lost for good. It is whole or absent, because a default
		// time beside a real size compares as changed forever.
		std::optional<DiskStamp> m_DiskStamp;
	};
}