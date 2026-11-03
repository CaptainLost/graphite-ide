#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Image/ImageDecoding.h"
#include "Core/ImGui/ImageTexture.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/LatestAnswer.h"
#include "Image/ImageStatus.h"
#include "Image/Zoom.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class ImageTabState
	{
		Loading,
		Ready,
		Failed
	};

	// Why a tab is Failed. Read: the file could not be read, which reading it again may mend. TooLarge: the file or its
	// picture is past what is previewed. Decode: the bytes are not a picture this program shows.
	enum class ImageFailure
	{
		None,
		Read,
		Decode,
		TooLarge
	};

	// One open picture: read and decoded in a job, swapped in without blanking when its file changes, and given up
	// under memory pressure. Every call is made on the UI thread, which is where its textures are made and released.
	class ImageTab
	{
	public:
		ImageTab(std::filesystem::path path, Core::JobSubmitter submit, Core::TextureFactory& textures);

		// Reads and decodes the file in a job; a call while a load runs is ignored. The picture on screen stays
		// until the new one lands.
		void Load();

		// Takes a finished load and plays a gif's frames. A shown tab asks for a frame when the next one is due, and
		// loads if it was never loaded or was released; a hidden tab does neither.
		void Update(std::chrono::steady_clock::time_point now, bool shown);

		// Loads again when the file's modified time or size differs from the version last read.
		void CheckDisk();

		// Gives up the frames and the texture but keeps the facts and the zoom; the tab is Loading until it is shown
		// or loaded again. A load still running lands as nothing, and a failed tab stays failed.
		void Release();

		ImageTabState GetState() const;

		// The whole line shown in place of the picture, or as the notice above its bytes when they are shown instead. Kept
		// while the file is read again, until the new result lands.
		const std::string& GetFailure() const;

		// None unless the tab is Failed or is loading again after a failure, whose kind it keeps until the new result lands.
		ImageFailure GetFailureKind() const;

		// False for a picture too large to preview, which reading the same file again cannot change; a change to the
		// file is read without asking.
		bool CanRetry() const;

		// Set while the picture on screen is an older version because the newer one could not be read.
		const std::string& GetReloadFailure() const;

		// The shown frame's texture, made on first use.
		const Core::ImageTexture* GetTexture();

		const ImageFacts* GetFacts() const;

		// `at` is in the file's pixels, which the decoded frame may have fewer of.
		std::optional<HoveredPixel> PixelAt(PixelPoint at) const;

		// The frames plus the texture's own copy of the shown one.
		std::uint64_t GetDecodedBytes() const;

		ZoomState& GetZoom();

	private:
		struct DiskStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;

			bool operator==(const DiskStamp&) const = default;
		};

		struct LoadedPicture
		{
			Core::Result<Core::DecodedImage> Decoded;
			std::optional<DiskStamp> Stamp;
			std::uint64_t FileSize = 0;
		};

		static std::optional<DiskStamp> ReadStamp(const std::filesystem::path& path);
		static LoadedPicture ReadAndDecode(const std::filesystem::path& path, const Core::DecodeLimits& limits);

		void TakeLanded();
		void Apply(LoadedPicture picture);
		void Animate(std::chrono::steady_clock::time_point now, bool shown);

		std::filesystem::path m_Path;
		Core::LatestAnswer<LoadedPicture> m_Load;
		Core::TextureFactory& m_Textures;

		ImageTabState m_State = ImageTabState::Loading;
		std::string m_Failure;
		bool m_CanRetry = true;
		ImageFailure m_FailureKind = ImageFailure::None;
		std::string m_ReloadFailure;

		std::optional<ImageFacts> m_Facts;
		std::vector<Core::ImageFrame> m_Frames;
		std::size_t m_FrameIndex = 0;
		std::optional<std::chrono::steady_clock::time_point> m_FrameShownAt;
		std::unique_ptr<Core::ImageTexture> m_Texture;
		ZoomState m_Zoom;

		// The version last read, whether it was shown or failed, so a broken file is not read again on every check.
		std::optional<DiskStamp> m_Stamp;

		bool m_NeedsLoad = true;
		bool m_CheckAfterLoad = false;
	};
}
