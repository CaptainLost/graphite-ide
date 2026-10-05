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
		std::string NotTextMessage(Encoding encoding, bool marked,
								   std::string_view content,
								   const std::filesystem::path& path)
		{
			const std::string name{EncodingName(encoding)};

			if (marked)
			{
				return "This file is marked " + name + ", but decoding it gives a NUL "
													   "character at byte " +
					   std::to_string(content.find('\0')) + ", and no text contains one. It is most likely UTF-32: the "
															"UTF-32 LE mark FF FE 00 00 begins with the UTF-16 LE mark, so "
															"the two cannot be told apart from the first two bytes alone. "
															"Re-save it as UTF-8 or UTF-16 to open it here: " +
					   ToUtf8(path);
			}

			return "This file carries no byte order mark, so it was taken for " + name + " from the shape of its first " + std::to_string(ProbeSampleSize) + " bytes; decoding all of it gives control characters, so it is not "
																																							 "text in that encoding after all: " +
				   ToUtf8(path);
		}

		Result<std::string> StripBomAndDecode(std::string bytes, Encoding declared,
											  std::size_t bomLength)
		{
			// Decode returns its input verbatim for every pass-through encoding, so calling
			// it unconditionally would keep a second copy of the whole file alive at once.
			if (!TransformsContent(declared))
			{
				bytes.erase(0, bomLength);
				return bytes;
			}

			return Decode(std::string_view{bytes}.substr(bomLength), declared);
		}

		bool DecodedContentIsText(std::string_view content, bool marked)
		{
			// A mark is the file stating its own encoding, so the only thing still
			// disqualifying is the one thing a mark cannot vouch for: a NUL character means
			// UTF-32 arrived as UTF-16, the UTF-32 LE mark FF FE 00 00 opening with the
			// UTF-16 LE one. Unmarked content was only guessed at from a prefix, so all of
			// it has to pass the same text-shape test the guess was drawn from.
			return marked ? content.find('\0') == std::string::npos : LooksLikeText(content);
		}

		Result<std::string> ReadWholeStream(std::ifstream& stream, std::uintmax_t reportedSize,
											const std::filesystem::path& path)
		{
			std::string content;
			content.reserve(static_cast<std::size_t>(reportedSize));

			std::array<char, 65536> chunk{};

			// gcount() decides how much was really there, not `reportedSize`: a file another
			// process is still appending to grows between the stat and this read, and a read
			// keyed to the earlier size would truncate the document silently. The ceiling
			// bounds a writer that keeps growing it faster than it can be read.
			while (stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || stream.gcount() > 0)
			{
				content.append(chunk.data(), static_cast<std::size_t>(stream.gcount()));

				if (content.size() > MaximumFileSize)
				{
					return Error{ErrorCode::InvalidArgument,
								 "File is too large to open (grew past " + std::to_string(MaximumFileSize) + " bytes while being read): " + ToUtf8(path)};
				}
			}

			if (stream.bad())
			{
				return Error{ErrorCode::IoFailure, "Cannot read file: " + ToUtf8(path)};
			}

			return content;
		}
	}

	Result<TextDocument> TextDocument::FromContent(std::string bytes, std::filesystem::path path)
	{
		// Sniffed over the same prefix ProbeFile reads: sniffing the whole file instead lets
		// the two disagree about an unmarked UTF-16 file bigger than the sample whose tail
		// holds a control character, and the probe is what decides the file may be opened.
		const Encoding declared =
			SniffEncoding(std::string_view{bytes}.substr(0, ProbeSampleSize));

		// BomLength(bytes) rather than BomLength(declared): SniffEncoding can name UTF-16
		// from the shape of the bytes alone, so an unmarked file must have nothing stripped.
		const std::size_t bomLength = BomLength(bytes);

		auto decoded = StripBomAndDecode(std::move(bytes), declared, bomLength);

		if (!decoded)
		{
			return decoded.GetError();
		}

		const std::string content = std::move(decoded).GetValue();

		if (TransformsContent(declared) && !DecodedContentIsText(content, bomLength > 0))
		{
			return Error{ErrorCode::InvalidArgument,
						 NotTextMessage(declared, bomLength > 0, content, path)};
		}

		TextDocument document;
		document.m_Path = std::move(path);
		document.m_Encoding = declared;
		document.m_HadBom = bomLength > 0;

		// Captured separately from m_Encoding: invalid UTF-8 behind a real mark demotes
		// m_Encoding to Unknown, but the mark that was there still has to go back out.
		document.m_BomEncoding = declared;

		// Only content that was not decoded can fail this: Decode returns valid UTF-8 by
		// construction, or an error.
		if (!TransformsContent(declared) && !IsValidUtf8(content))
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
		const auto status = std::filesystem::status(path, statusError);

		if (statusError || !std::filesystem::exists(status))
		{
			return Error{ErrorCode::NotFound, "No such file: " + ToUtf8(path)};
		}

		if (!std::filesystem::is_regular_file(status))
		{
			return Error{ErrorCode::InvalidArgument, "Not a file (it is a directory): " + ToUtf8(path)};
		}

		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return Error{ErrorCode::IoFailure, "Cannot read file: " + ToUtf8(path)};
		}

		// Refused before anything is allocated, so a lying or enormous size cannot force a
		// multi-gigabyte allocation and send std::bad_alloc across this Result boundary.
		if (size > MaximumFileSize)
		{
			return Error{ErrorCode::InvalidArgument, "File is too large to open (" + std::to_string(size) + " bytes): " + ToUtf8(path)};
		}

		// Read before the content, never after — see m_DiskStamp. Failing here is not fatal
		// to the load; it only costs this file its first change comparison.
		std::error_code timeError;
		const auto diskTime = std::filesystem::last_write_time(path, timeError);

		std::ifstream stream{path, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{ErrorCode::AccessDenied, "Cannot open file: " + ToUtf8(path)};
		}

		auto content = ReadWholeStream(stream, size, path);

		if (!content)
		{
			return content.GetError();
		}

		auto document = FromContent(std::move(content).GetValue(), path);

		// All of the stamp or none of it: a half-taken stamp is not "no stamp" but a stamp
		// from the epoch of a zero-byte file, which CheckChangedOnDisk() answers true to
		// forever after.
		if (document && !timeError)
		{
			document.GetValue().m_DiskStamp = DiskStamp{diskTime, size};
		}

		return document;
	}

	NormalisedLineEndings NormaliseLineEndings(std::string_view bytes)
	{
		NormalisedLineEndings result;
		result.Content.reserve(bytes.size());

		std::size_t crlfCount = 0;
		std::size_t lfCount = 0;

		for (std::size_t byteIndex = 0; byteIndex < bytes.size(); ++byteIndex)
		{
			const bool startsCrlf =
				bytes[byteIndex] == '\r' && byteIndex + 1 < bytes.size() && bytes[byteIndex + 1] == '\n';

			if (startsCrlf)
			{
				++crlfCount;
				result.Content.push_back('\n');
				++byteIndex;
				continue;
			}

			if (bytes[byteIndex] == '\n')
			{
				++lfCount;
			}

			result.Content.push_back(bytes[byteIndex]);
		}

		// A lone '\r' stays: taken for a classic Mac line end, it would split a line at a stray byte.
		result.Mixed = crlfCount > 0 && lfCount > 0;
		result.Ending = crlfCount > lfCount ? LineEnding::Crlf : LineEnding::Lf;

		return result;
	}

	std::string WithLineEnding(std::string_view text, LineEnding ending)
	{
		if (ending == LineEnding::Lf)
		{
			return std::string{text};
		}

		std::string result;
		result.reserve(text.size());

		for (const char byte : text)
		{
			if (byte == '\n')
			{
				result.push_back('\r');
			}

			result.push_back(byte);
		}

		return result;
	}

	std::string TextDocument::EncodeToBytes() const
	{
		std::string result;

		// Keyed off m_HadBom rather than m_Encoding: invalid UTF-8 following a mark demotes
		// m_Encoding to Unknown, and the bytes still have to round-trip.
		if (m_HadBom)
		{
			result += BomBytes(m_BomEncoding);
		}

		result += Encode(WithLineEnding(m_Buffer.Materialize(), m_LineEnding), m_Encoding);

		return result;
	}

	bool TextDocument::CheckChangedOnDisk() const
	{
		// Nothing was ever recorded to compare against, and every caller treats this answer
		// as a fact about the file rather than as a guess.
		if (!m_DiskStamp)
		{
			return false;
		}

		std::error_code timeError;
		const auto time = std::filesystem::last_write_time(m_Path, timeError);

		if (timeError)
		{
			return false;
		}

		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(m_Path, sizeError);

		if (sizeError)
		{
			return false;
		}

		return time != m_DiskStamp->Time || size != m_DiskStamp->Size;
	}

	Result<void> TextDocument::Save()
	{
		auto written = WriteFileAtomically(m_Path, EncodeToBytes());

		if (!written)
		{
			return written.GetError();
		}

		MarkSaved();
		m_Buffer.BreakUndoGroup();
		AcceptDiskState();

		return {};
	}

	Result<void> TextDocument::ReloadFromDisk()
	{
		auto fresh = Load(m_Path);

		if (!fresh)
		{
			return fresh.GetError();
		}

		TextDocument freshDocument = std::move(fresh).GetValue();
		const std::string freshText = freshDocument.m_Buffer.Materialize();

		// Bracketed on both sides so one Ctrl+Z reverses exactly the reload and none of the
		// typing around it.
		m_Buffer.BreakUndoGroup();
		m_Buffer.BeginTransaction();
		m_Buffer.Erase(0, m_Buffer.GetLength());
		m_Buffer.Insert(0, freshText);
		m_Buffer.EndTransaction();
		m_Buffer.BreakUndoGroup();

		m_Encoding = freshDocument.m_Encoding;
		m_LineEnding = freshDocument.m_LineEnding;
		m_WasMixed = freshDocument.m_WasMixed;
		m_ReadOnly = freshDocument.m_ReadOnly;
		m_HadBom = freshDocument.m_HadBom;
		m_BomEncoding = freshDocument.m_BomEncoding;

		MarkSaved();

		// The buffer now holds the fresh load's content, so it keeps the fresh load's stamp
		// — including no stamp at all.
		m_DiskStamp = freshDocument.m_DiskStamp;

		return {};
	}

	void TextDocument::AcceptDiskState()
	{
		std::error_code timeError;
		const auto time = std::filesystem::last_write_time(m_Path, timeError);

		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(m_Path, sizeError);

		// Both or neither: a new time beside an old size describes a file that never
		// existed, so a failed read leaves the previous stamp and the question is asked
		// again.
		if (timeError || sizeError)
		{
			return;
		}

		m_DiskStamp = DiskStamp{time, size};
	}
}
