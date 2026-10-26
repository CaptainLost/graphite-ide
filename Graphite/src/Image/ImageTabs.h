#pragma once

#include "Image/ImageTab.h"
#include "Image/PictureBudget.h"

#include "Core/ImGui/ImageTexture.h"
#include "Core/Jobs/JobSubmitter.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>

namespace Graphite {

	// The picture of every open image tab, by the tab's path, and the one budget their decoded pixels share. Paths
	// are matched exactly, as the tabs spell them.
	class ImageTabs
	{
	public:
		ImageTabs(Core::JobSubmitter submit, Core::TextureFactory& textures, PictureBudget budget = PictureBudget{});

		// Made on first use; it loads once it is first updated as shown.
		ImageTab& For(const std::filesystem::path& path);

		// Drops the picture of every tab not in `open`, which frees its texture.
		void Prune(const std::set<std::filesystem::path>& open);

		void CheckDisk(const std::filesystem::path& path);

		// Updates every tab, then releases what the budget names. WARNING: A landed load or a gif's next frame frees
		// the texture shown before it, so this runs before any picture is drawn in the frame.
		void Update(std::chrono::steady_clock::time_point now, const std::set<std::filesystem::path>& shown);

	private:
		struct Entry
		{
			ImageTab Tab;
			std::uint64_t LastShownFrame = 0;
		};

		Core::JobSubmitter m_Submit;
		Core::TextureFactory& m_Textures;
		PictureBudget m_Budget;
		std::uint64_t m_Frame = 0;
		std::map<std::filesystem::path, Entry> m_Tabs;
	};
}
