#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"

#include <optional>
#include <ostream>
#include <random>
#include <string>

using namespace Core;

namespace {
	const std::string kRandomFragments[] = {"x", "\n", "hello", "a\nb", "\n\n", "", "long fragment here"};

	constexpr std::size_t kLargeTextLength = 1000;
	constexpr int kToggleCount = 200;

	bool ChooseToInsert(std::mt19937& random, const std::string& content)
	{
		const bool nothingToErase = content.empty();

		if (nothingToErase)
		{
			return true;
		}

		return (random() % 2) == 0;
	}

	std::size_t RandomInsertionOffset(std::mt19937& random, std::size_t length)
	{
		return random() % (length + 1);
	}

	const std::string& RandomFragment(std::mt19937& random)
	{
		return kRandomFragments[random() % std::size(kRandomFragments)];
	}

	struct ErasedRange
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	ErasedRange RandomRangeWithin(std::mt19937& random, std::size_t length)
	{
		const std::size_t from = random() % length;
		const std::size_t to = from + random() % (length - from + 1);

		return ErasedRange{.From = from, .To = to};
	}
}

TEST_CASE("A buffer starts out holding what it was given")
{
	const TextBuffer buffer{"hello"};

	CHECK(buffer.GetLength() == 5);
	CHECK(buffer.Materialize() == "hello");
	CHECK_FALSE(buffer.IsEmpty());
}

TEST_CASE("An empty buffer is empty")
{
	const TextBuffer buffer;

	CHECK(buffer.GetLength() == 0);
	CHECK(buffer.Materialize() == "");
	CHECK(buffer.IsEmpty());
}

TEST_CASE("Text can be inserted at the start, in the middle and at the end")
{
	SUBCASE("at the start")
	{
		TextBuffer buffer{"world"};
		buffer.Insert(0, "hello ");

		CHECK(buffer.Materialize() == "hello world");
		CHECK(buffer.GetLength() == 11);
	}

	SUBCASE("in the middle")
	{
		TextBuffer buffer{"held"};
		buffer.Insert(3, "lo wor");

		CHECK(buffer.Materialize() == "hello word");
	}

	SUBCASE("at the end")
	{
		TextBuffer buffer{"hello"};
		buffer.Insert(5, " world");

		CHECK(buffer.Materialize() == "hello world");
	}
}

TEST_CASE("Inserting into an empty buffer works")
{
	TextBuffer buffer;
	buffer.Insert(0, "first");

	CHECK(buffer.Materialize() == "first");
}

TEST_CASE("Inserting nothing changes nothing")
{
	TextBuffer buffer{"hello"};
	const std::string_view nothing = "";
	buffer.Insert(2, nothing);

	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.GetLength() == 5);
}

TEST_CASE("Typing one byte after another reads back in order")
{
	TextBuffer buffer;
	const std::string expectedText = "abcdef";

	for (std::size_t keystroke = 0; keystroke < expectedText.size(); ++keystroke)
	{
		const char typedLetter = static_cast<char>('a' + keystroke);
		buffer.Insert(keystroke, std::string(1, typedLetter));
	}

	CHECK(buffer.Materialize() == expectedText);
}

TEST_CASE("Erasing takes out exactly the range asked for")
{
	SUBCASE("within one piece")
	{
		TextBuffer buffer{"hello world"};
		buffer.Erase(5, 11);

		CHECK(buffer.Materialize() == "hello");
	}

	SUBCASE("from the start")
	{
		TextBuffer buffer{"hello world"};
		buffer.Erase(0, 6);

		CHECK(buffer.Materialize() == "world");
	}

	SUBCASE("everything")
	{
		TextBuffer buffer{"hello"};
		buffer.Erase(0, 5);

		CHECK(buffer.Materialize() == "");
		CHECK(buffer.IsEmpty());
	}
}

TEST_CASE("Erasing across a piece boundary joins what is left on either side")
{
	TextBuffer buffer{"hello"};
	const std::size_t endOfOriginalPiece = 5;
	buffer.Insert(endOfOriginalPiece, " world");

	const std::size_t insideOriginalPiece = 3;
	const std::size_t insideInsertedPiece = 8;
	buffer.Erase(insideOriginalPiece, insideInsertedPiece);

	CHECK(buffer.Materialize() == "helrld");
}

TEST_CASE("Erasing whole pieces out of the middle drops them")
{
	TextBuffer buffer{"AZ"};
	buffer.Insert(1, "B");
	buffer.Insert(2, "C");
	buffer.Insert(3, "D");
	REQUIRE(buffer.Materialize() == "ABCDZ");

	buffer.Erase(1, 4);

	CHECK(buffer.Materialize() == "AZ");
}

TEST_CASE("Erasing an empty or reversed range changes nothing")
{
	TextBuffer buffer{"hello"};

	buffer.Erase(2, 2);
	CHECK(buffer.Materialize() == "hello");

	buffer.Erase(4, 1);
	CHECK(buffer.Materialize() == "hello");
}

TEST_CASE("Erasing past the end stops at the end instead of reading off it")
{
	TextBuffer buffer{"hello"};
	buffer.Erase(3, 99);

	CHECK(buffer.Materialize() == "hel");
}

TEST_CASE("A substring spans pieces without a seam")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");

	CHECK(buffer.Substring(3, 8) == "lo wo");
	CHECK(buffer.Substring(0, buffer.GetLength()) == "hello world");
}

TEST_CASE("A substring outside the content comes back empty rather than reading off the end")
{
	TextBuffer buffer{"hello"};

	CHECK(buffer.Substring(3, 99) == "lo");
	CHECK(buffer.Substring(99, 120) == "");
	CHECK(buffer.Substring(4, 2) == "");
}

TEST_CASE("Individual bytes can be read across pieces")
{
	TextBuffer buffer{"ab"};
	buffer.Insert(1, "XY");
	CHECK(buffer.Materialize() == "aXYb");

	CHECK(buffer.CharAt(0) == 'a');
	CHECK(buffer.CharAt(1) == 'X');
	CHECK(buffer.CharAt(2) == 'Y');
	CHECK(buffer.CharAt(3) == 'b');
	CHECK(buffer.CharAt(4) == '\0');
	CHECK(buffer.CharAt(99) == '\0');
}

TEST_CASE("Many edits spread over many pieces still read back correctly so a wrong piece start cannot hide behind a single piece")
{
	constexpr std::size_t kInsertionCount = 200;
	constexpr std::size_t kAlphabetLength = 26;

	TextBuffer buffer;
	std::string expected;

	for (std::size_t insertion = 0; insertion < kInsertionCount; ++insertion)
	{
		const std::string fragment(1, static_cast<char>('a' + insertion % kAlphabetLength));
		const std::size_t middleOffset = expected.size() / 2;

		buffer.Insert(middleOffset, fragment);
		expected.insert(middleOffset, fragment);
	}

	CHECK(buffer.Materialize() == expected);
	CHECK(buffer.GetLength() == expected.size());
}

TEST_CASE("An empty document is one empty line")
{
	const TextBuffer buffer;

	CHECK(buffer.GetLineCount() == 1);
	CHECK(buffer.LineText(0) == "");
	CHECK(buffer.LineStart(0) == 0);
	CHECK(buffer.LineLength(0) == 0);
}

TEST_CASE("A trailing newline opens a line rather than closing the file")
{
	const TextBuffer buffer{"a\nb\n"};

	CHECK(buffer.GetLineCount() == 3);
	CHECK(buffer.LineStart(0) == 0);
	CHECK(buffer.LineStart(1) == 2);
	CHECK(buffer.LineStart(2) == 4);
	CHECK(buffer.LineText(2) == "");
}

TEST_CASE("A file with no trailing newline has no empty line at the end")
{
	const TextBuffer buffer{"a\nb"};

	CHECK(buffer.GetLineCount() == 2);
	CHECK(buffer.LineText(1) == "b");
}

TEST_CASE("A line's text excludes the newline that ends it")
{
	const TextBuffer buffer{"first\nsecond\nthird"};

	CHECK(buffer.LineText(0) == "first");
	CHECK(buffer.LineText(1) == "second");
	CHECK(buffer.LineText(2) == "third");
	CHECK(buffer.LineLength(1) == 6);
}

TEST_CASE("Consecutive newlines make empty lines")
{
	const TextBuffer buffer{"\n\n\n"};

	CHECK(buffer.GetLineCount() == 4);

	for (std::size_t line = 0; line < 4; ++line)
	{
		CHECK(buffer.LineText(line) == "");
	}
}

TEST_CASE("Asking about a line that is not there gives the end rather than reading off it")
{
	const TextBuffer buffer{"a\nb"};

	CHECK(buffer.LineStart(99) == buffer.GetLength());
	CHECK(buffer.LineLength(99) == 0);
	CHECK(buffer.LineText(99) == "");
}

TEST_CASE("Offsets and positions convert both ways")
{
	const TextBuffer buffer{"first\nsecond\nthird"};
	const std::size_t newlineEndingFirstLine = 5;
	const std::size_t firstByteOfSecondLine = 6;

	CHECK(buffer.ToPosition(0) == Position{.Line = 0, .Column = 0});
	CHECK(buffer.ToPosition(3) == Position{.Line = 0, .Column = 3});
	CHECK(buffer.ToPosition(newlineEndingFirstLine) == Position{.Line = 0, .Column = 5});
	CHECK(buffer.ToPosition(firstByteOfSecondLine) == Position{.Line = 1, .Column = 0});
	CHECK(buffer.ToPosition(13) == Position{.Line = 2, .Column = 0});

	CHECK(buffer.ToOffset(Position{.Line = 0, .Column = 0}) == 0);
	CHECK(buffer.ToOffset(Position{.Line = 1, .Column = 0}) == 6);
	CHECK(buffer.ToOffset(Position{.Line = 2, .Column = 4}) == 17);
}

TEST_CASE("A position past the end of a line is pulled back to the line's end")
{
	const TextBuffer buffer{"ab\ncdef"};

	CHECK(buffer.ToOffset(Position{.Line = 0, .Column = 99}) == 2);
	CHECK(buffer.ToOffset(Position{.Line = 1, .Column = 99}) == 7);
	CHECK(buffer.ToOffset(Position{.Line = 99, .Column = 0}) == buffer.GetLength());
}

TEST_CASE("Every offset round-trips through a position")
{
	const TextBuffer buffer{"first\n\nthird line\nfourth"};

	for (std::size_t offset = 0; offset <= buffer.GetLength(); ++offset)
	{
		CHECK(buffer.ToOffset(buffer.ToPosition(offset)) == offset);
	}
}

TEST_CASE("The line index survives insertion")
{
	SUBCASE("of plain text within a line")
	{
		TextBuffer buffer{"first\nsecond"};
		buffer.Insert(2, "XX");

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 2);
		CHECK(buffer.LineText(0) == "fiXXrst");
		CHECK(buffer.LineStart(1) == 8);
	}

	SUBCASE("of a newline, which splits a line in two")
	{
		TextBuffer buffer{"abcd"};
		buffer.Insert(2, "\n");

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 2);
		CHECK(buffer.LineText(0) == "ab");
		CHECK(buffer.LineText(1) == "cd");
	}

	SUBCASE("of several lines at once, as a paste would")
	{
		TextBuffer buffer{"start\nend"};
		buffer.Insert(6, "one\ntwo\nthree\n");

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 5);
		CHECK(buffer.LineText(1) == "one");
		CHECK(buffer.LineText(3) == "three");
		CHECK(buffer.LineText(4) == "end");
	}

	SUBCASE("at the very start of a line")
	{
		TextBuffer buffer{"a\nb"};
		buffer.Insert(2, "X");

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.LineText(1) == "Xb");
	}

	SUBCASE("at the very end of the document")
	{
		TextBuffer buffer{"a\nb"};
		buffer.Insert(3, "\nc");

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 3);
		CHECK(buffer.LineText(2) == "c");
	}
}

TEST_CASE("The line index survives erasure")
{
	SUBCASE("within a line")
	{
		TextBuffer buffer{"first\nsecond"};
		buffer.Erase(1, 3);

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.LineText(0) == "fst");
		CHECK(buffer.LineStart(1) == 4);
	}

	SUBCASE("of a newline, which joins two lines")
	{
		TextBuffer buffer{"ab\ncd"};
		buffer.Erase(2, 3);

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 1);
		CHECK(buffer.LineText(0) == "abcd");
	}

	SUBCASE("across several lines")
	{
		TextBuffer buffer{"one\ntwo\nthree\nfour"};
		buffer.Erase(2, 12);

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 2);
		CHECK(buffer.LineText(0) == "one");
		CHECK(buffer.LineText(1) == "four");
	}

	SUBCASE("of everything")
	{
		TextBuffer buffer{"one\ntwo\n"};
		buffer.Erase(0, buffer.GetLength());

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 1);
		CHECK(buffer.LineText(0) == "");
	}

	SUBCASE("of a trailing newline")
	{
		TextBuffer buffer{"one\n"};
		buffer.Erase(3, 4);

		CHECK(buffer.LineIndexMatchesRebuild());
		CHECK(buffer.GetLineCount() == 1);
	}
}

TEST_CASE("Erasing whole lines from the start of a line keeps that line start and drops the one the erasure ends on")
{
	TextBuffer buffer{"ab\ncd\nef"};
	const std::size_t startOfSecondLine = 3;
	const std::size_t startOfThirdLine = 6;

	buffer.Erase(startOfSecondLine, startOfThirdLine);

	CHECK(buffer.LineIndexMatchesRebuild());
	CHECK(buffer.Materialize() == "ab\nef");
	CHECK(buffer.GetLineCount() == 2);
	CHECK(buffer.LineStart(1) == startOfSecondLine);
	CHECK(buffer.LineText(1) == "ef");
}

TEST_CASE("An edit far down the document leaves the lines above it alone")
{
	constexpr std::size_t kLineCount = 500;
	const std::size_t lineAboveEdit = 100;
	const std::size_t editedLine = 400;
	const std::size_t lineCountWithTrailingEmptyLine = kLineCount + 1;
	const std::size_t lineCountAfterInsertingOneNewline = lineCountWithTrailingEmptyLine + 1;

	std::string content;

	for (std::size_t line = 0; line < kLineCount; ++line)
	{
		content += "line " + std::to_string(line) + "\n";
	}

	TextBuffer buffer{content};
	const std::size_t lineAboveEditStartBefore = buffer.LineStart(lineAboveEdit);

	buffer.Insert(buffer.LineStart(editedLine), "inserted\n");

	CHECK(buffer.LineIndexMatchesRebuild());
	CHECK(buffer.LineStart(lineAboveEdit) == lineAboveEditStartBefore);
	CHECK(buffer.GetLineCount() == lineCountAfterInsertingOneNewline);
}

TEST_CASE("A fresh buffer has nothing to undo or redo")
{
	TextBuffer buffer{"hello"};

	CHECK_FALSE(buffer.CanUndo());
	CHECK_FALSE(buffer.CanRedo());
	CHECK_FALSE(buffer.Undo().has_value());
	CHECK_FALSE(buffer.Redo().has_value());
	CHECK(buffer.Materialize() == "hello");
}

TEST_CASE("Undo reverses an insertion")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");

	REQUIRE(buffer.CanUndo());
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Undo reverses an erasure")
{
	TextBuffer buffer{"hello world"};
	buffer.Erase(5, 11);
	REQUIRE(buffer.Materialize() == "hello");

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "hello world");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Redo reapplies what undo took away")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");
	buffer.Undo();

	REQUIRE(buffer.CanRedo());
	CHECK(buffer.Redo().has_value());
	CHECK(buffer.Materialize() == "hello world");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Undo reports the caret where a taken-away insertion began and at the end of a put-back deletion")
{
	TextBuffer inserted{"hello"};
	inserted.Insert(5, " world");

	const std::optional<std::size_t> caretAfterUndoingInsertion = inserted.Undo();

	REQUIRE(caretAfterUndoingInsertion.has_value());
	CHECK(*caretAfterUndoingInsertion == 5);

	TextBuffer erased{"hello world"};
	erased.Erase(5, 11);

	const std::optional<std::size_t> caretAfterUndoingDeletion = erased.Undo();

	REQUIRE(caretAfterUndoingDeletion.has_value());
	CHECK(*caretAfterUndoingDeletion == 11);
}

TEST_CASE("Redo reports the caret at the end of a put-back insertion and where a taken-away deletion began")
{
	TextBuffer inserted{"hello"};
	inserted.Insert(5, " world");
	inserted.Undo();

	const std::optional<std::size_t> caretAfterRedoingInsertion = inserted.Redo();

	REQUIRE(caretAfterRedoingInsertion.has_value());
	CHECK(*caretAfterRedoingInsertion == 11);

	TextBuffer erased{"hello world"};
	erased.Erase(5, 11);
	erased.Undo();

	const std::optional<std::size_t> caretAfterRedoingDeletion = erased.Redo();

	REQUIRE(caretAfterRedoingDeletion.has_value());
	CHECK(*caretAfterRedoingDeletion == 5);
}

TEST_CASE("Undoing a group reports the caret at its oldest edit which is where the user is looking")
{
	TextBuffer buffer;
	buffer.Insert(0, "a");
	buffer.Insert(1, "b");

	const std::optional<std::size_t> caretAfterUndo = buffer.Undo();

	CHECK(buffer.Materialize().empty());
	REQUIRE(caretAfterUndo.has_value());
	CHECK(*caretAfterUndo == 0);
}

TEST_CASE("A run of edits undoes and redoes in order")
{
	TextBuffer buffer;
	buffer.Insert(0, "one");
	buffer.BreakUndoGroup();
	buffer.Insert(3, " two");
	buffer.BreakUndoGroup();
	buffer.Insert(7, " three");
	REQUIRE(buffer.Materialize() == "one two three");

	buffer.Undo();
	CHECK(buffer.Materialize() == "one two");
	buffer.Undo();
	CHECK(buffer.Materialize() == "one");
	buffer.Undo();
	CHECK(buffer.Materialize() == "");
	CHECK_FALSE(buffer.CanUndo());

	buffer.Redo();
	CHECK(buffer.Materialize() == "one");
	buffer.Redo();
	buffer.Redo();
	CHECK(buffer.Materialize() == "one two three");
	CHECK_FALSE(buffer.CanRedo());
}

TEST_CASE("Undo restores the line index, not just the content")
{
	TextBuffer buffer{"one\ntwo\nthree"};
	buffer.Erase(3, 8);
	CHECK(buffer.Materialize() == "onethree");
	REQUIRE(buffer.GetLineCount() == 1);

	buffer.Undo();

	CHECK(buffer.GetLineCount() == 3);
	CHECK(buffer.LineText(1) == "two");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("A new edit throws away what could have been redone")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");
	buffer.Undo();
	REQUIRE(buffer.CanRedo());

	buffer.Insert(5, "!");

	CHECK_FALSE(buffer.CanRedo());
	CHECK(buffer.Materialize() == "hello!");
}

TEST_CASE("The revision changes after a save then an undo then new typing even though the undo stack is back to the same depth")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");

	const std::size_t revisionAtSave = buffer.GetRevision();

	buffer.Undo();
	CHECK(buffer.GetRevision() != revisionAtSave);

	buffer.Insert(5, "!");
	CHECK(buffer.GetRevision() != revisionAtSave);

	buffer.Undo();
	buffer.Redo();
	CHECK(buffer.GetRevision() != revisionAtSave);
}

TEST_CASE("Undoing back to a remembered revision reports that revision again")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, "!");

	const std::size_t rememberedRevision = buffer.GetRevision();

	buffer.BreakUndoGroup();
	buffer.Insert(6, "?");
	REQUIRE(buffer.GetRevision() != rememberedRevision);

	buffer.Undo();
	CHECK(buffer.GetRevision() == rememberedRevision);
}

TEST_CASE("A buffer nobody has edited has revision zero")
{
	const TextBuffer buffer{"hello"};

	CHECK(buffer.GetRevision() == 0);
}

TEST_CASE("Random edits from a fixed seed match a string oracle and keep the line index correct and undo all the way back")
{
	constexpr std::mt19937::result_type kFixedSeed = 20260808u;
	constexpr std::size_t kStepCount = 400;

	std::mt19937 random{kFixedSeed};

	const std::string initial = "alpha\nbeta\n\ngamma line\ndelta";
	TextBuffer buffer{initial};
	std::string oracle = initial;

	for (std::size_t step = 0; step < kStepCount; ++step)
	{
		const bool inserting = ChooseToInsert(random, oracle);

		if (inserting)
		{
			const std::size_t insertionOffset = RandomInsertionOffset(random, oracle.size());
			const std::string& fragment = RandomFragment(random);

			buffer.Insert(insertionOffset, fragment);
			oracle.insert(insertionOffset, fragment);
		}
		else
		{
			const auto [from, to] = RandomRangeWithin(random, oracle.size());

			buffer.Erase(from, to);
			oracle.erase(from, to - from);
		}

		REQUIRE(buffer.Materialize() == oracle);
		REQUIRE(buffer.GetLength() == oracle.size());
		REQUIRE(buffer.LineIndexMatchesRebuild());
	}

	while (buffer.CanUndo())
	{
		buffer.Undo();
	}

	CHECK(buffer.Materialize() == initial);
	CHECK(buffer.LineIndexMatchesRebuild());
	CHECK(buffer.GetRevision() == 0);
}

TEST_CASE("Random edits interleaved with undo and redo match a string oracle at every step")
{
	constexpr std::mt19937::result_type kFixedSeed = 20260810u;
	constexpr std::size_t kStepCount = 400;
	constexpr std::size_t kActionCount = 3;
	constexpr std::size_t kUndoAction = 1;
	constexpr std::size_t kRedoAction = 2;

	std::mt19937 random{kFixedSeed};

	const std::string initial = "alpha\nbeta\n\ngamma line\ndelta";
	TextBuffer buffer{initial};
	std::string oracle = initial;

	std::vector<std::string> contentAfterEachUndo;
	std::vector<std::string> contentAfterEachRedo;

	for (std::size_t step = 0; step < kStepCount; ++step)
	{
		const std::size_t action = random() % kActionCount;
		const bool undoChosen = action == kUndoAction;
		const bool redoChosen = action == kRedoAction;

		if (undoChosen && buffer.CanUndo())
		{
			REQUIRE(buffer.Undo().has_value());
			contentAfterEachRedo.push_back(oracle);
			oracle = contentAfterEachUndo.back();
			contentAfterEachUndo.pop_back();
		}
		else if (redoChosen && buffer.CanRedo())
		{
			REQUIRE(buffer.Redo().has_value());
			contentAfterEachUndo.push_back(oracle);
			oracle = contentAfterEachRedo.back();
			contentAfterEachRedo.pop_back();
		}
		else
		{
			const bool inserting = ChooseToInsert(random, oracle);

			if (inserting)
			{
				const std::size_t insertionOffset = RandomInsertionOffset(random, oracle.size());
				const std::string& fragment = RandomFragment(random);

				buffer.Insert(insertionOffset, fragment);

				const bool bufferRecordsInsertion = !fragment.empty();

				if (bufferRecordsInsertion)
				{
					contentAfterEachUndo.push_back(oracle);
					contentAfterEachRedo.clear();
					oracle.insert(insertionOffset, fragment);
				}
			}
			else
			{
				const auto [from, to] = RandomRangeWithin(random, oracle.size());

				buffer.Erase(from, to);

				const bool bufferRecordsErasure = from < to;

				if (bufferRecordsErasure)
				{
					contentAfterEachUndo.push_back(oracle);
					contentAfterEachRedo.clear();
					oracle.erase(from, to - from);
				}
			}

			// Without the break, two edits that land side by side coalesce into one undo step, and a
			// single Undo would reverse more than this one-edit-per-entry oracle expects.
			buffer.BreakUndoGroup();
		}

		REQUIRE(buffer.Materialize() == oracle);
		REQUIRE(buffer.GetLength() == oracle.size());
		REQUIRE(buffer.LineIndexMatchesRebuild());
	}
}

TEST_CASE("Every offset still round-trips through a position after random edits")
{
	constexpr std::mt19937::result_type kFixedSeed = 20260809u;
	constexpr std::size_t kInsertionCount = 100;

	std::mt19937 random{kFixedSeed};

	TextBuffer buffer{"one\ntwo\nthree"};

	for (std::size_t insertion = 0; insertion < kInsertionCount; ++insertion)
	{
		const std::size_t insertionOffset = RandomInsertionOffset(random, buffer.GetLength());
		const bool insertsNewline = (random() % 3) == 0;
		buffer.Insert(insertionOffset, insertsNewline ? "\n" : "z");
	}

	for (std::size_t offset = 0; offset <= buffer.GetLength(); ++offset)
	{
		REQUIRE(buffer.ToOffset(buffer.ToPosition(offset)) == offset);
	}
}

TEST_CASE("Toggling undo/redo over a large paste reuses its appended bytes rather than growing the append buffer")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, std::string(kLargeTextLength, 'x'));

	buffer.Undo();
	buffer.Redo();
	const std::size_t afterFirstCycle = buffer.GetAppendBufferSize();

	for (int toggle = 0; toggle < kToggleCount; ++toggle)
	{
		buffer.Undo();
		buffer.Redo();
	}

	CHECK(buffer.GetAppendBufferSize() == afterFirstCycle);
	CHECK(buffer.Materialize() == "hello" + std::string(kLargeTextLength, 'x'));
}

TEST_CASE("Toggling undo/redo on an erasure does not grow the append buffer")
{
	TextBuffer buffer{"hello" + std::string(kLargeTextLength, 'x') + "world"};
	const std::size_t erasedRunStart = 5;
	buffer.Erase(erasedRunStart, erasedRunStart + kLargeTextLength);
	REQUIRE(buffer.Materialize() == "helloworld");

	buffer.Undo();
	buffer.Redo();
	const std::size_t afterFirstCycle = buffer.GetAppendBufferSize();

	for (int toggle = 0; toggle < kToggleCount; ++toggle)
	{
		buffer.Undo();
		buffer.Redo();
	}

	CHECK(buffer.GetAppendBufferSize() == afterFirstCycle);
	CHECK(buffer.Materialize() == "helloworld");
}

TEST_CASE("Edits inside a transaction undo as one")
{
	TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "one TWO three");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "one two three");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Redo reapplies a whole transaction in the original order")
{
	TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Redo().has_value());
	CHECK(buffer.Materialize() == "one TWO three");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Nested transactions close only at the outermost end")
{
	TextBuffer buffer{"ab"};

	buffer.BeginTransaction();
	buffer.Insert(1, "X");
	buffer.BeginTransaction();
	buffer.Insert(2, "Y");
	buffer.EndTransaction();
	buffer.Insert(3, "Z");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "aXYZb");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "ab");
	CHECK_FALSE(buffer.CanUndo());
}

TEST_CASE("Consecutive typing undoes as one word")
{
	TextBuffer buffer;
	const std::string_view typedWord = "hello";

	for (std::size_t keystroke = 0; keystroke < typedWord.size(); ++keystroke)
	{
		buffer.BeginTransaction();
		buffer.Insert(keystroke, typedWord.substr(keystroke, 1));
		buffer.EndTransaction();
	}

	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("BreakUndoGroup splits typing into two steps")
{
	TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "ab");
	buffer.EndTransaction();

	buffer.BreakUndoGroup();

	buffer.BeginTransaction();
	buffer.Insert(2, "cd");
	buffer.EndTransaction();

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "ab");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("Typing that is not contiguous starts a new group")
{
	TextBuffer buffer{"xy"};

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(3, "b");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "axyb");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "axy");
}

TEST_CASE("A newline ends the group it lands in")
{
	TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "a\n");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(2, "b");
	buffer.EndTransaction();

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "a\n");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Backspaces coalesce and so do forward deletes")
{
	TextBuffer backspaces{"abcd"};

	for (std::size_t caretOffset = 4; caretOffset > 1; --caretOffset)
	{
		backspaces.BeginTransaction();
		backspaces.Erase(caretOffset - 1, caretOffset);
		backspaces.EndTransaction();
	}

	CHECK(backspaces.Materialize() == "a");
	CHECK(backspaces.Undo().has_value());
	CHECK(backspaces.Materialize() == "abcd");

	TextBuffer forwards{"abcd"};

	for (int forwardDelete = 0; forwardDelete < 3; ++forwardDelete)
	{
		forwards.BeginTransaction();
		forwards.Erase(1, 2);
		forwards.EndTransaction();
	}

	CHECK(forwards.Materialize() == "a");
	CHECK(forwards.Undo().has_value());
	CHECK(forwards.Materialize() == "abcd");
}

TEST_CASE("Deleting after typing does not join the typed group")
{
	TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "ab");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Erase(1, 2);
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "a");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "ab");
}

TEST_CASE("Coalescing does not grow the append buffer")
{
	TextBuffer buffer;

	for (std::size_t keystroke = 0; keystroke < 4; ++keystroke)
	{
		buffer.BeginTransaction();
		buffer.Insert(keystroke, "x");
		buffer.EndTransaction();
	}

	const std::size_t appendBufferSizeAfterTyping = buffer.GetAppendBufferSize();

	for (int toggle = 0; toggle < 3; ++toggle)
	{
		CHECK(buffer.Undo().has_value());
		CHECK(buffer.Redo().has_value());
	}

	CHECK(buffer.GetAppendBufferSize() == appendBufferSizeAfterTyping);
}

TEST_CASE("A newline landing on a transaction's later edit still closes the group")
{
	TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.Insert(1, "\n");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(1, "b");
	buffer.EndTransaction();

	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "a\n");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Typing after an erase-then-insert transaction coalesces with the insert")
{
	TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(7, "!");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "one TWO! three");
	CHECK(buffer.Undo().has_value());
	CHECK(buffer.Materialize() == "one two three");
}

TEST_CASE("A line that lies in one piece is viewed without a copy")
{
	const TextBuffer buffer{"first\nsecond line\nthird"};
	std::string copyWhenSplit;

	CHECK(buffer.LineTextView(1, copyWhenSplit) == "second line");
	CHECK(buffer.LineTextView(2, copyWhenSplit) == "third");
	CHECK(copyWhenSplit.empty());
}

TEST_CASE("A line that an edit split across pieces is viewed through a copy of it")
{
	TextBuffer buffer{"first\nsecond line\nthird"};
	buffer.Insert(9, "-inserted-");
	std::string copyWhenSplit;

	CHECK(buffer.LineTextView(1, copyWhenSplit) == "sec-inserted-ond line");
	CHECK(copyWhenSplit == "sec-inserted-ond line");
	CHECK(buffer.LineTextView(1, copyWhenSplit) == buffer.LineText(1));
}

TEST_CASE("An empty line is viewed as empty")
{
	const TextBuffer buffer{"a\n\nb"};
	std::string copyWhenSplit;

	CHECK(buffer.LineTextView(1, copyWhenSplit).empty());
}