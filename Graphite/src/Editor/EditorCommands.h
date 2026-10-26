#pragma once

#include "Core/Command/Binding.h"
#include "Core/Command/MenuItem.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Core {
	struct OpenItem;
	class CommandRegistry;
	class Keymap;
}

namespace Graphite {

	inline constexpr const char* kGoToDefinitionCommand = "editor.goToDefinition";
	inline constexpr const char* kFindReferencesCommand = "editor.findReferences";
	inline constexpr const char* kSwitchHeaderSourceCommand = "editor.switchHeaderSource";
	inline constexpr const char* kLineHistoryCommand = "git.lineHistory";
	inline constexpr const char* kCutCommand = "edit.cut";
	inline constexpr const char* kCopyCommand = "edit.copy";
	inline constexpr const char* kPasteCommand = "edit.paste";
	inline constexpr const char* kSelectAllCommand = "edit.selectAll";
	inline constexpr const char* kToggleLineCommentCommand = "editor.toggleLineComment";
	inline constexpr const char* kUndoCommand = "editor.undo";
	inline constexpr const char* kRedoCommand = "editor.redo";
	inline constexpr const char* kSaveCommand = "document.save";
	inline constexpr const char* kCloseCommand = "document.close";

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

	struct EditorCommandSpec
	{
		const char* Id;
		const char* Label;
		EditorAction Action;
		ImGuiKeyChord Chord;

		ImGuiKeyChord Alternate = 0;

		bool Repeats = false;
	};

	const std::vector<EditorCommandSpec>& EditorCommandSpecs();

	Core::Binding BindingOf(const EditorCommandSpec& spec);

	Core::Binding SaveBinding();
	Core::Binding CloseBinding();

	bool CanRun(EditorAction action, const Core::OpenItem& item, bool clipboardHasText);

	struct EditorActionOutcome
	{
		bool Changed = false;

		std::optional<std::string> ToClipboard;
	};

	EditorActionOutcome ApplyEditorAction(EditorAction action, Core::OpenItem& item,
										  const std::optional<std::string>& clipboard);

	std::vector<Core::MenuItem> BuildEditorContextMenu();

	class ActiveEditorTarget;
	class ReferencesPanel;

	struct EditorCommandServices
	{
		std::function<bool(const std::filesystem::path&)> HasLanguageServer;
		ReferencesPanel* References = nullptr;
	};

	// Cut to Redo, then Save and Close Tab.
	void RegisterEditingCommands(Core::CommandRegistry& commands, Core::Keymap& keys, ActiveEditorTarget& active);

	// Find, Go to Definition, Find All References and Line History: global, so they also run from a panel.
	void RegisterEditorNavigationCommands(Core::CommandRegistry& commands, Core::Keymap& keys,
										  ActiveEditorTarget& active, EditorCommandServices services);

	// The line Line History opens its peek under: the caret's, or the one right-clicked to open the context menu. For
	// the second the caret is collapsed onto the clicked place when it is on another line, since the peek closes as
	// soon as the caret leaves its line. The item must hold a document.
	std::size_t PlaceCaretForLineHistory(Core::OpenItem& item, std::optional<std::size_t> rightClickedOffset);
}
