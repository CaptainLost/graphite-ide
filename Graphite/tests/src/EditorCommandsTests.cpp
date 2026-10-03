#include <doctest/doctest.h>

#include "Editor/EditorCommands.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <string>

using namespace Graphite;

namespace {
	Core::OpenItem ItemWith(std::string content, std::filesystem::path path = "C:/p/main.cpp")
	{
		Core::OpenItem item;

		auto document = Core::TextDocument::FromContent(std::move(content), path);
		REQUIRE(document);

		item.Document = std::move(document).GetValue();
		item.Path = std::move(path);
		Core::RefreshDerived(item);

		return item;
	}

	std::string TextOf(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		return buffer.Substring(0, buffer.GetLength());
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
								   kSwitchHeaderSourceCommand, kCutCommand, kCopyCommand,
								   kPasteCommand, kToggleLineCommentCommand, kSelectAllCommand});

	const auto separators = std::count_if(menu.begin(), menu.end(), [](const Core::MenuItem& item) {
		return item.GetKind() == Core::MenuItem::Kind::Separator;
	});
	CHECK(separators == 2);
}

TEST_CASE("Cut and copy need a selection, paste needs text on the clipboard")
{
	Core::OpenItem item = ItemWith("abc");

	CHECK_FALSE(CanRun(EditorAction::Cut, item, true));
	CHECK_FALSE(CanRun(EditorAction::Copy, item, true));
	CHECK_FALSE(CanRun(EditorAction::Paste, item, false));
	CHECK(CanRun(EditorAction::Paste, item, true));
	CHECK(CanRun(EditorAction::SelectAll, item, false));

	item.Caret.Anchor = 0;
	item.Caret.Offset = 2;

	CHECK(CanRun(EditorAction::Cut, item, false));
	CHECK(CanRun(EditorAction::Copy, item, false));
}

TEST_CASE("Toggle line comment needs a language with a line comment")
{
	CHECK(CanRun(EditorAction::ToggleLineComment, ItemWith("a", "C:/p/main.cpp"), false));
	CHECK_FALSE(CanRun(EditorAction::ToggleLineComment, ItemWith("a", "C:/p/notes.txt"), false));
}

// A document is read-only when its bytes were not valid UTF-8 and so were not decoded; there is
// no setter, so the test builds one the way the loader does.
TEST_CASE("Edit actions are disabled on a read-only document")
{
	Core::OpenItem item = ItemWith("ab\xff");
	REQUIRE(item.Document->IsReadOnly());
	item.Caret.Anchor = 0;
	item.Caret.Offset = 2;

	CHECK_FALSE(CanRun(EditorAction::Cut, item, true));
	CHECK_FALSE(CanRun(EditorAction::Paste, item, true));
	CHECK_FALSE(CanRun(EditorAction::ToggleLineComment, item, true));
	CHECK(CanRun(EditorAction::Copy, item, true));
	CHECK(CanRun(EditorAction::SelectAll, item, true));
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

TEST_CASE("A C++ file has a language server and a text file has none")
{
	CHECK(HasLanguageServer("C:/p/main.cpp"));
	CHECK_FALSE(HasLanguageServer("C:/p/notes.txt"));
}
