#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/Fs/FileSystem.h"

#include <fstream>
#include <sstream>
#include <string_view>
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

        const std::string_view content =
            std::string_view{bytes}.substr(BomLength(bytes));

        TextDocument document;
        document.m_path = std::move(path);
        document.m_encoding = declared;

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

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        if (stream.bad())
            return Error{ErrorCode::IoFailure, "Cannot read file: " + fs::ToUtf8(path)};

        return FromContent(buffer.str(), path);
    }

    std::string TextDocument::Bytes() const
    {
        const std::string content = m_buffer.Text();

        std::string result;
        result.reserve(content.size() + 3);

        if (m_encoding == Encoding::Utf8Bom)
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
