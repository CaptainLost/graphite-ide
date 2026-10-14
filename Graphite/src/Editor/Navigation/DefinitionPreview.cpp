#include "Editor/Navigation/DefinitionPreview.h"

#include "Editor/Navigation/DefinitionExtent.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include <algorithm>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		DefinitionPreview Cut(const Core::TextBuffer& buffer, const std::filesystem::path& path, std::size_t line)
		{
			DefinitionPreview preview;
			preview.Path = path;

			const std::size_t lineCount = buffer.GetLineCount();
			if (line >= lineCount)
			{
				return preview;
			}

			const std::size_t scanEnd = std::min(line + kDefinitionScanLines, lineCount);
			std::vector<std::string> scannedText;

			for (std::size_t lineIndex = line; lineIndex < scanEnd; ++lineIndex)
			{
				scannedText.push_back(buffer.LineText(lineIndex));
			}

			const std::vector<std::string_view> scannedLines(scannedText.begin(), scannedText.end());
			const std::vector<std::vector<Token>> scannedTokens = ColourLines(LanguageOfPath(path), scannedLines);

			DefinitionLineRange range{line, std::min(line + (kDefinitionFallbackLines - 1), lineCount - 1)};

			if (!scannedTokens.empty())
			{
				range = DefinitionExtent(scannedLines, scannedTokens, line);
			}

			range.Last = std::min(range.Last, lineCount - 1);
			preview.FirstLine = range.First;
			for (std::size_t lineIndex = range.First; lineIndex <= range.Last; ++lineIndex)
			{
				preview.Lines.push_back(buffer.LineText(lineIndex));
			}
			return preview;
		}
	}

	DefinitionPreview LoadDefinitionPreview(const Core::DocumentSet& documents, const std::filesystem::path& path,
											std::size_t line)
	{
		if (const Core::TextBuffer* openBuffer = Core::BufferOfOpenDocument(documents, path))
		{
			return Cut(*openBuffer, path, line);
		}

		DefinitionPreview preview;
		preview.Path = path;

		// WARNING: The read happens on the UI thread, so a file that could stall a frame is not read.
		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
		if (sizeError)
		{
			preview.Unreadable = true;
			return preview;
		}
		if (size >= kPreviewFileByteLimit)
		{
			preview.TooLarge = true;
			return preview;
		}

		Core::Result<Core::TextDocument> loaded = Core::TextDocument::Load(path);
		if (!loaded.HasValue())
		{
			preview.Unreadable = true;
			return preview;
		}

		Core::TextBuffer& buffer = loaded.GetValue().GetBuffer();

		preview = Cut(buffer, path, line);
		preview.Text = std::move(buffer);
		return preview;
	}

	bool IsDrawable(const DefinitionPreview& preview)
	{
		return preview.TooLarge || (!preview.Unreadable && !preview.Lines.empty());
	}
}
