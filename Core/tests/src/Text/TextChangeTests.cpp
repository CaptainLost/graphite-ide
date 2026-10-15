#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextChange.h"

#include <optional>
#include <span>
#include <string>

using namespace Core;

namespace {
	std::span<const TextChange> AllChanges(const TextBuffer& buffer)
	{
		const std::optional<std::span<const TextChange>> changes = buffer.ChangesSince(0);
		REQUIRE(changes.has_value());

		return *changes;
	}
}

TEST_CASE("Every insertion adds one change with its lines")
{
	TextBuffer buffer{"one\ntwo\nthree"};
	CHECK(buffer.GetChangeSequence() == 0);

	buffer.Insert(4, "new\n");

	REQUIRE(buffer.GetChangeSequence() == 1);
	const std::span<const TextChange> changes = AllChanges(buffer);
	REQUIRE(changes.size() == 1);
	CHECK(changes[0].Sequence == 1);
	CHECK(changes[0].Offset == 4);
	CHECK(changes[0].RemovedLength == 0);
	CHECK(changes[0].InsertedLength == 4);
	CHECK(changes[0].FirstLine == 1);
	CHECK(changes[0].RemovedLines == 0);
	CHECK(changes[0].InsertedLines == 1);
	CHECK(changes[0].LineCountAfter == 4);
	CHECK_FALSE(changes[0].WholeContent);
}

TEST_CASE("An erase records the lines it took away")
{
	TextBuffer buffer{"one\ntwo\nthree"};

	buffer.Erase(2, 9);

	REQUIRE(buffer.Materialize() == "onhree");
	const std::span<const TextChange> changes = AllChanges(buffer);
	REQUIRE(changes.size() == 1);
	CHECK(changes[0].Offset == 2);
	CHECK(changes[0].RemovedLength == 7);
	CHECK(changes[0].FirstLine == 0);
	CHECK(changes[0].RemovedLines == 2);
	CHECK(changes[0].InsertedLines == 0);
	CHECK(changes[0].LineCountAfter == 1);
}

TEST_CASE("Undo and redo add changes of their own")
{
	TextBuffer buffer{"abc"};
	buffer.Insert(3, "\nd");

	REQUIRE(buffer.Undo().has_value());
	REQUIRE(buffer.Redo().has_value());

	CHECK(buffer.GetChangeSequence() == 3);
	const std::optional<std::span<const TextChange>> later = buffer.ChangesSince(1);
	REQUIRE(later.has_value());
	REQUIRE(later->size() == 2);
	CHECK((*later)[0].RemovedLines == 1);
	CHECK((*later)[0].LineCountAfter == 1);
	CHECK((*later)[1].InsertedLines == 1);
	CHECK((*later)[1].LineCountAfter == 2);
}

TEST_CASE("Asking for changes already seen gives none")
{
	TextBuffer buffer{"abc"};
	buffer.Insert(0, "x");

	const std::optional<std::span<const TextChange>> changes = buffer.ChangesSince(buffer.GetChangeSequence());

	REQUIRE(changes.has_value());
	CHECK(changes->empty());
}

TEST_CASE("A reader further behind than the log keeps is told to recompute")
{
	TextBuffer buffer;

	for (std::size_t edit = 0; edit < 3 * kChangeLogCapacity; ++edit)
	{
		buffer.Insert(0, "x");
	}

	CHECK_FALSE(buffer.ChangesSince(0).has_value());

	const std::optional<std::span<const TextChange>> latest = buffer.ChangesSince(buffer.GetChangeSequence() - 1);
	REQUIRE(latest.has_value());
	CHECK(latest->size() == 1);
}

TEST_CASE("Replacing the whole content is one change marked whole")
{
	TextBuffer buffer{"a\nb\nc"};

	buffer.ReplaceContent("x\ny");

	CHECK(buffer.Materialize() == "x\ny");
	const std::span<const TextChange> changes = AllChanges(buffer);
	REQUIRE(changes.size() == 1);
	CHECK(changes[0].WholeContent);
	CHECK(changes[0].RemovedLength == 5);
	CHECK(changes[0].InsertedLength == 3);
	CHECK(changes[0].RemovedLines == 2);
	CHECK(changes[0].InsertedLines == 1);
	CHECK(changes[0].LineCountAfter == 2);
}

TEST_CASE("Replacing the whole content is undone in one step")
{
	TextBuffer buffer{"a\nb"};
	buffer.Insert(3, "!");

	buffer.ReplaceContent("fresh");
	REQUIRE(buffer.Undo().has_value());

	CHECK(buffer.Materialize() == "a\nb!");
}

TEST_CASE("Undo and redo keep an edit's group")
{
	TextBuffer buffer{"abc"};
	buffer.Insert(3, "d");

	const std::optional<std::size_t> group = buffer.GetTopUndoGroup();
	REQUIRE(group.has_value());

	REQUIRE(buffer.Undo().has_value());
	CHECK_FALSE(buffer.GetTopUndoGroup().has_value());
	CHECK(buffer.GetTopRedoGroup() == group);
	CHECK(buffer.HoldsRedoGroup(*group));

	REQUIRE(buffer.Redo().has_value());
	CHECK(buffer.GetTopUndoGroup() == group);
	CHECK(buffer.HoldsUndoGroup(*group));
	CHECK_FALSE(buffer.HoldsRedoGroup(*group));
}

TEST_CASE("The lines a run of changes touched are measured from both ends")
{
	TextBuffer buffer{"0\n1\n2\n3\n4\n5"};

	buffer.Insert(buffer.LineStart(4), "x\n");
	buffer.Insert(buffer.LineStart(1), "y");

	const ChangedLines lines = LinesChangedBy(AllChanges(buffer));

	CHECK(lines.FirstLine == 1);
	CHECK(lines.LastLine == 5);
	CHECK_FALSE(lines.Whole);
}

TEST_CASE("Deleting the last newline leaves the last changed line inside the text")
{
	TextBuffer buffer{"a\n"};

	buffer.Erase(1, 2);

	const ChangedLines lines = LinesChangedBy(AllChanges(buffer));

	CHECK(lines.FirstLine == 0);
	CHECK(lines.LastLine == 0);
}

TEST_CASE("Typing into an empty buffer touches only its first line")
{
	TextBuffer buffer;

	buffer.Insert(0, "abc");

	const ChangedLines lines = LinesChangedBy(AllChanges(buffer));

	CHECK(lines.FirstLine == 0);
	CHECK(lines.LastLine == 0);
}

TEST_CASE("An offset moves past text inserted before it and onto the start of text removed around it")
{
	TextBuffer buffer{"0123456789"};

	buffer.Insert(2, "ab");
	buffer.Erase(6, 9);

	const std::span<const TextChange> changes = AllChanges(buffer);

	CHECK(ShiftOffset(1, changes) == 1);
	CHECK(ShiftOffset(2, changes) == 4);
	CHECK(ShiftOffset(5, changes) == 6);
	CHECK(ShiftOffset(8, changes) == 7);
	CHECK(ShiftOffset(9, changes) == 8);
}
