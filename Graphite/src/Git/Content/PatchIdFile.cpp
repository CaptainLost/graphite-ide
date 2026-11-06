#include "Git/Content/PatchIdFile.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kMagic = "GPI1";
		constexpr std::size_t kCountBytes = 4;
		constexpr std::size_t kFlagOffset = Core::Oid::kByteCount;
		constexpr std::size_t kPatchOffset = kFlagOffset + 1;
		constexpr std::size_t kRecordBytes = kPatchOffset + Core::Oid::kByteCount;

		void PutOid(std::string& out, const Core::Oid& id)
		{
			out.append(reinterpret_cast<const char*>(id.Bytes.data()), Core::Oid::kByteCount);
		}

		Core::Oid OidAt(std::string_view bytes, std::size_t position)
		{
			Core::Oid id;
			std::memcpy(id.Bytes.data(), bytes.data() + position, Core::Oid::kByteCount);
			return id;
		}
	}

	Core::Result<void> SavePatchIds(const std::filesystem::path& file, const Core::PatchIdCache& cache)
	{
		const std::vector<Core::PatchIdEntry> entries = cache.Entries();

		std::string bytes(kMagic);

		for (std::size_t index = 0; index < kCountBytes; ++index)
		{
			bytes.push_back(static_cast<char>((entries.size() >> (8 * index)) & 0xFF));
		}

		for (const Core::PatchIdEntry& entry : entries)
		{
			PutOid(bytes, entry.Commit);
			bytes.push_back(entry.PatchId ? '\x01' : '\x00');
			PutOid(bytes, entry.PatchId ? *entry.PatchId : Core::Oid{});
		}

		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		return Core::WriteFileAtomically(file, bytes);
	}

	void LoadPatchIds(const std::filesystem::path& file, Core::PatchIdCache& cache)
	{
		const Core::Result<std::string> read = Core::ReadFileBytes(file);

		if (!read)
		{
			return;
		}

		const std::string_view bytes = read.GetValue();
		const std::size_t header = kMagic.size() + kCountBytes;

		if (bytes.size() < header || bytes.substr(0, kMagic.size()) != kMagic)
		{
			return;
		}

		std::uint64_t count = 0;

		for (std::size_t index = 0; index < kCountBytes; ++index)
		{
			count |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[kMagic.size() + index])) << (8 * index);
		}

		// The size is checked against the count first, so a hostile count cannot make a loop run past the buffer.
		const std::size_t body = bytes.size() - header;

		if (body % kRecordBytes != 0 || body / kRecordBytes != count)
		{
			return;
		}

		const std::string_view records = bytes.substr(header);

		// Read everything before keeping anything: a file with one bad record loads nothing.
		for (std::size_t record = 0; record < records.size(); record += kRecordBytes)
		{
			if (static_cast<std::uint8_t>(records[record + kFlagOffset]) > 1)
			{
				return;
			}
		}

		for (std::size_t record = 0; record < records.size(); record += kRecordBytes)
		{
			const Core::Oid commit = OidAt(records, record);

			if (records[record + kFlagOffset] == '\x01')
			{
				cache.Store(commit, OidAt(records, record + kPatchOffset));
			}
			else
			{
				cache.Store(commit, std::nullopt);
			}
		}
	}
}
