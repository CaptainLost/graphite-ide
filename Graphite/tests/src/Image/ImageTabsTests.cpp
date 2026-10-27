#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeImageTexture.h"
#include "ImageFixtures.h"
#include "TempDirectory.h"

#include "Image/ImageTabs.h"
#include "Image/PictureBudget.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

using Graphite::ImageTabState;

namespace {

	using Clock = std::chrono::steady_clock;

	// Room for one 2 × 2 picture's frames and not for two.
	constexpr std::uint64_t kRoomForOnePicture = 2 * 2 * 4 + 4;

	template<std::size_t Count>
	std::string Contents(const std::uint8_t (&bytes)[Count])
	{
		return std::string(reinterpret_cast<const char*>(bytes), Count);
	}

	struct Pictures
	{
		explicit Pictures(Graphite::PictureBudget budget = Graphite::PictureBudget{}) : Tabs(Jobs.Submitter(), Textures, budget)
		{
		}

		// As the document windows do: a tab on screen is drawn, which makes its picture, and updated as shown.
		void Show(const std::set<std::filesystem::path>& shown)
		{
			for (const std::filesystem::path& path : shown)
			{
				Tabs.For(path);
			}

			Tabs.Update(Clock::now(), shown);
			Jobs.RunAll();
			Tabs.Update(Clock::now(), shown);
		}

		TempDirectory Directory;
		std::filesystem::path First = Directory.MakeFile("first.png", Contents(ImageFixtures::kPng2x2));
		std::filesystem::path Second = Directory.MakeFile("second.png", Contents(ImageFixtures::kPng2x2));
		DeferredJobs Jobs;
		FakeTextureFactory Textures;
		Graphite::ImageTabs Tabs;
	};
}

TEST_CASE("Image tabs load only the pictures on screen")
{
	Pictures pictures;
	pictures.Tabs.For(pictures.First);
	pictures.Tabs.For(pictures.Second);

	pictures.Tabs.Update(Clock::now(), {pictures.First});

	CHECK(pictures.Jobs.Count() == 1);

	pictures.Jobs.RunAll();
	pictures.Tabs.Update(Clock::now(), {pictures.First});

	CHECK(pictures.Tabs.For(pictures.First).GetState() == ImageTabState::Ready);
	CHECK(pictures.Tabs.For(pictures.Second).GetState() == ImageTabState::Loading);
	CHECK(pictures.Jobs.Count() == 0);
}

TEST_CASE("Image tabs past their budget release the picture shown least recently and keep the one on screen")
{
	Pictures pictures{Graphite::PictureBudget{kRoomForOnePicture}};

	pictures.Show({pictures.First});
	REQUIRE(pictures.Tabs.For(pictures.First).GetState() == ImageTabState::Ready);

	pictures.Show({pictures.Second});

	CHECK(pictures.Tabs.For(pictures.Second).GetState() == ImageTabState::Ready);
	CHECK(pictures.Tabs.For(pictures.First).GetState() == ImageTabState::Loading);
	CHECK(pictures.Tabs.For(pictures.First).GetDecodedBytes() == 0);

	// Shown again, the released picture loads again, and the other one goes in its turn.
	pictures.Show({pictures.First});

	CHECK(pictures.Tabs.For(pictures.First).GetState() == ImageTabState::Ready);
	CHECK(pictures.Tabs.For(pictures.Second).GetState() == ImageTabState::Loading);
}

TEST_CASE("Pruning image tabs frees the pictures of the tabs that closed")
{
	Pictures pictures;
	pictures.Show({pictures.First, pictures.Second});
	REQUIRE(pictures.Tabs.For(pictures.First).GetTexture() != nullptr);
	REQUIRE(pictures.Tabs.For(pictures.Second).GetTexture() != nullptr);
	REQUIRE(pictures.Textures.GetLiveCount() == 2);

	pictures.Tabs.Prune({pictures.Second});

	CHECK(pictures.Textures.GetLiveCount() == 1);
	CHECK(pictures.Tabs.For(pictures.Second).GetState() == ImageTabState::Ready);
}

TEST_CASE("A changed file reloads its image tab")
{
	Pictures pictures;
	pictures.Show({pictures.First});

	const std::filesystem::file_time_type before = std::filesystem::last_write_time(pictures.First);
	pictures.Directory.MakeFile("first.png", Contents(ImageFixtures::kGrayAlphaPng2x1));
	std::filesystem::last_write_time(pictures.First, before + std::chrono::seconds(60));

	pictures.Tabs.CheckDisk(pictures.Second);
	CHECK(pictures.Jobs.Count() == 0);

	pictures.Tabs.CheckDisk(pictures.First);
	REQUIRE(pictures.Jobs.Count() == 1);
	pictures.Jobs.RunAll();
	pictures.Tabs.Update(Clock::now(), {pictures.First});

	REQUIRE(pictures.Tabs.For(pictures.First).GetFacts() != nullptr);
	CHECK(pictures.Tabs.For(pictures.First).GetFacts()->Height == 1);
}
