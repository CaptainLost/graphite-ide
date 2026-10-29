#include "Hex/HexFile.h"

#include "Core/FileSystem/FileRange.h"

#include <algorithm>
#include <cstddef>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		// The reader's messages name the path and are whole sentences, so the line is worded from the code instead.
		std::string ReadFailure(const Core::Error& error)
		{
			switch (error.Code)
			{
			case Core::ErrorCode::NotFound:
				return "the file was not found";
			case Core::ErrorCode::AccessDenied:
				return "the file cannot be opened";
			case Core::ErrorCode::None:
			case Core::ErrorCode::InvalidArgument:
			case Core::ErrorCode::IoFailure:
			case Core::ErrorCode::Unsupported:
			case Core::ErrorCode::TooLarge:
			case Core::ErrorCode::Cancelled:
				break;
			}

			return "reading the file failed";
		}
	}

	HexFile::HexFile(std::filesystem::path path, Core::JobSubmitter submit)
		: m_Path(std::move(path)), m_Submit(std::move(submit)), m_Stamp(ReadStamp(m_Path)),
		  m_Size(m_Stamp ? m_Stamp->Size : 0)
	{
	}

	void HexFile::Want(std::uint64_t firstByte, std::uint64_t lastByte)
	{
		++m_WantCount;

		if (m_Size == 0 || firstByte > lastByte || firstByte >= m_Size)
		{
			return;
		}

		const std::uint64_t lastBlock = std::min(lastByte, m_Size - 1) / kHexBlockBytes;

		for (std::uint64_t index = firstByte / kHexBlockBytes; index <= lastBlock; ++index)
		{
			m_Wanted[index] = m_WantCount;

			if (const auto cached = m_Blocks.find(index); cached != m_Blocks.end())
			{
				cached->second.LastWanted = m_WantCount;
			}

			if (const auto reading = m_Reading.find(index); reading != m_Reading.end())
			{
				reading->second = m_WantCount;
			}

			Request(index, m_WantCount);
		}
	}

	void HexFile::Update()
	{
		for (LandedBlock& landed : m_Landed->TakeAll())
		{
			// A read asked for before the file changed may hold either version, so only the newest counts.
			if (landed.Generation != m_Generation)
			{
				continue;
			}

			const auto reading = m_Reading.find(landed.Index);
			const std::uint64_t lastWanted = reading != m_Reading.end() ? reading->second : 0;
			m_Reading.erase(landed.Index);

			if (!landed.Bytes)
			{
				m_Failure = ReadFailure(landed.Bytes.GetError());
				continue;
			}

			const auto [cached, firstRead] = m_Blocks.try_emplace(landed.Index);

			// A block already here was read before the file changed, so the bytes on screen may have changed with it.
			if (!firstRead)
			{
				++m_ChangeCount;
			}

			cached->second.Bytes = std::move(landed.Bytes).GetValue();
			cached->second.Generation = m_Generation;
			cached->second.LastWanted = lastWanted;
		}

		DropLeastRecentlyWanted();

		std::erase_if(m_Wanted, [this](const auto& entry) { return entry.second <= m_WantCountAtUpdate; });
		m_WantCountAtUpdate = m_WantCount;
	}

	std::optional<std::uint8_t> HexFile::ByteAt(std::uint64_t offset) const
	{
		if (offset >= m_Size)
		{
			return std::nullopt;
		}

		const auto cached = m_Blocks.find(offset / kHexBlockBytes);

		if (cached == m_Blocks.end())
		{
			return std::nullopt;
		}

		const std::uint64_t within = offset % kHexBlockBytes;
		const std::vector<std::uint8_t>& bytes = cached->second.Bytes;

		if (within >= bytes.size())
		{
			return std::nullopt;
		}

		return bytes[static_cast<std::size_t>(within)];
	}

	std::vector<std::uint8_t> HexFile::BytesFrom(std::uint64_t offset, std::size_t count) const
	{
		std::vector<std::uint8_t> found;

		if (offset >= m_Size)
		{
			return found;
		}

		const std::uint64_t end = offset + std::min<std::uint64_t>(count, m_Size - offset);

		for (std::uint64_t at = offset; at < end;)
		{
			const auto cached = m_Blocks.find(at / kHexBlockBytes);

			if (cached == m_Blocks.end())
			{
				break;
			}

			const std::vector<std::uint8_t>& bytes = cached->second.Bytes;
			const std::size_t within = static_cast<std::size_t>(at % kHexBlockBytes);

			if (within >= bytes.size())
			{
				break;
			}

			const std::size_t taken = static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size() - within, end - at));
			const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(within);
			found.insert(found.end(), first, first + static_cast<std::ptrdiff_t>(taken));
			at += taken;
		}

		return found;
	}

	void HexFile::CheckDisk()
	{
		const std::optional<DiskStamp> onDisk = ReadStamp(m_Path);

		// A file that cannot be looked at keeps what it showed; the tab's missing state speaks for a deleted file.
		if (!onDisk || onDisk == m_Stamp)
		{
			return;
		}

		m_Stamp = onDisk;
		++m_Generation;
		m_Reading.clear();
		m_Failure.clear();

		if (onDisk->Size != m_Size)
		{
			m_Size = onDisk->Size;
			++m_ChangeCount;
			DropBlocksPastTheEnd();
		}

		RequestWanted();
	}

	void HexFile::Retry()
	{
		m_Failure.clear();
		CheckDisk();
		RequestWanted();
	}

	std::uint64_t HexFile::GetSize() const
	{
		return m_Size;
	}

	bool HexFile::IsWaiting() const
	{
		return std::ranges::any_of(m_Reading, [this](const auto& reading) { return m_Wanted.contains(reading.first); });
	}

	const std::string& HexFile::GetFailure() const
	{
		return m_Failure;
	}

	std::uint64_t HexFile::GetChangeCount() const
	{
		return m_ChangeCount;
	}

	std::optional<HexFile::DiskStamp> HexFile::ReadStamp(const std::filesystem::path& path)
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

	void HexFile::Request(std::uint64_t index, std::uint64_t lastWanted)
	{
		// A failed read waits for Retry or a change on disk, rather than being tried again on every frame.
		if (!m_Failure.empty() || m_Reading.contains(index) || index * kHexBlockBytes >= m_Size)
		{
			return;
		}

		if (const auto cached = m_Blocks.find(index); cached != m_Blocks.end() && cached->second.Generation == m_Generation)
		{
			return;
		}

		m_Reading.emplace(index, lastWanted);

		m_Submit([path = m_Path, generation = m_Generation, index, landed = m_Landed] {
			landed->Post(LandedBlock{generation, index, Core::ReadFileRange(path, index * kHexBlockBytes, kHexBlockBytes)});
		});
	}

	void HexFile::RequestWanted()
	{
		for (const auto& [index, lastWanted] : m_Wanted)
		{
			Request(index, lastWanted);
		}
	}

	void HexFile::DropBlocksPastTheEnd()
	{
		std::erase_if(m_Blocks, [this](const auto& entry) { return entry.first * kHexBlockBytes >= m_Size; });
	}

	void HexFile::DropLeastRecentlyWanted()
	{
		while (m_Blocks.size() > kHexCachedBlocks)
		{
			const auto oldest = std::ranges::min_element(
				m_Blocks, [](const auto& left, const auto& right) { return left.second.LastWanted < right.second.LastWanted; });
			m_Blocks.erase(oldest);
		}
	}
}
