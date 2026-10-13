#pragma once

#include "Review/ReviewSessions.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

#include <cstdint>
#include <filesystem>

namespace Graphite {

	// The open folder's review sessions and the file they live in, `.graphite/reviews.json`. Kept apart
	// from workspace.json, which holds the window layout and is rewritten from a snapshot every frame:
	// these are data the reader made.
	//
	// Written once the sessions have stood unchanged for kSaveDelaySeconds, so a crash loses at most
	// that much and ticking through a list does not write a file per click, and at once on Close.
	//
	// A file that holds sessions this build could not take in is never simply written over: one that
	// does not parse, or is of another version, is first copied to `reviews.json.bak`, and one that
	// could not be read at all is not written until the folder is opened again.
	class ReviewStore : Core::NonCopyable
	{
	public:
		static constexpr float kSaveDelaySeconds = 2.0f;

		static std::filesystem::path FileFor(const std::filesystem::path& root);

		// A missing, unreadable, malformed or other-version file leaves no sessions.
		void Open(std::filesystem::path root);

		// Writes what is unsaved, then forgets the folder and its sessions.
		void Close();

		// The folder was moved or renamed while open: later writes go to its new place.
		void MoveTo(std::filesystem::path root);

		void Update(Core::Timestep ts);

		// Writes now if anything is unsaved.
		void Flush();

		ReviewSessions& GetSessions() { return m_Sessions; }
		const ReviewSessions& GetSessions() const { return m_Sessions; }

	private:
		// What may be done to the file Open found.
		enum class FileOnOpen
		{
			// Absent, or read: written over as it is.
			Writable,

			// Not parsed, or of another version: copied aside before the first write over it.
			CopyAsideFirst,

			// There but not readable: what it holds is unknown, so it is never written.
			LeftAlone
		};

		// False, saying so once, when the file Open could not take in is still there and could not
		// be copied: it must not be written over then.
		bool CopyAsideBeforeFirstWrite(const std::filesystem::path& file);

		std::filesystem::path m_Root;
		ReviewSessions m_Sessions;

		FileOnOpen m_FileOnOpen = FileOnOpen::Writable;
		ReviewsText m_TextOnOpen = ReviewsText::Read;
		bool m_CopyFailureLogged = false;

		std::uint64_t m_SavedVersion = 0;
		std::uint64_t m_SeenVersion = 0;
		float m_UnchangedFor = 0.0f;
	};
}
