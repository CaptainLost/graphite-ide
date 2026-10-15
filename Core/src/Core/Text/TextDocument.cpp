#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Core {

	namespace {
		constexpr std::size_t kReadChunkBytes = 64 * 1024;

		std::string NotTextMessage(Encoding encoding, bool hasByteOrderMark, std::string_view content,
								   const std::filesystem::path& path)
		{
			const std::string name{EncodingName(encoding)};

			if (hasByteOrderMark)
			{
				std::string message = "This file is marked " + name;
				message += ", but decoding it gives a NUL "
						   "character at byte ";
				message += std::to_string(content.find('\0'));
				message += ", and no text contains one. It is most likely UTF-32: the "
						   "UTF-32 LE mark FF FE 00 00 begins with the UTF-16 LE mark, so "
						   "the two cannot be told apart from the first two bytes alone. "
						   "Re-save it as UTF-8 or UTF-16 to open it here: ";
				message += ToUtf8(path);

				return message;
			}

			std::string message = "This file carries no byte order mark, so it was taken for " + name;
			message += " from the shape of its first ";
			message += std::to_string(ProbeSampleSize);
			message += " bytes; decoding all of it gives control characters, so it is not "
					   "text in that encoding after all: ";
			message += ToUtf8(path);

			return message;
		}

		// A NUL is the one thing a mark cannot vouch for: the UTF-32 LE mark FF FE 00 00 begins with
		// the UTF-16 LE mark, so a UTF-32 file arrives here decoded as UTF-16 and full of NULs.
		bool DecodedContentIsText(std::string_view content, bool hasByteOrderMark)
		{
			if (hasByteOrderMark)
			{
				const bool containsNul = content.find('\0') != std::string_view::npos;
				return !containsNul;
			}

			return LooksLikeText(content);
		}

		// Reads until the stream runs dry rather than for `reportedSize` bytes: a file another process
		// is still appending to grows between the stat and this read, and stopping at the earlier
		// size would truncate the document silently.
		Result<std::string> ReadWholeStream(std::ifstream& stream, std::uintmax_t reportedSize,
											const std::filesystem::path& path)
		{
			std::string fileBytes;
			fileBytes.reserve(static_cast<std::size_t>(reportedSize));

			std::array<char, kReadChunkBytes> chunk{};

			while (true)
			{
				stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
				const std::streamsize bytesRead = stream.gcount();

				const bool nothingLeftToRead = stream.fail() && bytesRead == 0;
				if (nothingLeftToRead)
				{
					break;
				}

				fileBytes.append(chunk.data(), static_cast<std::size_t>(bytesRead));

				const bool grewPastMaximumWhileBeingRead = fileBytes.size() > MaximumFileSize;
				if (grewPastMaximumWhileBeingRead)
				{
					return Error{.Code = ErrorCode::InvalidArgument,
								 .Message = "File is too large to open (grew past " + std::to_string(MaximumFileSize) + " bytes while being read): " + ToUtf8(path)};
				}
			}

			if (stream.bad())
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "Cannot read file: " + ToUtf8(path)};
			}

			return fileBytes;
		}
	}

	Result<TextDocument> TextDocument::FromContent(std::string bytes, std::filesystem::path path)
	{
		const std::string_view sampleProbeFileReads = std::string_view{bytes}.substr(0, ProbeSampleSize);
		const Encoding sniffedEncoding = SniffEncoding(sampleProbeFileReads);

		// Measured on the bytes, never taken from the sniffed encoding: SniffEncoding can name UTF-16
		// for bytes that carry no mark at all.
		const bool hasByteOrderMark = LeadingByteOrderMarkLength(bytes) > 0;

		Result<std::string> decoded = DecodeWithoutByteOrderMark(std::move(bytes), sniffedEncoding);

		if (!decoded)
		{
			return decoded.GetError();
		}

		const std::string content = std::move(decoded).GetValue();
		const bool contentWasDecoded = TransformsContent(sniffedEncoding);

		if (contentWasDecoded && !DecodedContentIsText(content, hasByteOrderMark))
		{
			return Error{.Code = ErrorCode::InvalidArgument,
						 .Message = NotTextMessage(sniffedEncoding, hasByteOrderMark, content, path)};
		}

		TextDocument document;
		document.m_Path = std::move(path);
		document.m_Encoding = sniffedEncoding;
		document.m_HadByteOrderMark = hasByteOrderMark;
		document.m_ByteOrderMarkEncoding = sniffedEncoding;

		const bool undecodedContentIsInvalidUtf8 = !contentWasDecoded && !IsValidUtf8(content);
		if (undecodedContentIsInvalidUtf8)
		{
			document.m_Encoding = Encoding::Unknown;
			document.m_ReadOnly = true;
		}

		NormalisedLineEndings normalised = NormaliseLineEndings(content);
		document.m_LineEnding = normalised.Ending;
		document.m_WasMixed = normalised.Mixed;
		document.m_Buffer = TextBuffer{std::move(normalised.Content)};

		return document;
	}

	Result<TextDocument> TextDocument::Load(const std::filesystem::path& path)
	{
		std::error_code statusError;
		const std::filesystem::file_status status = std::filesystem::status(path, statusError);

		if (statusError || !std::filesystem::exists(status))
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "No such file: " + ToUtf8(path)};
		}

		if (!std::filesystem::is_regular_file(status))
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "Not a file (it is a directory): " + ToUtf8(path)};
		}

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Cannot read file: " + ToUtf8(path)};
		}

		// Refused before the read reserves `fileSize` bytes, so a lying or enormous size cannot throw
		// std::bad_alloc across this Result boundary.
		if (fileSize > MaximumFileSize)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "File is too large to open (" + std::to_string(fileSize) + " bytes): " + ToUtf8(path)};
		}

		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTimeBeforeRead = std::filesystem::last_write_time(path, writeTimeError);

		std::ifstream stream{path, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{.Code = ErrorCode::AccessDenied, .Message = "Cannot open file: " + ToUtf8(path)};
		}

		Result<std::string> fileBytes = ReadWholeStream(stream, fileSize, path);

		if (!fileBytes)
		{
			return fileBytes.GetError();
		}

		Result<TextDocument> document = FromContent(std::move(fileBytes).GetValue(), path);

		// A write time that cannot be read costs the document its stamp, not the load; no test can make last_write_time fail here.
		const bool writeTimeWasRead = !writeTimeError;
		if (document && writeTimeWasRead)
		{
			document.GetValue().m_DiskStamp = DiskStamp{.Time = writeTimeBeforeRead, .Size = fileSize};
		}

		return document;
	}

	// A lone '\r' stays: taken for a classic Mac line end, it would split a line at a stray byte.
	NormalisedLineEndings NormaliseLineEndings(std::string_view text)
	{
		NormalisedLineEndings normalised;
		normalised.Content.reserve(text.size());

		std::size_t crlfCount = 0;
		std::size_t lfCount = 0;

		for (std::size_t byteIndex = 0; byteIndex < text.size(); ++byteIndex)
		{
			const bool startsCrlf =
				text[byteIndex] == '\r' && byteIndex + 1 < text.size() && text[byteIndex + 1] == '\n';

			if (startsCrlf)
			{
				++crlfCount;
				normalised.Content.push_back('\n');
				++byteIndex;
				continue;
			}

			if (text[byteIndex] == '\n')
			{
				++lfCount;
			}

			normalised.Content.push_back(text[byteIndex]);
		}

		normalised.Mixed = crlfCount > 0 && lfCount > 0;
		normalised.Ending = crlfCount > lfCount ? LineEnding::Crlf : LineEnding::Lf;

		return normalised;
	}

	std::string WithLineEnding(std::string_view text, LineEnding ending)
	{
		if (ending == LineEnding::Lf)
		{
			return std::string{text};
		}

		std::string textWithCrlf;
		textWithCrlf.reserve(text.size());

		for (const char byte : text)
		{
			if (byte == '\n')
			{
				textWithCrlf.push_back('\r');
			}

			textWithCrlf.push_back(byte);
		}

		return textWithCrlf;
	}

	std::string TextDocument::EncodeToBytes() const
	{
		std::string encodedBytes;

		if (m_HadByteOrderMark)
		{
			encodedBytes += ByteOrderMarkBytes(m_ByteOrderMarkEncoding);
		}

		encodedBytes += Encode(WithLineEnding(m_Buffer.Materialize(), m_LineEnding), m_Encoding);

		return encodedBytes;
	}

	bool TextDocument::CheckChangedOnDisk() const
	{
		if (!m_DiskStamp)
		{
			return false;
		}

		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(m_Path, writeTimeError);

		if (writeTimeError)
		{
			return false;
		}

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(m_Path, sizeError);

		if (sizeError)
		{
			return false;
		}

		return writeTime != m_DiskStamp->Time || fileSize != m_DiskStamp->Size;
	}

	Result<void> TextDocument::Save()
	{
		const Result<void> writeOutcome = WriteFileAtomically(m_Path, EncodeToBytes());

		if (!writeOutcome)
		{
			return writeOutcome.GetError();
		}

		MarkSaved();
		m_Buffer.BreakUndoGroup();
		AcceptDiskState();

		return {};
	}

	Result<void> TextDocument::ReloadFromDisk()
	{
		Result<TextDocument> fresh = Load(m_Path);

		if (!fresh)
		{
			return fresh.GetError();
		}

		TextDocument freshDocument = std::move(fresh).GetValue();
		const std::string freshText = freshDocument.m_Buffer.Materialize();

		m_Buffer.ReplaceContent(freshText);

		m_Encoding = freshDocument.m_Encoding;
		m_LineEnding = freshDocument.m_LineEnding;
		m_WasMixed = freshDocument.m_WasMixed;
		m_ReadOnly = freshDocument.m_ReadOnly;
		m_HadByteOrderMark = freshDocument.m_HadByteOrderMark;
		m_ByteOrderMarkEncoding = freshDocument.m_ByteOrderMarkEncoding;

		MarkSaved();

		// Copied even when the fresh load took no stamp: the old stamp describes content the buffer
		// no longer holds.
		m_DiskStamp = freshDocument.m_DiskStamp;

		return {};
	}

	void TextDocument::AcceptDiskState()
	{
		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(m_Path, writeTimeError);

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(m_Path, sizeError);

		const bool stampWasReadWhole = !writeTimeError && !sizeError;
		// A failed read keeps the previous stamp instead of clearing it, because a document with no stamp never reports a change on disk.
		if (!stampWasReadWhole)
		{
			return;
		}

		m_DiskStamp = DiskStamp{.Time = writeTime, .Size = fileSize};
	}
}
