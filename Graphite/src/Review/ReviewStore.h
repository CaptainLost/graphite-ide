#pragma once

#include "Review/ReviewSessions.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Graphite {

	// WARNING: This is not workspace.json, which is rewritten from a snapshot every frame; reviews are data the
	// reader made and are saved only after kSaveDelaySeconds without a change.
	class ReviewStore : Core::NonCopyable
	{
	public:
		static constexpr float kSaveDelaySeconds = 2.0f;

		static std::filesystem::path FileFor(const std::filesystem::path& root);

		void Open(std::filesystem::path root);

		void Close();

		void MoveTo(std::filesystem::path root);

		void Update(Core::Timestep timestep);

		void Flush();

		ReviewSessions& GetSessions() { return m_Sessions; }
		const ReviewSessions& GetSessions() const { return m_Sessions; }

	private:
		static constexpr float kNoSecondsUnchanged = 0.0f;

		enum class FileOnOpen
		{
			Writable,
			CopyAsideFirst,
			LeftAlone
		};

		bool CopyAsideBeforeFirstWrite(const std::filesystem::path& file);

		std::filesystem::path m_Root;
		ReviewSessions m_Sessions;

		FileOnOpen m_FileOnOpen = FileOnOpen::Writable;
		ReviewsText m_TextOnOpen = ReviewsText::Read;
		bool m_CopyFailureLogged = false;

		// The text last written, or the encoding of what was read on open. A draft changes the version without changing
		// the text, so a write of this same text is skipped: a glance never rewrites the file nor copies it aside.
		std::string m_LastText;

		std::uint64_t m_SavedVersion = 0;
		std::uint64_t m_SeenVersion = 0;
		float m_UnchangedFor = kNoSecondsUnchanged;
	};
}
