#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"

#include <ostream>
#include <random>
#include <string>

using namespace Core;

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
	buffer.Insert(2, "");

	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.GetLength() == 5);
}

TEST_CASE("Repeated insertion at the same point reads back in order")
{
	// This is what typing looks like: each keystroke inserts one byte after the last.
	TextBuffer buffer;

	for (std::size_t index = 0; index < 6; ++index)
	{
		buffer.Insert(index, std::string(1, static_cast<char>('a' + index)));
	}

	CHECK(buffer.Materialize() == "abcdef");
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
	buffer.Insert(5, " world"); // two pieces: "hello" and " world"
	buffer.Erase(3, 8);			// spans the boundary

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
	buffer.Insert(1, "XY"); // aXYb

	CHECK(buffer.CharAt(0) == 'a');
	CHECK(buffer.CharAt(1) == 'X');
	CHECK(buffer.CharAt(2) == 'Y');
	CHECK(buffer.CharAt(3) == 'b');
	CHECK(buffer.CharAt(4) == '\0');
	CHECK(buffer.CharAt(99) == '\0');
}

TEST_CASE("Many edits still read back correctly")
{
	// Enough pieces that a mistake in the piece starts shows up rather than hiding
	// behind a single-piece document.
	TextBuffer buffer;
	std::string expected;

	for (std::size_t index = 0; index < 200; ++index)
	{
		const std::string fragment(1, static_cast<char>('a' + index % 26));
		const std::size_t at = expected.size() / 2;

		buffer.Insert(at, fragment);
		expected.insert(at, fragment);
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

	CHECK(buffer.ToPosition(0) == Position{0, 0});
	CHECK(buffer.ToPosition(3) == Position{0, 3});
	CHECK(buffer.ToPosition(5) == Position{0, 5}); // on the newline itself
	CHECK(buffer.ToPosition(6) == Position{1, 0}); // first byte of line 1
	CHECK(buffer.ToPosition(13) == Position{2, 0});

	CHECK(buffer.ToOffset(Position{0, 0}) == 0);
	CHECK(buffer.ToOffset(Position{1, 0}) == 6);
	CHECK(buffer.ToOffset(Position{2, 4}) == 17);
}

TEST_CASE("A position past the end of a line is pulled back to the line's end")
{
	const TextBuffer buffer{"ab\ncdef"};

	CHECK(buffer.ToOffset(Position{0, 99}) == 2);
	CHECK(buffer.ToOffset(Position{1, 99}) == 7);
	CHECK(buffer.ToOffset(Position{99, 0}) == buffer.GetLength());
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

TEST_CASE("An edit far down the document leaves the lines above it alone")
{
	std::string content;

	for (std::size_t line = 0; line < 500; ++line)
	{
		content += "line " + std::to_string(line) + "\n";
	}

	TextBuffer buffer{content};
	const std::size_t before = buffer.LineStart(100);

	buffer.Insert(buffer.LineStart(400), "inserted\n");

	CHECK(buffer.LineIndexMatchesRebuild());
	CHECK(buffer.LineStart(100) == before);
	CHECK(buffer.GetLineCount() == 502);
}

TEST_CASE("A fresh buffer has nothing to undo or redo")
{
	TextBuffer buffer{"hello"};

	CHECK_FALSE(buffer.CanUndo());
	CHECK_FALSE(buffer.CanRedo());
	CHECK_FALSE(buffer.Undo());
	CHECK_FALSE(buffer.Redo());
	CHECK(buffer.Materialize() == "hello");
}

TEST_CASE("Undo reverses an insertion")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");

	REQUIRE(buffer.CanUndo());
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Undo reverses an erasure")
{
	TextBuffer buffer{"hello world"};
	buffer.Erase(5, 11);
	REQUIRE(buffer.Materialize() == "hello");

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "hello world");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Redo reapplies what undo took away")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");
	buffer.Undo();

	REQUIRE(buffer.CanRedo());
	CHECK(buffer.Redo());
	CHECK(buffer.Materialize() == "hello world");
	CHECK(buffer.LineIndexMatchesRebuild());
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
	// Erase(3, 8) removes "\ntwo\n" (indices 3..7), leaving "one" + "three" fused into a
	// single line "onethree" with no newline left.
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

TEST_CASE("The revision tells apart two edits that leave the stack the same depth")
{
	// This is the case a stack depth gets wrong: save, undo, type something else. The
	// depth is back to where it was while the content is not.
	TextBuffer buffer{"hello"};
	buffer.Insert(5, " world");

	const std::size_t saved = buffer.GetRevision();

	buffer.Undo();
	CHECK(buffer.GetRevision() != saved);

	buffer.Insert(5, "!");
	CHECK(buffer.GetRevision() != saved);

	buffer.Undo();
	buffer.Redo();
	CHECK(buffer.GetRevision() != saved);
}

TEST_CASE("Undoing back to a remembered revision reports that revision again")
{
	TextBuffer buffer{"hello"};
	buffer.Insert(5, "!");

	const std::size_t saved = buffer.GetRevision();

	buffer.BreakUndoGroup();
	buffer.Insert(6, "?");
	REQUIRE(buffer.GetRevision() != saved);

	buffer.Undo();
	CHECK(buffer.GetRevision() == saved);
}

TEST_CASE("An empty buffer's revision is zero")
{
	const TextBuffer buffer{"hello"};

	CHECK(buffer.GetRevision() == 0);
}

TEST_CASE("Random edits keep the line index correct and undo all the way back")
{
	// The strongest test available here: a std::string is the oracle, the line index is
	// compared against a rebuilt one after every step, and undoing everything has to
	// land exactly where it started. The seed is fixed so a failure reproduces.
	std::mt19937 random{20260808u};

	const std::string initial = "alpha\nbeta\n\ngamma line\ndelta";
	TextBuffer buffer{initial};
	std::string oracle = initial;

	const std::string fragments[] = {"x", "\n", "hello", "a\nb", "\n\n", "", "long fragment here"};

	for (std::size_t step = 0; step < 400; ++step)
	{
		const bool inserting = oracle.empty() || (random() % 2) == 0;

		if (inserting)
		{
			const std::size_t at = random() % (oracle.size() + 1);
			const std::string& fragment = fragments[random() % std::size(fragments)];

			buffer.Insert(at, fragment);
			oracle.insert(at, fragment);
		}
		else
		{
			const std::size_t from = random() % oracle.size();
			const std::size_t to = from + random() % (oracle.size() - from + 1);

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
	// The forwards-only fuzz test above drives edits and undoes everything at the end,
	// so Redo is only ever exercised by the three hand-written cases. This interleaves
	// a fresh edit, an undo, and a redo at every step so a bug reachable only through
	// that interaction — a redo replaying into a document shape it no longer matches —
	// has somewhere to surface. The oracle stacks mirror the buffer's own undo/redo
	// stacks: each holds the full content from just before the edit it would restore.
	std::mt19937 random{20260810u};

	const std::string initial = "alpha\nbeta\n\ngamma line\ndelta";
	TextBuffer buffer{initial};
	std::string oracle = initial;

	std::vector<std::string> undoOracle;
	std::vector<std::string> redoOracle;

	const std::string fragments[] = {"x", "\n", "hello", "a\nb", "\n\n", "", "long fragment here"};

	for (std::size_t step = 0; step < 400; ++step)
	{
		const std::size_t choice = random() % 3;

		if (choice == 1 && buffer.CanUndo())
		{
			REQUIRE(buffer.Undo());
			redoOracle.push_back(oracle);
			oracle = undoOracle.back();
			undoOracle.pop_back();
		}
		else if (choice == 2 && buffer.CanRedo())
		{
			REQUIRE(buffer.Redo());
			undoOracle.push_back(oracle);
			oracle = redoOracle.back();
			redoOracle.pop_back();
		}
		else
		{
			// Either a fresh edit was chosen, or undo/redo was chosen with nothing on
			// the corresponding stack to apply it to — either way, falls back to an
			// edit, exactly as Insert/Erase would clear the redo stack in real use.
			//
			// Insert and Erase both skip the undo push for a no-op (an empty fragment,
			// or an empty/reversed range) — see their early returns — so the oracle
			// stacks must skip it too, or they drift out of step with the buffer's own.
			const bool inserting = oracle.empty() || (random() % 2) == 0;

			if (inserting)
			{
				const std::size_t at = random() % (oracle.size() + 1);
				const std::string& fragment = fragments[random() % std::size(fragments)];

				buffer.Insert(at, fragment);

				if (!fragment.empty())
				{
					undoOracle.push_back(oracle);
					redoOracle.clear();
					oracle.insert(at, fragment);
				}
			}
			else
			{
				const std::size_t from = random() % oracle.size();
				const std::size_t to = from + random() % (oracle.size() - from + 1);

				buffer.Erase(from, to);

				if (from < to)
				{
					undoOracle.push_back(oracle);
					redoOracle.clear();
					oracle.erase(from, to - from);
				}
			}

			// Each fresh edit here stands for a whole, independent gesture as far as the
			// oracle stacks are concerned — they track content one edit at a time. Without
			// this, two edits that happen to land contiguously would coalesce into one
			// undo step and a single Undo() would reverse more than the oracle expects.
			buffer.BreakUndoGroup();
		}

		REQUIRE(buffer.Materialize() == oracle);
		REQUIRE(buffer.GetLength() == oracle.size());
		REQUIRE(buffer.LineIndexMatchesRebuild());
	}
}

TEST_CASE("Every offset still round-trips through a position after random edits")
{
	std::mt19937 random{20260809u};

	TextBuffer buffer{"one\ntwo\nthree"};

	for (std::size_t step = 0; step < 100; ++step)
	{
		const std::size_t at = random() % (buffer.GetLength() + 1);
		buffer.Insert(at, (random() % 3) == 0 ? "\n" : "z");
	}

	for (std::size_t offset = 0; offset <= buffer.GetLength(); ++offset)
	{
		REQUIRE(buffer.ToOffset(buffer.ToPosition(offset)) == offset);
	}
}

TEST_CASE("Toggling undo/redo on an insertion does not grow the append buffer")
{
	// Regression for a leak: replaying an edit used to re-append its text every time,
	// so oscillating undo/redo over a large paste grew m_Append without bound even
	// though the document's content never changed net of the toggle.
	TextBuffer buffer{"hello"};
	buffer.Insert(5, std::string(1000, 'x'));

	buffer.Undo();
	buffer.Redo();
	const std::size_t afterFirstCycle = buffer.GetAppendBufferSize();

	for (int i = 0; i < 200; ++i)
	{
		buffer.Undo();
		buffer.Redo();
	}

	CHECK(buffer.GetAppendBufferSize() == afterFirstCycle);
	CHECK(buffer.Materialize() == "hello" + std::string(1000, 'x'));
}

TEST_CASE("Toggling undo/redo on an erasure does not grow the append buffer")
{
	TextBuffer buffer{"hello" + std::string(1000, 'x') + "world"};
	buffer.Erase(5, 1005);
	REQUIRE(buffer.Materialize() == "helloworld");

	buffer.Undo();
	buffer.Redo();
	const std::size_t afterFirstCycle = buffer.GetAppendBufferSize();

	for (int i = 0; i < 200; ++i)
	{
		buffer.Undo();
		buffer.Redo();
	}

	CHECK(buffer.GetAppendBufferSize() == afterFirstCycle);
	CHECK(buffer.Materialize() == "helloworld");
}

TEST_CASE("Edits inside a transaction undo as one")
{
	Core::TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "one TWO three");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "one two three");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Redo reapplies a whole transaction in the original order")
{
	Core::TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	CHECK(buffer.Undo());
	CHECK(buffer.Redo());
	CHECK(buffer.Materialize() == "one TWO three");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Nested transactions close only at the outermost end")
{
	Core::TextBuffer buffer{"ab"};

	buffer.BeginTransaction();
	buffer.Insert(1, "X");
	buffer.BeginTransaction();
	buffer.Insert(2, "Y");
	buffer.EndTransaction();
	buffer.Insert(3, "Z");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "aXYZb");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "ab");
	CHECK_FALSE(buffer.CanUndo());
}

TEST_CASE("Consecutive typing undoes as one word")
{
	Core::TextBuffer buffer;

	for (std::size_t index = 0; index < 5; ++index)
	{
		buffer.BeginTransaction();
		buffer.Insert(index, std::string_view{"hello"}.substr(index, 1));
		buffer.EndTransaction();
	}

	CHECK(buffer.Materialize() == "hello");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("BreakUndoGroup splits typing into two steps")
{
	Core::TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "ab");
	buffer.EndTransaction();

	buffer.BreakUndoGroup();

	buffer.BeginTransaction();
	buffer.Insert(2, "cd");
	buffer.EndTransaction();

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "ab");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize().empty());
}

TEST_CASE("Typing that is not contiguous starts a new group")
{
	Core::TextBuffer buffer{"xy"};

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(3, "b");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "axyb");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "axy");
}

TEST_CASE("A newline ends the group it lands in")
{
	Core::TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "a\n");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(2, "b");
	buffer.EndTransaction();

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "a\n");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Backspaces coalesce and so do forward deletes")
{
	Core::TextBuffer backspaces{"abcd"};

	for (std::size_t offset = 4; offset > 1; --offset)
	{
		backspaces.BeginTransaction();
		backspaces.Erase(offset - 1, offset);
		backspaces.EndTransaction();
	}

	CHECK(backspaces.Materialize() == "a");
	CHECK(backspaces.Undo());
	CHECK(backspaces.Materialize() == "abcd");

	Core::TextBuffer forwards{"abcd"};

	for (int count = 0; count < 3; ++count)
	{
		forwards.BeginTransaction();
		forwards.Erase(1, 2);
		forwards.EndTransaction();
	}

	CHECK(forwards.Materialize() == "a");
	CHECK(forwards.Undo());
	CHECK(forwards.Materialize() == "abcd");
}

TEST_CASE("Deleting after typing does not join the typed group")
{
	Core::TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "ab");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Erase(1, 2);
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "a");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "ab");
}

TEST_CASE("Coalescing does not grow the append buffer")
{
	Core::TextBuffer buffer;

	for (std::size_t index = 0; index < 4; ++index)
	{
		buffer.BeginTransaction();
		buffer.Insert(index, "x");
		buffer.EndTransaction();
	}

	const std::size_t appended = buffer.GetAppendBufferSize();

	for (int round = 0; round < 3; ++round)
	{
		CHECK(buffer.Undo());
		CHECK(buffer.Redo());
	}

	CHECK(buffer.GetAppendBufferSize() == appended);
}

TEST_CASE("A newline landing on a transaction's later edit still closes the group")
{
	Core::TextBuffer buffer;

	buffer.BeginTransaction();
	buffer.Insert(0, "a");
	buffer.Insert(1, "\n");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(1, "b");
	buffer.EndTransaction();

	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "a\n");
	CHECK(buffer.LineIndexMatchesRebuild());
}

TEST_CASE("Typing after an erase-then-insert transaction coalesces with the insert")
{
	Core::TextBuffer buffer{"one two three"};

	buffer.BeginTransaction();
	buffer.Erase(4, 7);
	buffer.Insert(4, "TWO");
	buffer.EndTransaction();

	buffer.BeginTransaction();
	buffer.Insert(7, "!");
	buffer.EndTransaction();

	CHECK(buffer.Materialize() == "one TWO! three");
	CHECK(buffer.Undo());
	CHECK(buffer.Materialize() == "one two three");
}
