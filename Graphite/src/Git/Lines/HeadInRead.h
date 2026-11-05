#pragma once

#include "Core/Git/Head.h"

#include <string_view>

namespace Graphite {

	// How HEAD's history stands against the line read on screen. The read lists the branches as they were when it was
	// read, so after a checkout it can name another branch checked out than HEAD now does.
	enum class HeadInRead
	{
		Agrees,       // the read's checked-out branch is HEAD's: its line is HEAD's history's
		WaitsForRead, // they differ while a read is coming: HEAD's history waits for it
		Differs       // they differ and no read is coming: HEAD's history is shown alone, with no line
	};

	struct HeadInReadInput
	{
		const Core::HeadState* Head = nullptr; // null while unread and after a failed read
		std::string_view ReadCheckedOut;       // the full name of the read's checked-out branch; empty when it names none
		bool ReadComing = false;               // a line read, or the branch listing one is made from, is on its way

		// The last line read failed. A read asked after it, and the listing of a refresh, is background work that changes
		// nothing on screen until it lands, so nothing waits for it.
		bool ReadFailed = false;
	};

	HeadInRead PlaceHeadInRead(const HeadInReadInput& input);

	// What decides whether a history waits for a line read before its first rows.
	struct HistoryWaitInput
	{
		bool HasRead = false;        // a line read is in hand
		bool Reading = false;        // a line read is on its way
		bool ReadFailed = false;     // the last line read failed; one asked after it is background work
		bool BranchesFailed = false; // the branches could not be listed, so no first read is coming
		bool ReadLacksShown = false; // the read does not hold the reference shown, as one just made or renamed
		HeadInRead Head = HeadInRead::Agrees; // HEAD's history only; Agrees for any other
	};

	// Waits for a first read that is coming, for the read that names HEAD's branch checked out, and for a read that may
	// hold the reference shown. A failure stays said until a read succeeds, so nothing waits while one is held.
	bool HistoryWaitsForLines(const HistoryWaitInput& input);

	// What a history's list draws once its page is in hand.
	enum class HistoryRows
	{
		Waiting, // the waiting row, until the history's first rows can come with what they are drawn with
		Shown,   // the page's rows, with the marks and the header words of the line now shown
		Kept     // the rows on screen, shown before the history named a line, with nothing of the line yet
	};

	struct HistoryRowsInput
	{
		bool ShownForThis = false;    // the rows on screen were shown for this history and the branch it names now
		bool ShownWithNoLine = false; // they were shown for this history while it named no branch: HEAD's, with no read in hand or the read failing
		bool FirstRowsReady = false;  // the page asked with the line now shown has landed, and the counts its rows wait for
	};

	// Rows on screen stay while they show the same branch. HEAD's rows shown with no line stay when a read names HEAD's
	// branch, until the page asked with its line can take their place in one frame.
	HistoryRows RowsOfHistory(const HistoryRowsInput& input);
}
