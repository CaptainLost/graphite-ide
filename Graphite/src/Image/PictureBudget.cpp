#include "Image/PictureBudget.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	PictureBudget::PictureBudget(std::uint64_t limit) : m_Limit(limit) {}

	void PictureBudget::Note(const std::filesystem::path& path, std::uint64_t bytes, std::uint64_t lastShownFrame)
	{
		m_Pictures[path] = Picture{bytes, lastShownFrame};
	}

	void PictureBudget::Forget(const std::filesystem::path& path)
	{
		m_Pictures.erase(path);
	}

	std::vector<std::filesystem::path> PictureBudget::OverBudget(const std::set<std::filesystem::path>& onScreen) const
	{
		std::uint64_t total = 0;

		for (const auto& [path, picture] : m_Pictures)
		{
			total += picture.Bytes;
		}

		if (total <= m_Limit)
		{
			return {};
		}

		std::vector<std::pair<std::filesystem::path, Picture>> candidates;

		for (const auto& [path, picture] : m_Pictures)
		{
			if (picture.Bytes > 0 && !onScreen.contains(path))
			{
				candidates.emplace_back(path, picture);
			}
		}

		std::ranges::stable_sort(candidates, {}, [](const auto& candidate) { return candidate.second.LastShownFrame; });

		std::vector<std::filesystem::path> released;

		for (const auto& [path, picture] : candidates)
		{
			if (total <= m_Limit)
			{
				break;
			}

			released.push_back(path);
			total -= picture.Bytes;
		}

		return released;
	}
}
