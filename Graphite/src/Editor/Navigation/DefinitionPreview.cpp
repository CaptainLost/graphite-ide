#include "Editor/Navigation/DefinitionPreview.h"

#include "Editor/Navigation/DefinitionExtent.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace Graphite {

	namespace {
		DefinitionPreview Cut(const Core::TextBuffer& buffer, const std::filesystem::path& path,
							  std::size_t line)
		{
			DefinitionPreview preview;
			preview.Path = path;

			const std::size_t count = buffer.GetLineCount();
			if (line >= count)
			{
				return preview;
			}

			const std::size_t scanEnd = std::min(line + kDefinitionScanLines, count);
			std::vector<std::string> scannedText;

			for (std::size_t lineIndex = line; lineIndex < scanEnd; ++lineIndex)
			{
				scannedText.push_back(buffer.LineText(lineIndex));
			}

			const std::vector<std::string_view> scannedLines(scannedText.begin(), scannedText.end());
			const std::vector<std::vector<Token>> scannedTokens = ColourLines(LanguageOfPath(path), scannedLines);

			LineRange range{line, std::min(line + (kDefinitionFallbackLines - 1), count - 1)};

			if (!scannedTokens.empty())
			{
				range = DefinitionExtent(scannedLines, scannedTokens, line);
			}

			range.Last = std::min(range.Last, count - 1);
			preview.FirstLine = range.First;
			for (std::size_t i = range.First; i <= range.Last; ++i)
			{
				preview.Lines.push_back(buffer.LineText(i));
			}
			return preview;
		}
	}

	DefinitionPreview LoadDefinitionPreview(const Core::DocumentSet& documents,
											const std::filesystem::path& path, std::size_t line)
	{
		// An open buffer wins over the disk: it may hold unsaved edits, and clangd answered
		// about those edits too, so the line it named only means something in that text.
		if (const auto index = documents.Find(path))
		{
			const Core::OpenItem& item = documents.At(*index);
			if (item.Document)
			{
				return Cut(item.Document->GetBuffer(), path, line);
			}
		}

		DefinitionPreview preview;
		preview.Path = path;

		// The read happens on the UI thread, so a file that could stall a frame is not read.
		std::error_code ec;
		const std::uintmax_t size = std::filesystem::file_size(path, ec);
		if (ec)
		{
			preview.Unreadable = true;
			return preview;
		}
		if (size >= kPreviewFileByteLimit)
		{
			preview.TooLarge = true;
			return preview;
		}

		auto loaded = Core::TextDocument::Load(path);
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
