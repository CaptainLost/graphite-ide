#pragma once

#include <cstdint>
#include <string>

namespace Graphite {

	class PendingFolderRename final
	{
	public:
		void Request(std::string path, std::uint64_t currentLoad);

		void BeginFrame(bool editable, std::uint64_t currentLoad);

		bool IsLooking() const { return m_Looking; }

		bool LeadsTo(const std::string& folderPath) const;

		bool Take(const std::string& path);

		void EndFrame();

		bool IsPending() const { return !m_Path.empty(); }
		void Clear();

	private:
		std::string m_Path;
		std::uint64_t m_RequestedAt = 0;
		bool m_Looking = false;
	};
}
