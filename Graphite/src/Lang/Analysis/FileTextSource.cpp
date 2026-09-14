#include "Lang/Analysis/FileTextSource.h"

#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		// The same read PreviewContent does, deliberately: a file the background analysis decoded
		// differently from the pane that shows it would put its colours on different bytes.
		bool ReadAll(const std::filesystem::path& path, std::string& bytes)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return false;
			}

			bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

			return true;
		}
	}

	FileText ReadFileText(const std::filesystem::path& path)
	{
		FileText read;
		read.Path = path;

		const Core::Result<Core::FileProbe> probed = Core::ProbeFile(path);

		if (!probed)
		{
			return read;
		}

		const Core::FileProbe& probe = probed.GetValue();

		if (probe.Kind == Core::FileKind::Binary || probe.Kind == Core::FileKind::TooLarge ||
			probe.Kind == Core::FileKind::LargeText)
		{
			// LargeText is refused here where the editor accepts it: a file this size is minutes
			// of a server's attention for colours nobody has asked to see yet, and the tab that
			// opens it asks for its own.
			return read;
		}

		std::string bytes;

		if (!ReadAll(path, bytes))
		{
			return read;
		}

		// Bytes rather than probe.FileEncoding, for the reason TextDocument::FromContent
		// computes it the same way: the probe only sniffed a sample, so the mark actually
		// sitting on the bytes about to be decoded is what has to be stripped.
		const std::size_t bomLength = Core::BomLength(bytes);

		const Core::Result<std::string> text =
			Core::TransformsContent(probe.FileEncoding)
				? Core::Decode(std::string_view{bytes}.substr(bomLength), probe.FileEncoding)
				: Core::Decode(bytes, probe.FileEncoding);

		if (!text)
		{
			return read;
		}

		std::string decoded = text.GetValue();

		// Core::Decode passes UTF-8 through unchanged, so a mark on disk is still the first
		// three bytes here. SemanticOverlay lines its spans up against the editor's own
		// buffer, which TextDocument::FromContent already strips the same mark from — left
		// in here, every colour on line 0 of a BOM-prefixed file would sit one character to
		// the right of the text it belongs to.
		if (!Core::TransformsContent(probe.FileEncoding))
		{
			decoded.erase(0, bomLength);
		}

		std::error_code error;
		const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);

		if (error)
		{
			// Without a stamp there is nothing to vouch for the text, and an entry nothing can
			// vouch for is one the cache would have to refuse anyway.
			return read;
		}

		read.Text = std::move(decoded);
		read.ModifiedAt = modified;
		read.Readable = true;

		return read;
	}

	void DiskFileTexts::Request(const std::filesystem::path& path)
	{
		m_Jobs.Submit([ready = m_Ready, path]
		{
			FileText read = ReadFileText(path);

			const std::lock_guard<std::mutex> guard(ready->Mutex);
			ready->Texts.push_back(std::move(read));
		});
	}

	void DiskFileTexts::Take(std::vector<FileText>& out)
	{
		const std::lock_guard<std::mutex> guard(m_Ready->Mutex);

		for (FileText& text : m_Ready->Texts)
		{
			out.push_back(std::move(text));
		}

		m_Ready->Texts.clear();
	}
}
