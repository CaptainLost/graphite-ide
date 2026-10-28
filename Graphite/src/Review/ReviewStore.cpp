#include "Review/ReviewStore.h"

#include "Persistence/FileBackup.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <chrono>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		constexpr const char* kStateDirectory = ".graphite";
		constexpr const char* kReviewsFile = "reviews.json";

		std::string_view WhyNotTakenIn(ReviewsText text)
		{
			switch (text)
			{
			case ReviewsText::Read:
				return "was read";
			case ReviewsText::Malformed:
				return "does not parse";
			case ReviewsText::OtherVersion:
				return "is of a version this build does not know";
			}

			return "does not parse";
		}
	}

	std::filesystem::path ReviewStore::FileFor(const std::filesystem::path& root)
	{
		return root / kStateDirectory / kReviewsFile;
	}

	void ReviewStore::Open(std::filesystem::path root)
	{
		m_Root = std::move(root);
		m_FileOnOpen = FileOnOpen::Writable;
		m_TextOnOpen = ReviewsText::Read;
		m_CopyFailureLogged = false;

		ReviewSessions::Sessions stored;
		const std::filesystem::path file = FileFor(m_Root);

		std::error_code statError;
		const bool fileExists = std::filesystem::exists(file, statError);

		// WARNING: A failed stat is not "nothing is there": taking it for absence would write over a file
		// whose sessions were never read.
		if (statError)
		{
			APP_WARN("Cannot tell whether {} exists: {}; review sessions will not be saved until the folder is opened again",
					 Core::ToUtf8(file), statError.message());
			m_FileOnOpen = FileOnOpen::LeftAlone;
		}
		else if (fileExists)
		{
			Core::Result<std::string> text = Core::ReadFileBytes(file);

			if (text.HasValue())
			{
				DecodedSessions decoded = DecodeReviewSessionsText(text.GetValue());
				stored = std::move(decoded.Sessions);
				m_TextOnOpen = decoded.Text;
				m_FileOnOpen = decoded.Text == ReviewsText::Read ? FileOnOpen::Writable : FileOnOpen::CopyAsideFirst;
			}
			else
			{
				APP_WARN("{}; review sessions will not be saved until the folder is opened again",
						 text.GetError().Message);
				m_FileOnOpen = FileOnOpen::LeftAlone;
			}
		}

		// WARNING: The encoding of what was read, not the file's text: a file that was malformed, or of another version,
		// reads as no sessions, and a glance that leaves none must not count as a change that copies it aside.
		m_LastText = EncodeReviewSessions(stored);
		m_Sessions.Replace(std::move(stored));
		m_SavedVersion = m_Sessions.GetVersion();
		m_SeenVersion = m_SavedVersion;
		m_UnchangedFor = kNoSecondsUnchanged;
	}

	void ReviewStore::Close()
	{
		Flush();

		m_Root.clear();
		m_Sessions.Replace(ReviewSessions::Sessions{});
		m_FileOnOpen = FileOnOpen::Writable;
		m_TextOnOpen = ReviewsText::Read;
		m_CopyFailureLogged = false;
		m_LastText.clear();
		m_SavedVersion = m_Sessions.GetVersion();
		m_SeenVersion = m_SavedVersion;
		m_UnchangedFor = kNoSecondsUnchanged;
	}

	void ReviewStore::MoveTo(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void ReviewStore::Update(Core::Timestep timestep)
	{
		if (m_Root.empty() || m_FileOnOpen == FileOnOpen::LeftAlone)
		{
			return;
		}

		if (m_Sessions.GetVersion() != m_SeenVersion)
		{
			m_SeenVersion = m_Sessions.GetVersion();
			m_UnchangedFor = kNoSecondsUnchanged;
			Core::Redraw::RequestIn(std::chrono::duration<double>(kSaveDelaySeconds));
			return;
		}

		if (m_SavedVersion == m_SeenVersion)
		{
			return;
		}

		m_UnchangedFor += timestep.GetSeconds();

		if (m_UnchangedFor >= kSaveDelaySeconds)
		{
			Flush();
		}

		// WARNING: A requested deadline lasts for one wait, so a save still to come asks again every frame
		// for the frame it falls due on.
		if (m_SavedVersion != m_SeenVersion)
		{
			Core::Redraw::RequestIn(std::chrono::duration<double>(kSaveDelaySeconds - m_UnchangedFor));
		}
	}

	void ReviewStore::Flush()
	{
		if (m_Root.empty() || m_Sessions.GetVersion() == m_SavedVersion || m_FileOnOpen == FileOnOpen::LeftAlone)
		{
			return;
		}

		std::string text = EncodeReviewSessions(m_Sessions.GetAll());

		if (text == m_LastText)
		{
			m_SavedVersion = m_Sessions.GetVersion();
			m_SeenVersion = m_SavedVersion;
			return;
		}

		const std::filesystem::path file = FileFor(m_Root);

		if (m_FileOnOpen == FileOnOpen::CopyAsideFirst && !CopyAsideBeforeFirstWrite(file))
		{
			m_UnchangedFor = kNoSecondsUnchanged;
			return;
		}

		std::error_code directoryError;
		std::filesystem::create_directories(file.parent_path(), directoryError);

		const Core::Result<void> written = Core::WriteFileAtomically(file, text);

		if (!written.HasValue())
		{
			APP_WARN("Cannot write {}: {}", Core::ToUtf8(file), written.GetError().Message);
			m_UnchangedFor = kNoSecondsUnchanged;
			return;
		}

		m_LastText = std::move(text);
		m_SavedVersion = m_Sessions.GetVersion();
		m_SeenVersion = m_SavedVersion;
	}

	bool ReviewStore::CopyAsideBeforeFirstWrite(const std::filesystem::path& file)
	{
		std::error_code statError;
		const bool goneSinceOpen = !std::filesystem::exists(file, statError) && !statError;

		if (goneSinceOpen || BackUpBeforeWritingOver(file, WhyNotTakenIn(m_TextOnOpen)))
		{
			m_FileOnOpen = FileOnOpen::Writable;
			return true;
		}

		if (!m_CopyFailureLogged)
		{
			APP_WARN("Not writing {}: it {} and could not be copied aside", Core::ToUtf8(file),
					 WhyNotTakenIn(m_TextOnOpen));
			m_CopyFailureLogged = true;
		}

		return false;
	}
}
