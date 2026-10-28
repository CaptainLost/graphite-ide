#include "Image/ImageTab.h"

#include "Core/App/Redraw.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Image/ImageFormat.h"

#include <limits>
#include <new>
#include <span>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		// The most DecodeImage takes, so a larger file is refused before it is read.
		constexpr std::uintmax_t kMaximumFileBytes = static_cast<std::uintmax_t>(std::numeric_limits<int>::max());

		// The reader's messages name the path and are whole sentences, so the line is worded from the code instead.
		Core::Error ReadFailure(const Core::Error& error)
		{
			switch (error.Code)
			{
			case Core::ErrorCode::NotFound:
				return Core::Error{error.Code, "the file was not found"};
			case Core::ErrorCode::AccessDenied:
				return Core::Error{error.Code, "the file cannot be opened"};
			case Core::ErrorCode::TooLarge:
				return Core::Error{error.Code, "Image file is too large to preview"};
			case Core::ErrorCode::None:
			case Core::ErrorCode::InvalidArgument:
			case Core::ErrorCode::IoFailure:
			case Core::ErrorCode::Unsupported:
			case Core::ErrorCode::Cancelled:
				break;
			}

			return Core::Error{error.Code, "reading the file failed"};
		}

		// The reader's codes are the three the file can fail with; the decoder says anything else.
		ImageFailure KindOf(const Core::Error& error)
		{
			switch (error.Code)
			{
			case Core::ErrorCode::TooLarge:
				return ImageFailure::TooLarge;
			case Core::ErrorCode::NotFound:
			case Core::ErrorCode::AccessDenied:
			case Core::ErrorCode::IoFailure:
				return ImageFailure::Read;
			case Core::ErrorCode::None:
			case Core::ErrorCode::InvalidArgument:
			case Core::ErrorCode::Unsupported:
			case Core::ErrorCode::Cancelled:
				break;
			}

			return ImageFailure::Decode;
		}

		std::string FailureLine(const Core::Error& error)
		{
			if (error.Code == Core::ErrorCode::TooLarge)
			{
				return error.Message;
			}

			return "Could not read this image: " + error.Message;
		}
	}

	ImageTab::ImageTab(std::filesystem::path path, Core::JobSubmitter submit, Core::TextureFactory& textures)
		: m_Path(std::move(path)), m_Submit(std::move(submit)), m_Textures(textures)
	{
	}

	void ImageTab::Load()
	{
		if (m_LoadRunning)
		{
			return;
		}

		m_LoadRunning = true;
		m_NeedsLoad = false;
		m_CheckAfterLoad = false;

		if (m_State == ImageTabState::Failed)
		{
			m_State = ImageTabState::Loading;
			m_Failure.clear();
			m_FailureKind = ImageFailure::None;
		}

		Core::DecodeLimits limits;
		limits.MaximumSide = m_Textures.GetMaximumSide();

		m_Submit([path = m_Path, generation = ++m_Generation, limits, landed = m_Landed] {
			landed->Post(ReadAndDecode(path, generation, limits));
		});
	}

	void ImageTab::Update(std::chrono::steady_clock::time_point now, bool shown)
	{
		TakeLanded();

		if (shown && m_NeedsLoad)
		{
			Load();
		}

		Animate(now, shown);
	}

	void ImageTab::CheckDisk()
	{
		// A tab holding nothing reads the file as it is when it next loads.
		if (m_NeedsLoad)
		{
			return;
		}

		if (m_LoadRunning)
		{
			m_CheckAfterLoad = true;
			return;
		}

		const std::optional<DiskStamp> onDisk = ReadStamp(m_Path);

		if (onDisk && onDisk != m_Stamp)
		{
			Load();
		}
	}

	void ImageTab::Release()
	{
		m_Texture.reset();
		m_Frames = {};
		m_FrameIndex = 0;
		m_FrameShownAt.reset();
		m_ReloadFailure.clear();

		++m_Generation;
		m_LoadRunning = false;
		m_CheckAfterLoad = false;

		if (m_State != ImageTabState::Failed)
		{
			m_State = ImageTabState::Loading;
			m_NeedsLoad = true;
		}
	}

	ImageTabState ImageTab::GetState() const
	{
		return m_State;
	}

	const std::string& ImageTab::GetFailure() const
	{
		return m_Failure;
	}

	ImageFailure ImageTab::GetFailureKind() const
	{
		return m_FailureKind;
	}

	bool ImageTab::CanRetry() const
	{
		return m_CanRetry;
	}

	const std::string& ImageTab::GetReloadFailure() const
	{
		return m_ReloadFailure;
	}

	const Core::ImageTexture* ImageTab::GetTexture()
	{
		if (!m_Texture && !m_Frames.empty())
		{
			m_Texture = m_Textures.Create(m_Frames[m_FrameIndex].Image);
		}

		return m_Texture.get();
	}

	const ImageFacts* ImageTab::GetFacts() const
	{
		return m_Facts ? &*m_Facts : nullptr;
	}

	std::optional<HoveredPixel> ImageTab::PixelAt(PixelPoint at) const
	{
		if (m_Frames.empty() || !m_Facts || at.X < 0 || at.Y < 0 || at.X >= m_Facts->Width || at.Y >= m_Facts->Height)
		{
			return std::nullopt;
		}

		const Core::RgbaImage& image = m_Frames[m_FrameIndex].Image;
		const std::int64_t x = static_cast<std::int64_t>(at.X) * image.Width / m_Facts->Width;
		const std::int64_t y = static_cast<std::int64_t>(at.Y) * image.Height / m_Facts->Height;
		const std::size_t offset = static_cast<std::size_t>((y * image.Width + x) * 4);

		HoveredPixel pixel;
		pixel.At = at;
		pixel.R = image.Pixels[offset];
		pixel.G = image.Pixels[offset + 1];
		pixel.B = image.Pixels[offset + 2];
		pixel.A = image.Pixels[offset + 3];
		return pixel;
	}

	std::uint64_t ImageTab::GetDecodedBytes() const
	{
		std::uint64_t bytes = 0;

		for (const Core::ImageFrame& frame : m_Frames)
		{
			bytes += frame.Image.ByteSize();
		}

		// The texture keeps a whole copy of its frame on the CPU for the renderer to upload.
		if (m_Texture)
		{
			bytes += m_Frames[m_FrameIndex].Image.ByteSize();
		}

		return bytes;
	}

	ZoomState& ImageTab::GetZoom()
	{
		return m_Zoom;
	}

	std::optional<ImageTab::DiskStamp> ImageTab::ReadStamp(const std::filesystem::path& path)
	{
		std::error_code timeError;
		const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, timeError);

		if (timeError)
		{
			return std::nullopt;
		}

		std::error_code sizeError;
		const std::uintmax_t size = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return std::nullopt;
		}

		return DiskStamp{time, size};
	}

	ImageTab::LoadedPicture ImageTab::ReadAndDecode(const std::filesystem::path& path, std::uint64_t generation,
													const Core::DecodeLimits& limits)
	{
		// Taken before the read, so a change during it shows as a newer stamp at the next check.
		const std::optional<DiskStamp> stamp = ReadStamp(path);

		const std::optional<Core::ImageFormat> format = Core::ImageFormatOfPath(path);

		if (!format)
		{
			return {generation, Core::Error{Core::ErrorCode::Unsupported, "its extension names no image format"}, stamp};
		}

		try
		{
			// Read whole and closed before decoding starts, so the file is never held while the tab shows it.
			const Core::Result<std::string> bytes = Core::ReadFileBytes(path, kMaximumFileBytes);

			if (!bytes)
			{
				return {generation, ReadFailure(bytes.GetError()), stamp};
			}

			const std::string& contents = bytes.GetValue();
			const std::span<const std::uint8_t> read{reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size()};
			return {generation, Core::DecodeImage(read, *format, limits), stamp, read.size()};
		}
		catch (const std::bad_alloc&)
		{
			return {generation, Core::Error{Core::ErrorCode::IoFailure, "there is not enough memory to read it"}, stamp};
		}
	}

	void ImageTab::TakeLanded()
	{
		for (LoadedPicture& picture : m_Landed->TakeAll())
		{
			// Only the newest load counts; one asked for before a release or another load lands as nothing.
			if (picture.Generation == m_Generation)
			{
				m_LoadRunning = false;
				Apply(std::move(picture));
			}
		}

		if (m_CheckAfterLoad && !m_LoadRunning)
		{
			m_CheckAfterLoad = false;
			CheckDisk();
		}
	}

	void ImageTab::Apply(LoadedPicture picture)
	{
		m_Stamp = picture.Stamp;

		if (!picture.Decoded)
		{
			const Core::Error& error = picture.Decoded.GetError();

			if (!m_Frames.empty())
			{
				m_ReloadFailure = "Could not read the new version: " + error.Message;
				return;
			}

			// The facts of a released picture would describe one that is no longer shown.
			m_Facts.reset();
			m_Failure = FailureLine(error);
			m_FailureKind = KindOf(error);
			m_CanRetry = error.Code != Core::ErrorCode::TooLarge;
			m_State = ImageTabState::Failed;
			return;
		}

		Core::DecodedImage decoded = std::move(picture.Decoded).GetValue();

		if (!m_Facts || m_Facts->Width != decoded.FileWidth || m_Facts->Height != decoded.FileHeight)
		{
			m_Zoom.Opened = false;
		}

		ImageFacts facts;
		facts.Format = decoded.Format;
		facts.Width = decoded.FileWidth;
		facts.Height = decoded.FileHeight;
		facts.FileSize = picture.FileSize;
		facts.EntryCount = decoded.EntryCount;
		facts.FileFrameCount = decoded.FileFrameCount;
		facts.ShownFrameCount = decoded.Frames.size();
		m_Facts = facts;

		m_Frames = std::move(decoded.Frames);
		m_FrameIndex = 0;
		m_FrameShownAt.reset();
		m_Texture.reset();

		m_Failure.clear();
		m_FailureKind = ImageFailure::None;
		m_ReloadFailure.clear();
		m_State = ImageTabState::Ready;
	}

	void ImageTab::Animate(std::chrono::steady_clock::time_point now, bool shown)
	{
		if (m_Frames.size() < 2)
		{
			return;
		}

		// A hidden gif stands still and asks for nothing; shown again, its frame gets its whole delay.
		if (!shown)
		{
			m_FrameShownAt.reset();
			return;
		}

		if (!m_FrameShownAt)
		{
			m_FrameShownAt = now;
		}

		const std::chrono::steady_clock::time_point due = *m_FrameShownAt + m_Frames[m_FrameIndex].Delay;

		if (now >= due)
		{
			m_FrameIndex = (m_FrameIndex + 1) % m_Frames.size();
			m_Texture.reset();

			// WARNING: Timed from when the frame was due, not from the late wake-up that showed it, or every frame
			// runs long by that lateness. Woken so late that the new frame would be due already, as after a long
			// pause, the timing starts again from now, so the frames it missed are skipped rather than played at once.
			const bool newFrameAlreadyDue = now - due >= m_Frames[m_FrameIndex].Delay;
			m_FrameShownAt = newFrameAlreadyDue ? now : due;
		}

		Core::Redraw::RequestIn(*m_FrameShownAt + m_Frames[m_FrameIndex].Delay - now);
	}
}
