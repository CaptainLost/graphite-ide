#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace Graphite {

	// One budget of decoded bytes shared by every picture. It only names what to release; the owners release it.
	class PictureBudget
	{
	public:
		explicit PictureBudget(std::uint64_t limit = 1ull << 30);

		// `lastShownFrame` is the frame the picture was last on screen, which orders the releases.
		void Note(const std::filesystem::path& path, std::uint64_t bytes, std::uint64_t lastShownFrame);

		void Forget(const std::filesystem::path& path);

		// The pictures to release to come back within the limit, least recently shown first. A picture on screen is
		// never named, so what is on screen may stay over the limit.
		std::vector<std::filesystem::path> OverBudget(const std::set<std::filesystem::path>& onScreen) const;

	private:
		struct Picture
		{
			std::uint64_t Bytes = 0;
			std::uint64_t LastShownFrame = 0;
		};

		std::uint64_t m_Limit = 0;
		std::map<std::filesystem::path, Picture> m_Pictures;
	};
}
