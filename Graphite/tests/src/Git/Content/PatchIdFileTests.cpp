#include <doctest/doctest.h>

#include "Git/Content/PatchIdFile.h"

#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <cstdint>
#include <optional>
#include <string>

using namespace Graphite;

namespace {
	Core::Oid IdOf(std::uint8_t seed)
	{
		Core::Oid id;
		id.Bytes.fill(seed);
		return id;
	}

	bool LoadsNothing(TempDirectory& directory, const std::string& name, const std::string& bytes)
	{
		const std::filesystem::path path = directory.MakeFile(name, bytes);
		Core::PatchIdCache cache;
		LoadPatchIds(path, cache);
		return cache.GetSize() == 0;
	}
}

TEST_CASE("Git window: patch ids survive a save and a load")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "sub" / "patches.bin";

	Core::PatchIdCache saved;
	saved.Store(IdOf(1), IdOf(11));
	saved.Store(IdOf(2), std::nullopt);
	saved.Store(IdOf(3), IdOf(13));
	REQUIRE(SavePatchIds(file, saved));

	Core::PatchIdCache loaded;
	LoadPatchIds(file, loaded);

	CHECK(loaded.GetSize() == 3);
	REQUIRE(loaded.Find(IdOf(1)).has_value());
	CHECK(*loaded.Find(IdOf(1)) == std::optional<Core::Oid>(IdOf(11)));
	REQUIRE(loaded.Find(IdOf(2)).has_value());
	CHECK_FALSE(loaded.Find(IdOf(2))->has_value());
	REQUIRE(loaded.Find(IdOf(3)).has_value());
	CHECK(*loaded.Find(IdOf(3)) == std::optional<Core::Oid>(IdOf(13)));
	CHECK_FALSE(loaded.Find(IdOf(4)).has_value());
}

TEST_CASE("Git window: a damaged patch id file loads nothing")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "patches.bin";

	Core::PatchIdCache saved;
	saved.Store(IdOf(1), IdOf(11));
	saved.Store(IdOf(2), std::nullopt);
	REQUIRE(SavePatchIds(file, saved));
	const std::string good = Core::ReadFileBytes(file).GetValue();

	// Sanity: the undamaged bytes do load, so the failures below come from the damage.
	{
		Core::PatchIdCache cache;
		LoadPatchIds(directory.MakeFile("good.bin", good), cache);
		CHECK(cache.GetSize() == 2);
	}

	CHECK(LoadsNothing(directory, "truncated.bin", good.substr(0, good.size() - 1)));
	CHECK(LoadsNothing(directory, "extra.bin", good + "x"));
	CHECK(LoadsNothing(directory, "empty.bin", ""));
	CHECK(LoadsNothing(directory, "foreign.bin", "GPI2" + good.substr(4)));

	std::string badFlag = good;
	badFlag[8 + Core::Oid::kByteCount] = '\x07';
	CHECK(LoadsNothing(directory, "flag.bin", badFlag));

	std::string hugeCount = good;
	hugeCount.replace(4, 4, "\xFF\xFF\xFF\xFF", 4);
	CHECK(LoadsNothing(directory, "count.bin", hugeCount));

	Core::PatchIdCache missing;
	LoadPatchIds(directory.GetPath() / "missing.bin", missing);
	CHECK(missing.GetSize() == 0);
}
