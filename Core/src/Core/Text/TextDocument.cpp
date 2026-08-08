#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/Fs/FileSystem.h"

#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace core::text
{
    namespace
    {
        struct Normalised
        {
            std::string content;
            LineEnding lineEnding = LineEnding::Lf;
            bool mixed = false;
        };

        /// Turns every CRLF into LF and reports which style the file mostly used.
        Normalised NormaliseLineEndings(std::string_view bytes)
        {
            Normalised result;
            result.content.reserve(bytes.size());

            std::size_t crlf = 0;
            std::size_t lf = 0;

            for (std::size_t at = 0; at < bytes.size(); ++at)
            {
                if (bytes[at] == '\r' && at + 1 < bytes.size() && bytes[at + 1] == '\n')
                {
                    ++crlf;
                    result.content.push_back('\n');
                    ++at;
                    continue;
                }

                if (bytes[at] == '\n')
                    ++lf;

                result.content.push_back(bytes[at]);
            }

            // A lone '\r' — a Mac Classic line ending — is left alone rather than
            // guessed at. Nothing in the last twenty years writes them, and treating a
            // stray one as a line break would split a line for no reason.
            result.mixed = crlf > 0 && lf > 0;
            result.lineEnding = crlf > lf ? LineEnding::Crlf : LineEnding::Lf;

            return result;
        }
    }

    Result<TextDocument> TextDocument::FromContent(std::string bytes, std::filesystem::path path)
    {
        const Encoding declared = DetectEncoding(bytes);

        if (declared == Encoding::Utf16Le || declared == Encoding::Utf16Be)
        {
            return Error{ErrorCode::Unsupported,
                         "Cannot open a " + std::string{EncodingName(declared)}
                             + " file yet: " + fs::ToUtf8(path)};
        }

        const std::size_t bomLength = BomLength(bytes);
        const std::string_view content = std::string_view{bytes}.substr(bomLength);

        TextDocument document;
        document.m_path = std::move(path);
        document.m_encoding = declared;
        document.m_hadBom = bomLength > 0;

        if (!IsValidUtf8(content))
        {
            document.m_encoding = Encoding::Unknown;
            document.m_readOnly = true;
        }

        Normalised normalised = NormaliseLineEndings(content);
        document.m_lineEnding = normalised.lineEnding;
        document.m_wasMixed = normalised.mixed;
        document.m_buffer = TextBuffer{std::move(normalised.content)};

        return document;
    }

    Result<TextDocument> TextDocument::Load(const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};

        if (!stream.is_open())
            return Error{ErrorCode::NotFound, "Cannot open file: " + fs::ToUtf8(path)};

        // Reading via `buffer << stream.rdbuf()` would not do: a mid-read I/O failure
        // sets badbit on the destination ostringstream, not on `stream`, so the error
        // would go unnoticed and a truncated read would be treated as a complete file.
        // Reading the known size through the ifstream's own read() keeps the failure on
        // the object we actually check.
        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

        if (sizeError)
            return Error{ErrorCode::IoFailure, "Cannot read file: " + fs::ToUtf8(path)};

        std::string content(static_cast<std::size_t>(size), '\0');

        if (size > 0)
        {
            stream.read(content.data(), static_cast<std::streamsize>(size));

            // Trust only a read that filled the whole size seen above; a legitimately
            // empty file never reaches this branch, so this cannot mistake "nothing to
            // read" for failure.
            if (stream.bad() || static_cast<std::uintmax_t>(stream.gcount()) != size)
                return Error{ErrorCode::IoFailure, "Cannot read file: " + fs::ToUtf8(path)};
        }

        return FromContent(std::move(content), path);
    }

    std::string TextDocument::Bytes() const
    {
        const std::string content = m_buffer.Text();

        std::string result;
        result.reserve(content.size() + 3);

        // Keyed off m_hadBom rather than m_encoding == Utf8Bom: invalid UTF-8 following a
        // mark demotes m_encoding to Unknown, but the mark itself still arrived and still
        // has to go back, or a read-only file's bytes would not round-trip.
        if (m_hadBom)
            result += "\xEF\xBB\xBF";

        if (m_lineEnding == LineEnding::Lf)
        {
            result += content;
            return result;
        }

        for (const char byte : content)
        {
            if (byte == '\n')
                result.push_back('\r');

            result.push_back(byte);
        }

        return result;
    }
}
