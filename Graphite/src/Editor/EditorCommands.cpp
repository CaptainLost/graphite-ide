#include "Editor/EditorCommands.h"

#include "Language/Registry/LanguageRegistry.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"

#include <optional>
#include <string_view>

namespace Graphite {

	namespace {
		bool IsWritable(const Core::OpenItem& item)
		{
			return item.Document && !item.Document->IsReadOnly();
		}

		std::string SelectionForClipboard(const Core::OpenItem& item)
		{
			const auto [selectionStart, selectionEnd] = Core::SelectionRange(item.Caret);

			// WARNING: The buffer's text is LF-only and the Windows clipboard is CRLF: pasted into
			// a Win32 edit control, LF-only text arrives as a single line.
			return Core::WithLineEnding(item.Document->GetBuffer().Substring(selectionStart, selectionEnd),
										Core::LineEnding::Crlf);
		}
	}

	const std::vector<EditorCommandSpec>& EditorCommandSpecs()
	{
		static const std::vector<EditorCommandSpec> specs = {
			{.Id = kCutCommand,
			 .Label = "Cut",
			 .Action = EditorAction::Cut,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_X},
			{.Id = kCopyCommand,
			 .Label = "Copy",
			 .Action = EditorAction::Copy,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_C},
			{.Id = kPasteCommand,
			 .Label = "Paste",
			 .Action = EditorAction::Paste,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_V},
			{.Id = kSelectAllCommand,
			 .Label = "Select All",
			 .Action = EditorAction::SelectAll,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_A},
			{.Id = kToggleLineCommentCommand,
			 .Label = "Toggle Line Comment",
			 .Action = EditorAction::ToggleLineComment,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_Slash},
			{.Id = kUndoCommand,
			 .Label = "Undo",
			 .Action = EditorAction::Undo,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_Z,
			 .Repeats = true},
			{.Id = kRedoCommand,
			 .Label = "Redo",
			 .Action = EditorAction::Redo,
			 .Chord = ImGuiMod_Ctrl | ImGuiKey_Y,
			 .Alternate = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
			 .Repeats = true},
		};

		return specs;
	}

	Core::Binding BindingOf(const EditorCommandSpec& spec)
	{
		Core::Binding binding{.Primary = Core::Chord::Key(spec.Chord)};

		if (spec.Alternate != 0)
		{
			binding.Alternates.push_back(Core::Chord::Key(spec.Alternate));
		}

		return binding;
	}

	Core::Binding SaveBinding()
	{
		return {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)};
	}

	Core::Binding CloseBinding()
	{
		return {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W),
				.Alternates = {Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F4)}};
	}

	bool CanRun(EditorAction action, const Core::OpenItem& item, bool clipboardHasText)
	{
		if (!item.Document)
		{
			return false;
		}

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

		case EditorAction::Undo:
			return IsWritable(item) && item.Document->GetBuffer().CanUndo();

		case EditorAction::Redo:
			return IsWritable(item) && item.Document->GetBuffer().CanRedo();
		}

		return false;
	}

	EditorActionOutcome ApplyEditorAction(EditorAction action, Core::OpenItem& item,
										  const std::optional<std::string>& clipboard)
	{
		EditorActionOutcome outcome;

		if (!item.Document)
		{
			return outcome;
		}

		switch (action)
		{
		case EditorAction::Copy:
			if (Core::HasSelection(item.Caret))
			{
				outcome.ToClipboard = SelectionForClipboard(item);
			}
			break;

		case EditorAction::Cut:
			// WARNING: DeleteBackward on an empty selection removes the character before the
			// caret and puts nothing on the clipboard, so cut needs a selection.
			if (IsWritable(item) && Core::HasSelection(item.Caret))
			{
				outcome.ToClipboard = SelectionForClipboard(item);
				outcome.Changed = Core::DeleteBackward(item);
			}
			break;

		case EditorAction::Paste:
			if (clipboard)
			{
				outcome.Changed = Core::InsertText(item, *clipboard);
			}
			break;

		case EditorAction::SelectAll:
			item.Caret = Core::SelectAll(item.Document->GetBuffer());
			break;

		case EditorAction::ToggleLineComment:
			if (const std::optional<std::string_view> prefix =
					LineCommentPrefix(LanguageOfPath(item.Path)))
			{
				outcome.Changed = Core::ToggleLineComment(item, *prefix);
			}
			break;

		case EditorAction::Undo:
			outcome.Changed = Core::Undo(item);
			break;

		case EditorAction::Redo:
			outcome.Changed = Core::Redo(item);
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
