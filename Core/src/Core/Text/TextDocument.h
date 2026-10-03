#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	enum class LineEnding
	{
		Lf,
		Crlf
	};

	std::string WithLineEnding(std::string_view text, LineEnding ending);

	class TextDocument
	{
	public:
		static Result<TextDocument> FromContent(std::string bytes,
												std::filesystem::path path);

		static Result<TextDocument> Load(const std::filesystem::path& path);

		TextBuffer& GetBuffer() { return m_Buffer; }
		const TextBuffer& GetBuffer() const { return m_Buffer; }

		const std::filesystem::path& GetPath() const { return m_Path; }

		void SetPath(std::filesystem::path path) { m_Path = std::move(path); }

		Encoding GetFileEncoding() const { return m_Encoding; }

		LineEnding GetDocumentLineEnding() const { return m_LineEnding; }

		bool WasMixed() const { return m_WasMixed; }

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
		bool m_HadBom = false;
		Encoding m_BomEncoding = Encoding::Utf8;
		std::size_t m_SavedRevision = 0;

		struct DiskStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;
		};

		// Read BEFORE the content, never after: a stamp taken afterwards records a state
		// newer than what was read, and that change is then lost for good.
		std::optional<DiskStamp> m_DiskStamp;
	};
}