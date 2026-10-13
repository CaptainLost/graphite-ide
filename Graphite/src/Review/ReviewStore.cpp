#include "Review/ReviewStore.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <chrono>
#include <string>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		constexpr const char* kStateDirectory = ".graphite";
		constexpr const char* kReviewsFile = "reviews.json";
	}

	std::filesystem::path ReviewStore::FileFor(const std::filesystem::path& root)
	{
		return root / kStateDirectory / kReviewsFile;
	}

	void ReviewStore::Open(std::filesystem::path root)
	{
		m_Root = std::move(root);

		ViewedMarks::Marks stored;
		const std::filesystem::path file = FileFor(m_Root);

		if (Core::PathExists(file))
		{
			Core::Result<std::string> text = Core::ReadFileBytes(file);

			if (text.HasValue())
			{
				stored = DecodeViewedMarks(text.GetValue());
			}
			else
			{
				APP_WARN("Cannot read {}: {}", Core::ToUtf8(file), text.GetError().Message);
			}
		}

		m_Marks.Replace(std::move(stored));
		m_SavedVersion = m_Marks.GetVersion();
		m_SeenVersion = m_SavedVersion;
		m_UnchangedFor = 0.0f;
	}

	void ReviewStore::Close()
	{
		Flush();

		m_Root.clear();
		m_Marks.Replace(ViewedMarks::Marks{});
		m_SavedVersion = m_Marks.GetVersion();
		m_SeenVersion = m_SavedVersion;
		m_UnchangedFor = 0.0f;
	}

	void ReviewStore::MoveTo(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void ReviewStore::Update(Core::Timestep ts)
	{
		if (m_Root.empty())
		{
			return;
		}

		if (m_Marks.GetVersion() != m_SeenVersion)
		{
			m_SeenVersion = m_Marks.GetVersion();
			m_UnchangedFor = 0.0f;
			Core::Redraw::RequestIn(std::chrono::duration<double>(kSaveDelaySeconds));
			return;
		}

		if (m_SavedVersion == m_SeenVersion)
		{
			return;
		}

		m_UnchangedFor += ts.GetSeconds();

		if (m_UnchangedFor >= kSaveDelaySeconds)
		{
			Flush();
		}
	}

	void ReviewStore::Flush()
	{
		if (m_Root.empty() || m_Marks.GetVersion() == m_SavedVersion)
		{
			return;
		}

		const std::filesystem::path file = FileFor(m_Root);

		std::error_code directoryError;
		std::filesystem::create_directories(file.parent_path(), directoryError);

		const Core::Result<void> written = Core::WriteFileAtomically(file, EncodeViewedMarks(m_Marks.GetAll()));

		if (!written.HasValue())
		{
			// Left unsaved, so the next change or the close tries again.
			APP_WARN("Cannot write {}: {}", Core::ToUtf8(file), written.GetError().Message);
			m_UnchangedFor = 0.0f;
			return;
		}

		m_SavedVersion = m_Marks.GetVersion();
		m_SeenVersion = m_SavedVersion;
	}
}
