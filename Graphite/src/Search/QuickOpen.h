#pragma once

#include "Core/Base/Base.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobSubmitter.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	// Ctrl+P: a field over the editors that finds a file of the open folder by a few letters of its path and opens it.
	// The folder's files are listed on a worker each time it opens, leaving out .git and what git ignores, and are
	// matched as they arrive.
	class QuickOpen
	{
	public:
		using OpenFile = std::function<void(const std::filesystem::path& absolute)>;

		QuickOpen(Core::JobSubmitter submit, OpenFile open);
		~QuickOpen();

		QuickOpen(const QuickOpen&) = delete;
		QuickOpen& operator=(const QuickOpen&) = delete;

		void Open(const std::filesystem::path& root);

		bool IsOpen() const { return m_Open; }

		// Draws over the main viewport while open; closes on Escape, on a click outside, or once a file is opened.
		void Render();

		static constexpr std::size_t kShownMatches = 60;

	private:
		struct Listing
		{
			std::mutex Mutex;
			std::vector<std::string> Paths;
			bool Done = false;
		};

		void Close();
		void Rank();

		Core::JobSubmitter m_Submit;
		OpenFile m_OpenFile;

		bool m_Open = false;
		bool m_JustOpened = false;
		std::filesystem::path m_Root;
		std::string m_Typed;
		Core::CancellationToken m_Token;
		Core::Ref<Listing> m_Listing;

		// The listing as last copied out of the worker's, and the ranked matches into it.
		std::vector<std::string> m_Paths;
		bool m_ListingDone = false;
		std::vector<std::size_t> m_Matches;
		std::size_t m_Selected = 0;
		bool m_SelectionMoved = false;
		std::string m_RankedFor;
		std::size_t m_RankedCount = 0;
	};
}
