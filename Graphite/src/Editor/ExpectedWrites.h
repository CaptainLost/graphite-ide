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

		// Every path handed in here MUST already be absolute. A relative path cannot be
		// matched safely against a document's absolute one: TakeFor compares whole paths, and
		// a relative expectation would either never match anything (if compared as given) or,
		// if resolved against the wrong base, could match that name anywhere in the tree - the
		// same danger a bare-filename match has. The caller absolutises, deliberately: it is
		// the caller (with the repository root in hand) that knows how to make a git-relative
		// path absolute, not this class.
		void Expect(const std::vector<std::filesystem::path>& paths);

		// True once per registration, and never after the entry has expired. Compares whole
		// paths - lexically normalised and folded case-insensitively, since Windows paths are
		// - rather than a filename or a suffix: either of those can be satisfied by a
		// different file of the same name elsewhere in the tree, which would silence a
		// genuine external change to a dirty buffer the user never asked to discard.
		bool TakeFor(const std::filesystem::path& path);

		void Tick(float seconds);

		void Clear() { m_Entries.clear(); }

		std::size_t GetCount() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			// Stored already normalised (lexically_normal, native separators) so TakeFor
			// compares like for like without renormalising every entry on every query.
			std::filesystem::path Path;

			// Counts up rather than down so the comparison reads as "older than the
			// deadline", and so a fresh entry starts at the obvious value.
			float Age = 0.0f;
		};

		std::vector<Entry> m_Entries;
	};
}
