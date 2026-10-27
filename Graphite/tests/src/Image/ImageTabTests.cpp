#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeImageTexture.h"
#include "ImageFixtures.h"
#include "TempDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Image/ImageDecoding.h"
#include "Core/Image/ImageFormat.h"
#include "Image/ImageTab.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <share.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Graphite::ImageTab;
using Graphite::ImageTabState;
using Graphite::PixelPoint;

namespace {

	using namespace std::chrono_literals;

	using Clock = std::chrono::steady_clock;

	constexpr std::string_view kNotAPicture = "this is not a picture";

	template<std::size_t Count>
	std::span<const std::uint8_t> Bytes(const std::uint8_t (&array)[Count])
	{
		return std::span<const std::uint8_t>{array, Count};
	}

	std::span<const std::uint8_t> Bytes(std::string_view text)
	{
		return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
	}

	std::filesystem::path WriteFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return path;
	}

	// Moves the modified time on as well, so the change is seen even when the size stays the same.
	void Rewrite(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
	{
		const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);
		WriteFile(path, bytes);
		std::filesystem::last_write_time(path, before + std::chrono::seconds(60));
	}

	// A 24-bit bmp whose left half is red and right half blue.
	std::vector<std::uint8_t> HalvesBmp(int width, int height)
	{
		constexpr std::uint32_t kHeadersSize = 54;
		const std::uint32_t rowSize = (static_cast<std::uint32_t>(width) * 3 + 3) / 4 * 4;
		const std::uint32_t pixelsSize = rowSize * static_cast<std::uint32_t>(height);

		std::vector<std::uint8_t> bytes;
		auto put16 = [&](std::uint32_t value) {
			bytes.push_back(static_cast<std::uint8_t>(value));
			bytes.push_back(static_cast<std::uint8_t>(value >> 8));
		};
		auto put32 = [&](std::uint32_t value) {
			put16(value & 0xFFFF);
			put16(value >> 16);
		};

		bytes.push_back('B');
		bytes.push_back('M');
		put32(kHeadersSize + pixelsSize);
		put32(0);
		put32(kHeadersSize);
		put32(40);
		put32(static_cast<std::uint32_t>(width));
		put32(static_cast<std::uint32_t>(height));
		put16(1);
		put16(24);
		put32(0);
		put32(pixelsSize);
		put32(2835);
		put32(2835);
		put32(0);
		put32(0);

		for (int row = 0; row < height; ++row)
		{
			for (int column = 0; column < width; ++column)
			{
				const bool left = column < width / 2;
				bytes.push_back(left ? 0 : 255);
				bytes.push_back(0);
				bytes.push_back(left ? 255 : 0);
			}

			for (std::uint32_t padding = static_cast<std::uint32_t>(width) * 3; padding < rowSize; ++padding)
			{
				bytes.push_back(0);
			}
		}

		return bytes;
	}

	std::string DecodeFailure(std::span<const std::uint8_t> bytes, Core::ImageFormat format)
	{
		const Core::Result<Core::DecodedImage> decoded = Core::DecodeImage(bytes, format, Core::DecodeLimits{});
		REQUIRE_FALSE(decoded.HasValue());
		return decoded.GetError().Message;
	}

	ImTextureID TextureId(ImageTab& tab)
	{
		const auto* texture = static_cast<const FakeImageTexture*>(tab.GetTexture());
		REQUIRE(texture != nullptr);
		return texture->GetId();
	}

	void CheckColour(const ImageTab& tab, PixelPoint at, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
	{
		const std::optional<Graphite::HoveredPixel> pixel = tab.PixelAt(at);
		REQUIRE(pixel.has_value());
		CHECK(pixel->At == at);
		CHECK(pixel->R == r);
		CHECK(pixel->G == g);
		CHECK(pixel->B == b);
		CHECK(pixel->A == a);
	}

	void LoadAndLand(ImageTab& tab, DeferredJobs& jobs, Clock::time_point now = Clock::now())
	{
		tab.Load();
		jobs.RunAll();
		tab.Update(now, true);
	}

	void ReloadAndLand(ImageTab& tab, DeferredJobs& jobs)
	{
		tab.CheckDisk();
		REQUIRE(jobs.Count() == 1);
		jobs.RunAll();
		tab.Update(Clock::now(), true);
	}
}

TEST_CASE("An image tab loads its file in the background")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2)), jobs.Submitter(), textures);

	tab.Load();
	CHECK(tab.GetState() == ImageTabState::Loading);
	CHECK(jobs.Count() == 1);

	tab.Load();
	CHECK(jobs.Count() == 1);

	tab.Update(Clock::now(), true);
	CHECK(tab.GetState() == ImageTabState::Loading);
	CHECK(tab.GetTexture() == nullptr);
	CHECK(tab.GetFacts() == nullptr);
	CHECK(jobs.Count() == 1);

	jobs.RunAll();
	CHECK(tab.GetState() == ImageTabState::Loading);

	tab.Update(Clock::now(), true);
	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK(tab.GetFailure().empty());

	const Graphite::ImageFacts* facts = tab.GetFacts();
	REQUIRE(facts != nullptr);
	CHECK(facts->Format == Core::ImageFormat::Png);
	CHECK(facts->Width == 2);
	CHECK(facts->Height == 2);
	CHECK(facts->FileSize == sizeof(ImageFixtures::kPng2x2));
	CHECK(facts->FileFrameCount == 1);
	CHECK(facts->ShownFrameCount == 1);

	const Core::ImageTexture* texture = tab.GetTexture();
	REQUIRE(texture != nullptr);
	CHECK(texture->GetSize().x == 2.0f);
	CHECK(texture->GetSize().y == 2.0f);
	CHECK(tab.GetTexture() == texture);
	CHECK(textures.GetCreatedCount() == 1);

	CheckColour(tab, {0, 0}, 255, 0, 0, 255);
	CheckColour(tab, {1, 1}, 0, 0, 0, 0);
	CHECK_FALSE(tab.PixelAt({2, 0}).has_value());
	CHECK_FALSE(tab.PixelAt({0, -1}).has_value());

	// The frame on the CPU and the texture's own copy of it.
	CHECK(tab.GetDecodedBytes() == 2 * 2 * 4 * 2);
}

TEST_CASE("An image tab that cannot decode its file fails with the reason")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "broken.png", Bytes(kNotAPicture)), jobs.Submitter(), textures);

	LoadAndLand(tab, jobs);

	const std::string reason = DecodeFailure(Bytes(kNotAPicture), Core::ImageFormat::Png);
	CHECK_FALSE(reason.empty());
	CHECK(tab.GetState() == ImageTabState::Failed);
	CHECK(tab.GetFailure() == "Could not read this image: " + reason);
	CHECK(tab.GetFailure().find("cannot be read") == std::string::npos);
	CHECK(tab.GetReloadFailure().empty());
	CHECK(tab.GetFacts() == nullptr);
	CHECK(tab.GetTexture() == nullptr);
	CHECK(tab.GetDecodedBytes() == 0);
	CHECK(tab.CanRetry());

	SUBCASE("Retry loads again and shows the picture once the file is readable")
	{
		WriteFile(directory.GetPath() / "broken.png", Bytes(ImageFixtures::kPng2x2));
		tab.Load();
		CHECK(tab.GetState() == ImageTabState::Loading);

		jobs.RunAll();
		tab.Update(Clock::now(), true);
		CHECK(tab.GetState() == ImageTabState::Ready);
		CHECK(tab.GetFailure().empty());
	}

	SUBCASE("A file that is not there fails as unreadable")
	{
		ImageTab missing(directory.GetPath() / "missing.png", jobs.Submitter(), textures);
		LoadAndLand(missing, jobs);

		CHECK(missing.GetState() == ImageTabState::Failed);
		CHECK(missing.GetFailure() == "Could not read this image: the file was not found");
		CHECK(missing.CanRetry());
	}

	SUBCASE("A file another program holds shut fails as one that cannot be opened")
	{
		const std::filesystem::path path = WriteFile(directory.GetPath() / "held.png", Bytes(ImageFixtures::kPng2x2));
		std::FILE* held = ::_wfsopen(path.c_str(), L"rb", _SH_DENYRW);
		REQUIRE(held != nullptr);

		ImageTab heldTab(path, jobs.Submitter(), textures);
		LoadAndLand(heldTab, jobs);
		std::fclose(held);

		CHECK(heldTab.GetState() == ImageTabState::Failed);
		CHECK(heldTab.GetFailure() == "Could not read this image: the file cannot be opened");
	}
}

TEST_CASE("An image past the pixel limit fails as too large to preview")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "huge.png", Bytes(ImageFixtures::kPngHeader20000x12000)), jobs.Submitter(),
				 textures);

	LoadAndLand(tab, jobs);

	CHECK(tab.GetState() == ImageTabState::Failed);
	CHECK(tab.GetFailure() == "Image is too large to preview (20000 × 12000)");
	CHECK(tab.GetFacts() == nullptr);
	CHECK(tab.GetTexture() == nullptr);
	CHECK_FALSE(tab.CanRetry());
}

TEST_CASE("A changed file reloads and keeps the old picture until the new one is ready")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	const std::filesystem::path path = WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2));
	ImageTab tab(path, jobs.Submitter(), textures);
	LoadAndLand(tab, jobs);
	const ImTextureID before = TextureId(tab);

	tab.CheckDisk();
	CHECK(jobs.Count() == 0);

	Rewrite(path, Bytes(ImageFixtures::kGrayAlphaPng2x1));
	tab.CheckDisk();
	CHECK(jobs.Count() == 1);

	tab.Update(Clock::now(), true);
	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK(TextureId(tab) == before);
	CHECK(tab.GetFacts()->Height == 2);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	jobs.RunAll();
	tab.Update(Clock::now(), true);
	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK(tab.GetFacts()->Width == 2);
	CHECK(tab.GetFacts()->Height == 1);
	CHECK(tab.GetFacts()->FileSize == sizeof(ImageFixtures::kGrayAlphaPng2x1));
	CHECK(TextureId(tab) != before);
	CHECK(tab.GetTexture()->GetSize().y == 1.0f);
	CHECK(textures.GetLiveCount() == 1);
	CheckColour(tab, {0, 0}, 100, 100, 100, 255);

	tab.CheckDisk();
	CHECK(jobs.Count() == 0);

	SUBCASE("A change while the reload runs is read once the reload lands")
	{
		Rewrite(path, Bytes(ImageFixtures::kPng2x2));
		tab.CheckDisk();
		REQUIRE(jobs.Count() == 1);
		jobs.RunAll();

		Rewrite(path, Bytes(ImageFixtures::kGrayAlphaPng2x1));
		tab.CheckDisk();
		CHECK(jobs.Count() == 0);

		// The 2x2 version lands, but the file has changed since it was read.
		tab.Update(Clock::now(), true);
		CHECK(tab.GetFacts()->Height == 2);
		CHECK(jobs.Count() == 1);

		jobs.RunAll();
		tab.Update(Clock::now(), true);
		CHECK(tab.GetFacts()->Height == 1);
	}
}

TEST_CASE("A reload that fails keeps the old picture and says why")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	const std::filesystem::path path = WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2));
	ImageTab tab(path, jobs.Submitter(), textures);
	LoadAndLand(tab, jobs);
	const ImTextureID before = TextureId(tab);

	// Cut inside its pixel data, as a save still under way leaves it.
	const std::span<const std::uint8_t> halfWritten = Bytes(ImageFixtures::kPng2x2).first(sizeof(ImageFixtures::kPng2x2) - 20);
	Rewrite(path, halfWritten);
	ReloadAndLand(tab, jobs);

	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK(tab.GetFailure().empty());
	CHECK(tab.GetReloadFailure() == "Could not read the new version: " + DecodeFailure(halfWritten, Core::ImageFormat::Png));
	CHECK(TextureId(tab) == before);
	CHECK(textures.GetCreatedCount() == 1);
	CHECK(tab.GetFacts()->Width == 2);
	CHECK(tab.GetFacts()->FileSize == sizeof(ImageFixtures::kPng2x2));
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	// The broken version is not read again on every check, only once it changes.
	tab.CheckDisk();
	CHECK(jobs.Count() == 0);

	SUBCASE("Retry reads it again")
	{
		tab.Load();
		CHECK(jobs.Count() == 1);
	}

	SUBCASE("The finished save replaces it and clears the line")
	{
		Rewrite(path, Bytes(ImageFixtures::kGrayAlphaPng2x1));
		ReloadAndLand(tab, jobs);

		CHECK(tab.GetReloadFailure().empty());
		CHECK(tab.GetFacts()->Height == 1);
	}
}

TEST_CASE("Zoom survives a reload that keeps the dimensions and starts again when they change")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	const std::filesystem::path path = WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2));
	ImageTab tab(path, jobs.Submitter(), textures);
	LoadAndLand(tab, jobs);

	tab.GetZoom() = Graphite::ZoomState{4.0f, ImVec2(10.0f, 5.0f), true};

	Rewrite(path, Bytes(ImageFixtures::kPng2x2));
	ReloadAndLand(tab, jobs);
	CHECK(tab.GetZoom().Opened);
	CHECK(tab.GetZoom().Zoom == 4.0f);
	CHECK(tab.GetZoom().Pan.x == 10.0f);
	CHECK(tab.GetZoom().Pan.y == 5.0f);

	Rewrite(path, Bytes(ImageFixtures::kGrayAlphaPng2x1));
	ReloadAndLand(tab, jobs);
	CHECK_FALSE(tab.GetZoom().Opened);
}

TEST_CASE("A gif shows its next frame once its delay has passed")
{
	static_cast<void>(Core::Redraw::TakeDeadline());

	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "spinner.gif", Bytes(ImageFixtures::kGif2Frames2x2)), jobs.Submitter(),
				 textures);
	tab.Load();
	jobs.RunAll();

	const Clock::time_point start = Clock::now();
	tab.Update(start, true);
	const Clock::time_point afterUpdate = Clock::now();

	REQUIRE(tab.GetFacts() != nullptr);
	CHECK(tab.GetFacts()->FileFrameCount == 2);
	CHECK(tab.GetFacts()->ShownFrameCount == 2);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);
	const ImTextureID first = TextureId(tab);

	// The first frame lasts 50 ms, so the next frame is asked for then.
	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= start + 49ms);
	CHECK(*deadline <= afterUpdate + 50ms);

	tab.Update(start + 49ms, true);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);
	CHECK(TextureId(tab) == first);

	tab.Update(start + 50ms, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);
	CHECK(TextureId(tab) != first);
	CHECK(textures.GetLiveCount() == 1);

	// The second frame's 10 ms counts as 100 ms, then the gif starts over.
	tab.Update(start + 149ms, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	tab.Update(start + 150ms, true);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	// Both frames, plus the texture's copy of the one shown.
	REQUIRE(tab.GetTexture() != nullptr);
	CHECK(tab.GetDecodedBytes() == 2 * 2 * 4 * 3);

	static_cast<void>(Core::Redraw::TakeDeadline());
}

TEST_CASE("A gif woken late times its next frame from when the frame was due")
{
	static_cast<void>(Core::Redraw::TakeDeadline());

	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "spinner.gif", Bytes(ImageFixtures::kGif2Frames2x2)), jobs.Submitter(),
				 textures);
	tab.Load();
	jobs.RunAll();

	const Clock::time_point start = Clock::now();
	tab.Update(start, true);
	static_cast<void>(Core::Redraw::TakeDeadline());

	// The first frame was due at 50 ms, so the second frame's 100 ms end at 150 ms, not at 160 ms.
	const Clock::time_point beforeLateUpdate = Clock::now();
	tab.Update(start + 60ms, true);
	const Clock::time_point afterLateUpdate = Clock::now();
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	const std::optional<Core::Redraw::Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= beforeLateUpdate + 90ms);
	CHECK(*deadline <= afterLateUpdate + 90ms);

	tab.Update(start + 149ms, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	tab.Update(start + 150ms, true);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	static_cast<void>(Core::Redraw::TakeDeadline());
}

TEST_CASE("A gif woken after a long pause shows one frame on and none of the frames it missed")
{
	static_cast<void>(Core::Redraw::TakeDeadline());

	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "spinner.gif", Bytes(ImageFixtures::kGif2Frames2x2)), jobs.Submitter(),
				 textures);
	tab.Load();
	jobs.RunAll();

	const Clock::time_point start = Clock::now();
	tab.Update(start, true);

	tab.Update(start + 5s, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	tab.Update(start + 5s, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	// The frame shown after the pause gets its whole 100 ms from then.
	tab.Update(start + 5s + 99ms, true);
	CheckColour(tab, {0, 0}, 0, 0, 255, 255);

	tab.Update(start + 5s + 100ms, true);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	static_cast<void>(Core::Redraw::TakeDeadline());
}

TEST_CASE("A gif in a tab that is not shown asks for no frames")
{
	static_cast<void>(Core::Redraw::TakeDeadline());

	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "spinner.gif", Bytes(ImageFixtures::kGif2Frames2x2)), jobs.Submitter(),
				 textures);
	tab.Load();
	jobs.RunAll();

	const Clock::time_point start = Clock::now();
	tab.Update(start, false);
	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());

	tab.Update(start + 1s, false);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	// Shown again, the frame on screen gets its whole delay.
	tab.Update(start + 2s, true);
	CHECK(Core::Redraw::TakeDeadline().has_value());
	tab.Update(start + 2s + 49ms, true);
	CheckColour(tab, {0, 0}, 255, 0, 0, 255);

	static_cast<void>(Core::Redraw::TakeDeadline());
}

TEST_CASE("A released image tab loads again when asked")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2)), jobs.Submitter(), textures);
	LoadAndLand(tab, jobs);
	REQUIRE(tab.GetTexture() != nullptr);
	tab.GetZoom() = Graphite::ZoomState{4.0f, ImVec2(10.0f, 5.0f), true};

	tab.Release();
	CHECK(tab.GetState() == ImageTabState::Loading);
	CHECK(tab.GetTexture() == nullptr);
	CHECK(textures.GetLiveCount() == 0);
	CHECK(tab.GetDecodedBytes() == 0);
	CHECK_FALSE(tab.PixelAt({0, 0}).has_value());
	REQUIRE(tab.GetFacts() != nullptr);
	CHECK(tab.GetFacts()->Width == 2);

	// Hidden, it stays released; the check for changes waits for the next load as well.
	tab.Update(Clock::now(), false);
	tab.CheckDisk();
	CHECK(jobs.Count() == 0);

	SUBCASE("by a load")
	{
		tab.Load();
	}

	SUBCASE("by being shown")
	{
		tab.Update(Clock::now(), true);
	}

	CHECK(jobs.Count() == 1);
	jobs.RunAll();
	tab.Update(Clock::now(), true);
	CHECK(tab.GetState() == ImageTabState::Ready);
	CHECK(tab.GetTexture() != nullptr);
	CHECK(tab.GetDecodedBytes() == 2 * 2 * 4 * 2);
	CHECK(tab.GetZoom().Opened);
	CHECK(tab.GetZoom().Zoom == 4.0f);
}

TEST_CASE("A released picture whose file broke since fails without its old facts")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	const std::filesystem::path path = WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2));
	ImageTab tab(path, jobs.Submitter(), textures);
	LoadAndLand(tab, jobs);

	tab.Release();
	Rewrite(path, Bytes(kNotAPicture));
	tab.Update(Clock::now(), true);
	jobs.RunAll();
	tab.Update(Clock::now(), true);

	CHECK(tab.GetState() == ImageTabState::Failed);
	CHECK(tab.GetFailure().starts_with("Could not read this image: "));
	CHECK(tab.GetReloadFailure().empty());
	CHECK(tab.GetFacts() == nullptr);
}

TEST_CASE("A load still running when its tab is released lands as nothing")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	ImageTab tab(WriteFile(directory.GetPath() / "picture.png", Bytes(ImageFixtures::kPng2x2)), jobs.Submitter(), textures);

	tab.Load();
	tab.Release();
	tab.Load();
	REQUIRE(jobs.Count() == 2);

	tab.Release();
	jobs.RunAll();
	tab.Update(Clock::now(), false);
	CHECK(tab.GetState() == ImageTabState::Loading);
	CHECK(tab.GetDecodedBytes() == 0);
	CHECK(tab.GetFacts() == nullptr);

	tab.Load();
	jobs.RunAll();
	tab.Update(Clock::now(), false);
	CHECK(tab.GetState() == ImageTabState::Ready);
}

TEST_CASE("An image under a path with Polish letters loads")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures;
	const std::filesystem::path path =
		WriteFile(directory.GetPath() / std::filesystem::path(u8"zdjęcie łąki.png"), Bytes(ImageFixtures::kPng2x2));
	REQUIRE(std::filesystem::exists(path));
	ImageTab tab(path, jobs.Submitter(), textures);

	LoadAndLand(tab, jobs);

	CHECK(tab.GetState() == ImageTabState::Ready);
	REQUIRE(tab.GetFacts() != nullptr);
	CHECK(tab.GetFacts()->Width == 2);

	tab.CheckDisk();
	CHECK(jobs.Count() == 0);
}

TEST_CASE("The pixel under the pointer is read from a picture scaled down to the texture limit")
{
	TempDirectory directory;
	DeferredJobs jobs;
	FakeTextureFactory textures(4);
	const std::vector<std::uint8_t> halves = HalvesBmp(16, 16);
	ImageTab tab(WriteFile(directory.GetPath() / "halves.bmp", halves), jobs.Submitter(), textures);

	LoadAndLand(tab, jobs);

	REQUIRE(tab.GetFacts() != nullptr);
	CHECK(tab.GetFacts()->Width == 16);
	CHECK(tab.GetFacts()->Height == 16);
	REQUIRE(tab.GetTexture() != nullptr);
	CHECK(tab.GetTexture()->GetSize().x == 4.0f);

	const std::optional<Graphite::HoveredPixel> left = tab.PixelAt({0, 0});
	REQUIRE(left.has_value());
	CHECK(left->R > 200);
	CHECK(left->B < 50);

	const std::optional<Graphite::HoveredPixel> right = tab.PixelAt({15, 15});
	REQUIRE(right.has_value());
	CHECK(right->At == PixelPoint{15, 15});
	CHECK(right->R < 50);
	CHECK(right->B > 200);

	CHECK_FALSE(tab.PixelAt({16, 0}).has_value());
}
