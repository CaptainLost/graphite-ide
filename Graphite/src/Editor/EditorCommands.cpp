#include "Editor/EditorCommands.h"

#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/ServerRegistry.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

namespace Graphite {

	namespace {
		bool IsWritable(const Core::OpenItem& item)
		{
			return item.Document && !item.Document->IsReadOnly();
		}

		std::string SelectionForClipboard(const Core::OpenItem& item)
		{
			const auto [from, to] = Core::SelectionRange(item.Caret);

			// The buffer's text is LF-only and the Windows clipboard is CRLF: pasted into a
			// Win32 edit control, LF-only text arrives as a single line.
			return Core::WithLineEnding(item.Document->GetBuffer().Substring(from, to),
										Core::LineEnding::Crlf);
		}
	}

	bool HasLanguageServer(const std::filesystem::path& path)
	{
		return ServerConfigFor(LanguageOfPath(path)) != nullptr;
	}

	bool CanRun(EditorAction action, const Core::OpenItem& item, bool clipboardHasText)
	{
		if (!item.Document)
			return false;

		switch (action)
		{
		case EditorAction::Cut:
			return IsWritable(item) && Core::HasSelection(item.Caret);

		case EditorAction::Copy:
			return Core::HasSelection(item.Caret);

		case EditorAction::Paste:
			return IsWritable(item) && clipboardHasText;

		case EditorAction::SelectAll:
			return true;

		case EditorAction::ToggleLineComment:
			return IsWritable(item) && LineCommentPrefix(LanguageOfPath(item.Path)).has_value();
		}

		return false;
	}

	EditorActionOutcome ApplyEditorAction(EditorAction action, Core::OpenItem& item,
										  const std::optional<std::string>& clipboard)
	{
		EditorActionOutcome outcome;

		if (!item.Document)
			return outcome;

		switch (action)
		{
		case EditorAction::Copy:
			if (Core::HasSelection(item.Caret))
				outcome.ToClipboard = SelectionForClipboard(item);
			break;

		case EditorAction::Cut:
			// Only with something selected: DeleteBackward on an empty selection removes the
			// character before the caret and puts nothing on the clipboard.
			if (IsWritable(item) && Core::HasSelection(item.Caret))
			{
				outcome.ToClipboard = SelectionForClipboard(item);
				outcome.Changed = Core::DeleteBackward(item);
			}
			break;

		case EditorAction::Paste:
			if (clipboard)
				outcome.Changed = Core::InsertText(item, *clipboard);
			break;

		case EditorAction::SelectAll:
			item.Caret = Core::SelectAll(item.Document->GetBuffer());
			break;

		case EditorAction::ToggleLineComment:
			if (const auto prefix = LineCommentPrefix(LanguageOfPath(item.Path)))
				outcome.Changed = Core::ToggleLineComment(item, *prefix);
			break;
		}

		return outcome;
	}

	std::vector<Core::MenuItem> BuildEditorContextMenu()
	{
		return {Core::MenuItem::Command(kGoToDefinitionCommand),
				Core::MenuItem::Command(kFindReferencesCommand),
				Core::MenuItem::Command(kSwitchHeaderSourceCommand),
				Core::MenuItem::Separator(),
				Core::MenuItem::Command(kCutCommand),
				Core::MenuItem::Command(kCopyCommand),
				Core::MenuItem::Command(kPasteCommand),
				Core::MenuItem::Separator(),
				Core::MenuItem::Command(kToggleLineCommentCommand),
				Core::MenuItem::Command(kSelectAllCommand)};
	}
}
