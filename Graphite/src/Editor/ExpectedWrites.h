#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace Graphite {

	// Paths this application has just written itself, so the editor can reload them without
	// asking. Without it, discarding a file that is open with unsaved changes asks twice:
	// once in the panel that did the discarding, and again in the reload conflict, whose
	// wording is written for a change made behind the user's back.
	//
	// Three rules bound it, and they matter more than the suppression itself, because an
	// entry left lying around would silence a genuine external change later:
	//
	//  - An entry is registered only once its write has succeeded, which narrows the window
	//    to the milliseconds between the write and the watcher noticing it.
	//  - An entry is one-shot: the first check for that path consumes it.
	//  - An entry expires, in case the event never arrives at all.
	class ExpectedWrites
	{
	public:
		// Two seconds, matching kReloadAfterSecondsAway rather than introducing a second,
		// competing idea of how long the application waits for the filesystem to settle.
		static constexpr float kExpectedWriteSeconds = 2.0f;

		void Expect(const std::vector<std::filesystem::path>& paths);

		// True once per registration, and never after the entry has expired. Matches by
		// filename and a native-path suffix rather than exact equality: a git status names a
		// path relative to the working directory ("src/a.txt") while an open document names
		// an absolute one ("D:/repo/src/a.txt"), and the two never compare equal outright.
		bool TakeFor(const std::filesystem::path& path);

		void Tick(float seconds);

		void Clear() { m_Entries.clear(); }

		std::size_t GetCount() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			std::filesystem::path Path;

			// Counts up rather than down so the comparison reads as "older than the
			// deadline", and so a fresh entry starts at the obvious value.
			float Age = 0.0f;
		};

		std::vector<Entry> m_Entries;
	};
}
