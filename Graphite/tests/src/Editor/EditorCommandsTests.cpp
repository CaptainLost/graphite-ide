#include <doctest/doctest.h>

#include "Editor/EditorCommands.h"

#include "Core/Command/Keymap.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	constexpr bool kClipboardHasText = true;
	constexpr bool kClipboardEmpty = false;

	Core::OpenItem ItemWith(std::string content, std::filesystem::path path = "C:/p/main.cpp")
	{
		Core::OpenItem item;

		Core::Result<Core::TextDocument> document = Core::TextDocument::FromContent(std::move(content), path);
		REQUIRE(document);

		item.Document = std::make_shared<Core::TextDocument>(std::move(document).GetValue());
		item.Path = std::move(path);
		Core::RecomputeLineCaches(item);

		return item;
	}

	std::string TextOf(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		return buffer.Substring(0, buffer.GetLength());
	}

	std::size_t CountSeparators(const std::vector<Core::MenuItem>& items)
	{
		std::size_t separatorCount = 0;

		for (const Core::MenuItem& item : items)
		{
			if (item.GetKind() == Core::MenuItem::Kind::Separator)
			{
				++separatorCount;
			}
		}

		return separatorCount;
	}

	std::vector<std::string> CommandIds(const std::vector<Core::MenuItem>& items)
	{
		std::vector<std::string> ids;

		for (const Core::MenuItem& item : items)
		{
			if (item.GetKind() == Core::MenuItem::Kind::Command)
			{
				ids.push_back(item.GetText());
			}
		}

		return ids;
	}
}

TEST_CASE("The context menu lists navigation, clipboard, then comment and select all")
{
	const std::vector<Core::MenuItem> menu = BuildEditorContextMenu();

	CHECK(CommandIds(menu) ==
		  std::vector<std::string>{kGoToDefinitionCommand, kFindReferencesCommand,
								   kSwitchHeaderSourceCommand, kLineHistoryCommand, kCutCommand, kCopyCommand,
								   kPasteCommand, kToggleLineCommentCommand, kSelectAllCommand});

	CHECK(CountSeparators(menu) == 3);
}

TEST_CASE("Peek command Line History sits in its own group of the context menu")
{
	const std::vector<Core::MenuItem> menu = BuildEditorContextMenu();

	const auto lineHistory = std::ranges::find_if(menu, [](const Core::MenuItem& item) {
		return item.GetKind() == Core::MenuItem::Kind::Command && item.GetText() == kLineHistoryCommand;
	});

	REQUIRE(lineHistory != menu.end());
	REQUIRE(lineHistory != menu.begin());
	REQUIRE(std::next(lineHistory) != menu.end());

	CHECK(std::prev(lineHistory)->GetKind() == Core::MenuItem::Kind::Separator);
	CHECK(std::next(lineHistory)->GetKind() == Core::MenuItem::Kind::Separator);
}

TEST_CASE("Peek command Line History opens on the caret's line when no line was right-clicked")
{
	Core::OpenItem item = ItemWith("first\nsecond\nthird\n");
	const std::size_t intoTheThirdLine = item.Document->GetBuffer().LineStart(2) + 2;
	item.Caret = Core::Caret{.Offset = intoTheThirdLine, .Anchor = 0};

	CHECK(PlaceCaretForLineHistory(item, std::nullopt) == 2);
	CHECK(item.Caret.Offset == intoTheThirdLine);
	CHECK(item.Caret.Anchor == 0);
}

TEST_CASE("Peek command Line History collapses the caret onto the right-clicked line")
{
	Core::OpenItem item = ItemWith("first\nsecond\nthird\n");
	const Core::TextBuffer& buffer = item.Document->GetBuffer();
	const std::size_t intoTheThirdLine = buffer.LineStart(2) + 2;
	const std::size_t intoTheSecondLine = buffer.LineStart(1) + 1;
	item.Caret = Core::Caret{.Offset = intoTheThirdLine, .Anchor = 0};

	CHECK(PlaceCaretForLineHistory(item, intoTheSecondLine) == 1);
	CHECK(item.Caret.Offset == intoTheSecondLine);
	CHECK_FALSE(Core::HasSelection(item.Caret));
}

TEST_CASE("Peek command Line History keeps a selection whose caret is on the right-clicked line")
{
	Core::OpenItem item = ItemWith("first\nsecond\nthird\n");
	const std::size_t intoTheThirdLine = item.Document->GetBuffer().LineStart(2) + 2;
	item.Caret = Core::Caret{.Offset = intoTheThirdLine, .Anchor = 0};

	CHECK(PlaceCaretForLineHistory(item, intoTheThirdLine - 1) == 2);
	CHECK(item.Caret.Offset == intoTheThirdLine);
	CHECK(item.Caret.Anchor == 0);
}

TEST_CASE("Cut and copy need a selection, paste needs text on the clipboard")
{
	Core::OpenItem item = ItemWith("abc");

	CHECK_FALSE(CanRun(EditorAction::Cut, item, kClipboardHasText));
	CHECK_FALSE(CanRun(EditorAction::Copy, item, kClipboardHasText));
	CHECK_FALSE(CanRun(EditorAction::Paste, item, kClipboardEmpty));
	CHECK(CanRun(EditorAction::Paste, item, kClipboardHasText));
	CHECK(CanRun(EditorAction::SelectAll, item, kClipboardEmpty));

	item.Caret.Anchor = 0;
	item.Caret.Offset = 2;

	CHECK(CanRun(EditorAction::Cut, item, kClipboardEmpty));
	CHECK(CanRun(EditorAction::Copy, item, kClipboardEmpty));
}

TEST_CASE("Toggle line comment needs a language with a line comment")
{
	CHECK(CanRun(EditorAction::ToggleLineComment, ItemWith("a", "C:/p/main.cpp"), kClipboardEmpty));
	CHECK_FALSE(CanRun(EditorAction::ToggleLineComment, ItemWith("a", "C:/p/notes.txt"), kClipboardEmpty));
}

TEST_CASE("Edit actions are disabled on a read-only document")
{
	Core::OpenItem item = ItemWith("ab\xff");
	REQUIRE(item.Document->IsReadOnly());
	item.Caret.Anchor = 0;
	item.Caret.Offset = 2;

	CHECK_FALSE(CanRun(EditorAction::Cut, item, kClipboardHasText));
	CHECK_FALSE(CanRun(EditorAction::Paste, item, kClipboardHasText));
	CHECK_FALSE(CanRun(EditorAction::ToggleLineComment, item, kClipboardHasText));
	CHECK(CanRun(EditorAction::Copy, item, kClipboardHasText));
	CHECK(CanRun(EditorAction::SelectAll, item, kClipboardHasText));
}

TEST_CASE("Copy hands the selection to the clipboard in CRLF and leaves the text alone")
{
	Core::OpenItem item = ItemWith("one\ntwo");
	item.Caret.Anchor = 0;
	item.Caret.Offset = 7;

	const EditorActionOutcome outcome = ApplyEditorAction(EditorAction::Copy, item, std::nullopt);

	CHECK_FALSE(outcome.Changed);
	REQUIRE(outcome.ToClipboard);
	CHECK(*outcome.ToClipboard == "one\r\ntwo");
	CHECK(TextOf(item) == "one\ntwo");
}

TEST_CASE("Cut copies the selection and removes it")
{
	Core::OpenItem item = ItemWith("abcdef");
	item.Caret.Anchor = 1;
	item.Caret.Offset = 3;

	const EditorActionOutcome outcome = ApplyEditorAction(EditorAction::Cut, item, std::nullopt);

	CHECK(outcome.Changed);
	CHECK(outcome.ToClipboard == std::optional<std::string>{"bc"});
	CHECK(TextOf(item) == "adef");
}

TEST_CASE("Paste inserts the clipboard at the caret and an empty clipboard does nothing")
{
	Core::OpenItem item = ItemWith("ad");
	item.Caret.Anchor = item.Caret.Offset = 1;

	CHECK(ApplyEditorAction(EditorAction::Paste, item, std::string{"bc"}).Changed);
	CHECK(TextOf(item) == "abcd");

	CHECK_FALSE(ApplyEditorAction(EditorAction::Paste, item, std::nullopt).Changed);
}

TEST_CASE("Select all selects the whole document")
{
	Core::OpenItem item = ItemWith("abc\ndef");

	(void)ApplyEditorAction(EditorAction::SelectAll, item, std::nullopt);

	CHECK(item.Caret.Anchor == 0);
	CHECK(item.Caret.Offset == 7);
}

TEST_CASE("Toggle line comment uses the document's own language")
{
	Core::OpenItem item = ItemWith("int a;");

	CHECK(ApplyEditorAction(EditorAction::ToggleLineComment, item, std::nullopt).Changed);
	CHECK(TextOf(item) == "// int a;");
}

TEST_CASE("Undo and redo are available only when the history has a step to take")
{
	Core::OpenItem item = ItemWith("abc");

	CHECK_FALSE(CanRun(EditorAction::Undo, item, kClipboardEmpty));
	CHECK_FALSE(CanRun(EditorAction::Redo, item, kClipboardEmpty));

	item.Caret.Anchor = item.Caret.Offset = 3;
	REQUIRE(Core::InsertText(item, "d"));

	CHECK(CanRun(EditorAction::Undo, item, kClipboardEmpty));
	CHECK_FALSE(CanRun(EditorAction::Redo, item, kClipboardEmpty));

	CHECK(ApplyEditorAction(EditorAction::Undo, item, std::nullopt).Changed);
	CHECK(TextOf(item) == "abc");
	CHECK(CanRun(EditorAction::Redo, item, kClipboardEmpty));

	CHECK(ApplyEditorAction(EditorAction::Redo, item, std::nullopt).Changed);
	CHECK(TextOf(item) == "abcd");
}

TEST_CASE("Save, undo, redo and close have default bindings that take nothing from each other")
{
	Core::Keymap keys;

	for (const EditorCommandSpec& spec : EditorCommandSpecs())
	{
		keys.SetDefault(spec.Id, BindingOf(spec));
	}

	keys.SetDefault(kSaveCommand, SaveBinding());
	keys.SetDefault(kCloseCommand, CloseBinding());

	keys.SetDefault("document.saveAll",
					{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});

	CHECK(keys.FindConflicts().empty());

	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)) == kSaveCommand);
	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Z)) == kUndoCommand);
	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Y)) == kRedoCommand);
	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) == kRedoCommand);
	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)) == kCloseCommand);
	CHECK(keys.BoundTo(Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F4)) == kCloseCommand);
}

TEST_CASE("Only undo and redo repeat while their key is held")
{
	for (const EditorCommandSpec& spec : EditorCommandSpecs())
	{
		const bool isHistoryAction = spec.Action == EditorAction::Undo || spec.Action == EditorAction::Redo;

		CHECK_MESSAGE(spec.Repeats == isHistoryAction, spec.Id);
	}
}
