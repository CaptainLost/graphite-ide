#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Codec.h"
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
        // The same prefix ProbeFile reads, rather than the whole file. Sharing
        // SniffEncoding with the probe guarantees the two run the same algorithm; only
        // giving them the same bytes guarantees they reach the same answer, and their
        // answers have to agree — the probe decides whether a file may be opened at all,
        // this decides how to read it. Sniffing the whole file here used to split them on
        // an unmarked UTF-16 file bigger than the sample whose tail holds a control
        // character: the probe called it UTF-16 text, this called it UTF-8, and raw
        // UTF-16 with a NUL after every character landed in a writable buffer.
        const Encoding declared =
            SniffEncoding(std::string_view{bytes}.substr(0, ProbeSampleSize));

        // BomLength(declared) would be wrong here: SniffEncoding can name a UTF-16 file
        // by the shape of its bytes alone, with no mark actually present, and
        // BomLength(Encoding) always answers for the encoding as such, not for this
        // particular file. BomLength(bytes) checks the real leading bytes instead, so an
        // unmarked file has nothing stripped from it.
        const std::size_t bomLength = BomLength(bytes);

        std::string content;

        // Decode is called only when it would actually change something. For every other
        // encoding it returns its input verbatim, which means a second copy of the whole
        // file: near MaximumFileSize that is another half gigabyte alive at the same time
        // as these bytes and as the copy NormaliseLineEndings makes below, which is the
        // peak MaximumFileSize exists to bound. Moving the bytes across instead costs the
        // memmove that drops the mark and nothing else. TransformsContent is the single
        // source of truth for which arms transform, so this asks it rather than keeping a
        // second list of them.
        if (TransformsContent(declared))
        {
            auto decoded = Decode(std::string_view{bytes}.substr(bomLength), declared);

            if (!decoded)
                return decoded.GetError();

            content = std::move(decoded).Value();
        }
        else
        {
            bytes.erase(0, bomLength);
            content = std::move(bytes);
        }

        // The strict rule, which only a full load can afford. SniffEncoding tolerates
        // input cut mid-unit and saw only the prefix above, so its verdict is a hypothesis
        // about the rest of the file; here the rest has arrived and been decoded, so the
        // same rule is put to all of it. Two files reach this line and neither may become
        // a writable document: one whose UTF-16 shape holds for its first 8 KB and not
        // afterwards, and a UTF-32 one, whose FF FE 00 00 mark opens with the UTF-16 LE
        // mark and so decodes to a NUL after every character. Refusing is the point — a
        // false negative costs a moment's confusion, a false positive costs the file at
        // its first edit. TransformsContent keeps this to content the codec produced;
        // bytes that only passed through are judged by IsValidUtf8 below, as before.
        if (TransformsContent(declared) && !LooksLikeText(content))
        {
            return Error{ErrorCode::InvalidArgument,
                         "This is not " + std::string{EncodingName(declared)}
                             + " text: decoding it gives control characters: "
                             + fs::ToUtf8(path)};
        }

        TextDocument document;
        document.m_path = std::move(path);
        document.m_encoding = declared;
        document.m_hadBom = bomLength > 0;
        // Assigned whether or not a mark was there, because m_hadBom already says which it
        // was and Bytes() reads this only when that is true. Captured separately from
        // m_encoding: invalid UTF-8 behind a real mark demotes m_encoding to Unknown for
        // read-only handling, but the mark that was actually there still has to go back.
        document.m_bomEncoding = declared;

        // Only content that was not decoded can fail validation: Decode returns valid
        // UTF-8 by construction, or an error. Validating it here would suggest otherwise.
        // TransformsContent is the single source of truth for which arms of Decode's
        // switch actually decode something, so this stays correct as more of them do.
        if (!TransformsContent(declared) && !IsValidUtf8(content))
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
            result += BomBytes(m_bomEncoding);

        result += Encode(WithLineEnding(m_buffer.Text(), m_lineEnding), m_encoding);

        return result;
    }
}
