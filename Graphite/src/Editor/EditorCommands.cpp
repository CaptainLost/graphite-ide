#include "Editor/EditorCommands.h"

#include "Editor/EditorCommandTarget.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <cstddef>
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
			{.Id = kMoveLinesUpCommand,
			 .Label = "Move Line Up",
			 .Action = EditorAction::MoveLinesUp,
			 .Chord = ImGuiMod_Alt | ImGuiKey_UpArrow,
			 .Repeats = true},
			{.Id = kMoveLinesDownCommand,
			 .Label = "Move Line Down",
			 .Action = EditorAction::MoveLinesDown,
			 .Chord = ImGuiMod_Alt | ImGuiKey_DownArrow,
			 .Repeats = true},
			{.Id = kDuplicateLinesCommand,
			 .Label = "Duplicate Line",
			 .Action = EditorAction::DuplicateLines,
			 .Chord = ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_DownArrow,
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

		case EditorAction::MoveLinesUp:
		case EditorAction::MoveLinesDown:
		case EditorAction::DuplicateLines:
			return IsWritable(item);
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

		case EditorAction::MoveLinesUp:
			outcome.Changed = Core::MoveLines(item, -1);
			break;

		case EditorAction::MoveLinesDown:
			outcome.Changed = Core::MoveLines(item, 1);
			break;

		case EditorAction::DuplicateLines:
			outcome.Changed = Core::DuplicateLines(item);
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
				Core::MenuItem::Command(kLineHistoryCommand),
				Core::MenuItem::Separator(),
				Core::MenuItem::Command(kCutCommand),
				Core::MenuItem::Command(kCopyCommand),
				Core::MenuItem::Command(kPasteCommand),
				Core::MenuItem::Separator(),
				Core::MenuItem::Command(kToggleLineCommentCommand),
				Core::MenuItem::Command(kSelectAllCommand)};
	}

	void RegisterEditingCommands(Core::CommandRegistry& commands, Core::Keymap& keys, ActiveEditorTarget& active)
	{
		const bool fromChord = false;
		const bool fromMenu = true;

		for (const EditorCommandSpec& spec : EditorCommandSpecs())
		{
			const EditorAction action = spec.Action;

			commands.Add({.Id = spec.Id,
						  .Label = spec.Label,
						  .Run = [&active, action, fromChord] { active.Get()->Queue(action, fromChord); },
						  .RunFromMenu = [&active, action, fromMenu] { active.Get()->Queue(action, fromMenu); },
						  .Enabled = [&active, action] { return active.Get() != nullptr && active.Get()->CanRun(action); },
						  .KeyScope = Core::ShortcutScope::FocusedEditor,
						  .RepeatsWhileHeld = spec.Repeats});

			keys.SetDefault(spec.Id, BindingOf(spec));
		}

		commands.Add({.Id = kSaveCommand,
					  .Label = "Save",
					  .Run = [&active] { active.Get()->Save(); },
					  .Enabled = [&active] { return active.Get() != nullptr && active.Get()->CanSave(); },
					  .KeyScope = Core::ShortcutScope::FocusedEditor,
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kSaveCommand, SaveBinding());

		commands.Add({.Id = kCloseCommand,
					  .Label = "Close Tab",
					  .Run = [&active] { active.Get()->Close(); },
					  .Enabled = [&active] { return active.Get() != nullptr && active.Get()->CanClose(); },
					  .KeyScope = Core::ShortcutScope::FocusedEditor,
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kCloseCommand, CloseBinding());
	}

	void RegisterEditorNavigationCommands(Core::CommandRegistry& commands, Core::Keymap& keys,
										  ActiveEditorTarget& active, EditorCommandServices services)
	{
		// The commands below are global, as before: they also run from a panel, on the view the
		// user last worked in.
		commands.Add({.Id = "find.open",
					  .Label = "Find",
					  .Run = [&active] { active.Get()->OpenFind(); },
					  .Enabled = [&active] { return active.Get() != nullptr && active.Get()->CanFind(); }});

		keys.SetDefault("find.open", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F)});

		const auto hasServerAtCaret = [&active, hasLanguageServer = services.HasLanguageServer] {
			if (active.Get() == nullptr)
			{
				return false;
			}

			const std::optional<std::filesystem::path> file = active.Get()->CaretFile();

			return file.has_value() && hasLanguageServer(*file);
		};

		commands.Add({.Id = kGoToDefinitionCommand,
					  .Label = "Go to Definition",
					  .Run = [&active] { active.Get()->GoToDefinition(); },
					  .Enabled = hasServerAtCaret});

		keys.SetDefault(kGoToDefinitionCommand, {.Primary = Core::Chord::Key(ImGuiKey_F12)});

		ReferencesPanel* const references = services.References;

		commands.Add({.Id = kFindReferencesCommand,
					  .Label = "Find All References",
					  .Run = [&active, references] {
						  if (references != nullptr)
						  {
							  active.Get()->FindReferences(*references);
						  }
					  },
					  .Enabled = hasServerAtCaret});

		keys.SetDefault(kFindReferencesCommand,
						{.Primary = Core::Chord::Key(ImGuiMod_Shift | ImGuiKey_F12)});

		// No default key. From the context menu it acts on the right-clicked line, which the editor records as the menu
		// opens, because a right-click inside a selection leaves the caret where it was; from the palette it acts on
		// the caret's line.
		constexpr bool kFromPalette = false;
		constexpr bool kFromMenu = true;

		commands.Add({.Id = kLineHistoryCommand,
					  .Label = "Line History",
					  .Run = [&active] { active.Get()->ShowLineHistory(kFromPalette); },
					  .RunFromMenu = [&active] { active.Get()->ShowLineHistory(kFromMenu); },
					  .Enabled = [&active] { return active.Get() != nullptr && active.Get()->CanShowLineHistory(); }});
	}

	std::size_t PlaceCaretForLineHistory(Core::OpenItem& item, std::optional<std::size_t> rightClickedOffset)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t caretLine = buffer.ToPosition(item.Caret.Offset).Line;

		if (!rightClickedOffset)
		{
			return caretLine;
		}

		// Another view may have shortened the document while the menu was open.
		const std::size_t offset = std::min(*rightClickedOffset, buffer.GetLength());
		const std::size_t clickedLine = buffer.ToPosition(offset).Line;

		if (clickedLine != caretLine)
		{
			item.Caret = Core::PlaceAt(buffer, item.Caret, offset, Core::SelectionChange::Collapse, item.TabWidth);
		}

		return clickedLine;
	}
}
