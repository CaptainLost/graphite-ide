#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/Mailbox.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kHexBlockBytes = 64 * 1024;
	inline constexpr std::size_t kHexCachedBlocks = 64;

	// The bytes behind one hex tab: read in blocks by jobs as the view asks for them, kept in a small cache, and swapped
	// for their new version without blanking when the file changes. Every call is made on the UI thread.
	class HexFile
	{
	public:
		HexFile(std::filesystem::path path, Core::JobSubmitter submit);

		// Asks for the blocks holding both ends and everything between, each once while its read runs. The view calls it
		// every frame for the rows on screen: a block stays wanted until the second update after its last Want.
		void Want(std::uint64_t firstByte, std::uint64_t lastByte);

		// Takes the blocks read for the file as it was last checked. Called once a frame.
		void Update();

		// Also an older version's byte while the new one is read; nullopt while unread and past the end.
		std::optional<std::uint8_t> ByteAt(std::uint64_t offset) const;

		// What is cached from `offset`, stopping at the first unread byte.
		std::vector<std::uint8_t> BytesFrom(std::uint64_t offset, std::size_t count) const;

		// On a new modified time or size, reads the wanted blocks again and shows their old bytes until they land. A
		// changed file is read even after a failure, as Retry would read it.
		void CheckDisk();

		void Retry();

		std::uint64_t GetSize() const;

		// True while a wanted block is being read.
		bool IsWaiting() const;

		// Empty, or why a read failed.
		const std::string& GetFailure() const;

		// Moves on when the size changes or a block's new version replaces its old one, so the view can clamp.
		std::uint64_t GetChangeCount() const;

	private:
		struct DiskStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;

			bool operator==(const DiskStamp&) const = default;
		};

		struct LandedBlock
		{
			std::uint64_t Generation = 0;
			std::uint64_t Index = 0;
			Core::Result<std::vector<std::uint8_t>> Bytes;
		};

		struct CachedBlock
		{
			std::vector<std::uint8_t> Bytes;

			// Older than the file's for a block read before the file changed, which is shown until its new version lands.
			std::uint64_t Generation = 0;
			std::uint64_t LastWanted = 0;
		};

		static std::optional<DiskStamp> ReadStamp(const std::filesystem::path& path);

		void Request(std::uint64_t index, std::uint64_t lastWanted);
		void RequestWanted();
		void DropBlocksPastTheEnd();
		void DropLeastRecentlyWanted();

		std::filesystem::path m_Path;
		Core::JobSubmitter m_Submit;

		// WARNING: Held through a shared_ptr so a read still finishing after the tab was closed has somewhere to post.
		Core::Ref<Core::Mailbox<LandedBlock>> m_Landed = Core::CreateRef<Core::Mailbox<LandedBlock>>();

		std::optional<DiskStamp> m_Stamp;
		std::uint64_t m_Size = 0;
		std::uint64_t m_Generation = 0;
		std::uint64_t m_ChangeCount = 0;
		std::string m_Failure;

		std::map<std::uint64_t, CachedBlock> m_Blocks;

		// The blocks being read for the current generation, each by the number of the Want that last asked for it, so a
		// block landing after it left the screen is not taken for the most recently wanted.
		std::map<std::uint64_t, std::uint64_t> m_Reading;

		// The wanted blocks, numbered the same way. Kept across one update, so a block on screen counts as wanted
		// wherever in the frame it is asked for.
		std::map<std::uint64_t, std::uint64_t> m_Wanted;
		std::uint64_t m_WantCount = 0;
		std::uint64_t m_WantCountAtUpdate = 0;
	};
}
