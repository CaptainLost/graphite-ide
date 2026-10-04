#pragma once

#include <cstdint>
#include <string>

namespace Graphite {

	// A folder just written to the solution file, renamed in place as soon as a reload lists
	// it. The request is tied to the load that was current when the folder was written: the
	// first load after it either lists the folder or never will, so the request lives for
	// that one walk and no longer. Without a load the tree is left alone, so a reload that
	// never arrives cannot hold rows open or start a rename much later.
	class PendingFolderRename final
	{
	public:
		void Request(std::string path, std::uint64_t currentLoad);

		// Called before the walk. A solution that is gone or cannot be edited drops the
		// request outright.
		void BeginFrame(bool editable, std::uint64_t currentLoad);

		// Whether this frame's walk looks for the folder: a load has happened since the
		// request, so the walk opens the way to it and starts the rename when it reaches it.
		bool IsLooking() const { return m_Looking; }

		// Whether the folder lies below `folderPath`, on a frame the walk looks for it.
		bool LeadsTo(const std::string& folderPath) const;

		// Whether the walk has reached the folder; a match spends the request.
		bool Take(const std::string& path);

		// Called after the walk: a load whose walk did not reach the folder ends the wait.
		void EndFrame();

		bool IsPending() const { return !m_Path.empty(); }
		void Clear();

	private:
		std::string m_Path;
		std::uint64_t m_RequestedAt = 0;
		bool m_Looking = false;
	};
}
