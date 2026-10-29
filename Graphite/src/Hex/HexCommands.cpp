#include "Hex/HexCommands.h"

#include "Hex/HexText.h"

#include "Core/FileSystem/FileRange.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kCopyTooLarge = "Select at most 1 MB to copy";
	}

	bool CanRunOnHexTab(EditorAction action, std::uint64_t fileSize)
	{
		switch (action)
		{
		case EditorAction::Copy:
		case EditorAction::SelectAll:
			return fileSize > 0;

		case EditorAction::Cut:
		case EditorAction::Paste:
		case EditorAction::ToggleLineComment:
		case EditorAction::Undo:
		case EditorAction::Redo:
			break;
		}

		return false;
	}

	HexSelection WholeFile(std::uint64_t fileSize)
	{
		return HexSelection{.Anchor = 0, .Active = fileSize > 0 ? fileSize - 1 : 0};
	}

	Core::Result<std::string> CopySelectedBytes(const std::filesystem::path& path, const HexSelection& selection,
												std::uint64_t fileSize)
	{
		if (fileSize == 0)
		{
			return std::string{};
		}

		if (selection.Count() > kMostCopiedBytes)
		{
			return Core::Error{.Code = Core::ErrorCode::TooLarge, .Message = kCopyTooLarge};
		}

		Core::Result<std::vector<std::uint8_t>> bytes =
			Core::ReadFileRange(path, selection.First(), static_cast<std::size_t>(selection.Count()));

		if (!bytes)
		{
			return bytes.GetError();
		}

		return CopyText(bytes.GetValue());
	}
}
