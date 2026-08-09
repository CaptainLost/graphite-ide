#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/FileProbe.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
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

        const std::size_t bomLength = BomLength(declared);
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
        // Stat first and distinguish why the path is unusable, the same way ProbeFile
        // does: "not there" and "there but a directory" and "there but locked by another
        // process" are different problems and deserve different messages.
        std::error_code statusError;
        const auto status = std::filesystem::status(path, statusError);

        if (statusError || !std::filesystem::exists(status))
            return Error{ErrorCode::NotFound, "No such file: " + fs::ToUtf8(path)};

        if (!std::filesystem::is_regular_file(status))
            return Error{ErrorCode::InvalidArgument, "Not a file (it is a directory): " + fs::ToUtf8(path)};

        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

        if (sizeError)
            return Error{ErrorCode::IoFailure, "Cannot read file: " + fs::ToUtf8(path)};

        // Refused before anything is allocated: a lying or enormous size (a multi-GB
        // file, or one crafted to report one) must not be able to force a multi-GB
        // std::string allocation and send std::bad_alloc across this Result boundary.
        if (size > MaximumFileSize)
        {
            return Error{ErrorCode::InvalidArgument, "File is too large to open ("
                                                          + std::to_string(size)
                                                          + " bytes): " + fs::ToUtf8(path)};
        }

        std::ifstream stream{path, std::ios::binary};

        if (!stream.is_open())
            return Error{ErrorCode::AccessDenied, "Cannot open file: " + fs::ToUtf8(path)};

        // Read to the stream's actual end rather than trusting `size`: on Windows the
        // realistic way for the two to disagree is a file another process is still
        // appending to, which grows between the file_size() call above and this read. A
        // read keyed to the earlier size would silently truncate the document instead of
        // failing loudly, so gcount() decides how much was really there, not `size`.
        //
        // That read still has to stop somewhere: a writer that keeps growing the file
        // faster than this loop can be refused could otherwise turn "read the whole
        // file" into "read forever", and even a one-off grower past the ceiling would
        // land right back in the multi-GB-allocation failure MaximumFileSize exists to
        // rule out — the exact contract violation the check above already refused, just
        // reached through the read instead of the stat. So the loop is bounded by the
        // same ceiling: once more than MaximumFileSize bytes have actually been read,
        // the file is refused the same way an oversized stat would have been.
        std::string content;
        content.reserve(static_cast<std::size_t>(size));

        std::array<char, 65536> chunk{};

        while (stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || stream.gcount() > 0)
        {
            content.append(chunk.data(), static_cast<std::size_t>(stream.gcount()));

            if (content.size() > MaximumFileSize)
            {
                return Error{ErrorCode::InvalidArgument,
                             "File is too large to open (grew past "
                                 + std::to_string(MaximumFileSize) + " bytes while being read): "
                                 + fs::ToUtf8(path)};
            }
        }

        if (stream.bad())
            return Error{ErrorCode::IoFailure, "Cannot read file: " + fs::ToUtf8(path)};

        return FromContent(std::move(content), path);
    }

    std::string WithLineEnding(std::string_view text, LineEnding ending)
    {
        if (ending == LineEnding::Lf)
            return std::string{text};

        std::string result;
        result.reserve(text.size());

        for (const char byte : text)
        {
            if (byte == '\n')
                result.push_back('\r');

            result.push_back(byte);
        }

        return result;
    }

    std::string TextDocument::Bytes() const
    {
        std::string result;

        // Keyed off m_hadBom rather than m_encoding == Utf8Bom: invalid UTF-8 following a
        // mark demotes m_encoding to Unknown, but the mark itself still arrived and still
        // has to go back, or a read-only file's bytes would not round-trip.
        if (m_hadBom)
            result += "\xEF\xBB\xBF";

        result += WithLineEnding(m_buffer.Text(), m_lineEnding);

        return result;
    }
}
