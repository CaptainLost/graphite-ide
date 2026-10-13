#pragma once

#include "Review/ViewedMarks.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

#include <cstdint>
#include <filesystem>

namespace Graphite {

	// The open folder's viewed marks and the file they live in, `.graphite/reviews.json`. Kept apart
	// from workspace.json, which holds the window layout and is rewritten from a snapshot every frame:
	// these are data the reader made.
	//
	// Written once the marks have stood unchanged for kSaveDelaySeconds, so a crash loses at most
	// that much and ticking through a list does not write a file per click, and at once on Close.
	class ReviewStore : Core::NonCopyable
	{
	public:
		static constexpr float kSaveDelaySeconds = 2.0f;

		static std::filesystem::path FileFor(const std::filesystem::path& root);

		// A missing or unreadable file leaves no marks.
		void Open(std::filesystem::path root);

		// Writes what is unsaved, then forgets the folder and its marks.
		void Close();

		// The folder was moved or renamed while open: later writes go to its new place.
		void MoveTo(std::filesystem::path root);

		void Update(Core::Timestep ts);

		// Writes now if anything is unsaved.
		void Flush();

		ViewedMarks& GetMarks() { return m_Marks; }
		const ViewedMarks& GetMarks() const { return m_Marks; }

	private:
		std::filesystem::path m_Root;
		ViewedMarks m_Marks;

		std::uint64_t m_SavedVersion = 0;
		std::uint64_t m_SeenVersion = 0;
		float m_UnchangedFor = 0.0f;
	};
}
