#pragma once

#include "Core/Command/Binding.h"
#include "Core/Command/MenuItem.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {
	struct OpenItem;
}

namespace Graphite {

	inline constexpr const char* kGoToDefinitionCommand = "editor.goToDefinition";
	inline constexpr const char* kFindReferencesCommand = "editor.findReferences";
	inline constexpr const char* kSwitchHeaderSourceCommand = "editor.switchHeaderSource";
	inline constexpr const char* kCutCommand = "edit.cut";
	inline constexpr const char* kCopyCommand = "edit.copy";
	inline constexpr const char* kPasteCommand = "edit.paste";
	inline constexpr const char* kSelectAllCommand = "edit.selectAll";
	inline constexpr const char* kToggleLineCommentCommand = "editor.toggleLineComment";
	inline constexpr const char* kUndoCommand = "editor.undo";
	inline constexpr const char* kRedoCommand = "editor.redo";
	inline constexpr const char* kSaveCommand = "document.save";
	inline constexpr const char* kCloseCommand = "document.close";

	// What an editor command asks of the document it runs on. Queued rather than applied where
	// the command runs: only the widget knows what has to be told an edit happened.
	enum class EditorAction : std::uint8_t
	{
		Cut,
		Copy,
		Paste,
		SelectAll,
		ToggleLineComment,
		Undo,
		Redo
	};

	// An editor command that queues an EditorAction on the active document, with the chords it
	// is bound to by default. Held in one table so the defaults can be checked against each other
	// without a window to register them in.
	struct EditorCommandSpec
	{
		const char* Id;
		const char* Label;
		EditorAction Action;
		ImGuiKeyChord Chord;

		// Zero for none.
		ImGuiKeyChord Alternate = 0;

		// Only undo and redo: holding the key walks the history, paced by the widget.
		bool Repeats = false;
	};

	const std::vector<EditorCommandSpec>& EditorCommandSpecs();

	Core::Binding BindingOf(const EditorCommandSpec& spec);

	// The defaults of the two commands that act on the active tab rather than on its text.
	Core::Binding SaveBinding();
	Core::Binding CloseBinding();

	// The clipboard is read by the caller and passed in, so this never touches ImGui.
	bool CanRun(EditorAction action, const Core::OpenItem& item, bool clipboardHasText);

	struct EditorActionOutcome
	{
		bool Changed = false;

		// What the action put on the clipboard, already in the clipboard's CRLF form.
		std::optional<std::string> ToClipboard;
	};

	// Pure: reads the clipboard from `clipboard` and reports what to write back, so it can be
	// tested with no ImGui context. The caller tells the syntax cache and the lenses.
	EditorActionOutcome ApplyEditorAction(EditorAction action, Core::OpenItem& item,
										  const std::optional<std::string>& clipboard);

	std::vector<Core::MenuItem> BuildEditorContextMenu();
}
