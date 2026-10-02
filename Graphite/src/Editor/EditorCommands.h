#pragma once

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

	// What an editor command asks of the document it runs on. Queued rather than applied where
	// the command runs: only the widget knows what has to be told an edit happened.
	enum class EditorAction : std::uint8_t
	{
		Cut,
		Copy,
		Paste,
		SelectAll,
		ToggleLineComment
	};

	// Whether a language server is configured for this file's language — the condition every
	// navigation command shares.
	bool HasLanguageServer(const std::filesystem::path& path);

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
