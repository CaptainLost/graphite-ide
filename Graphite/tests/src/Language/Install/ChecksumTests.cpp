#include <doctest/doctest.h>

#include "Language/Install/Checksum.h"

#include "Core/Base/ByteText.h"
#include "Core/Platform/Hash.h"

#include "TempDirectory.h"

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	constexpr const char* kClangdDigest = "sha256:23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810";
}

TEST_CASE("A checksum reads back from the form GitHub publishes")
{
	const std::optional<Graphite::Checksum> checksum = Graphite::ParseChecksum(kClangdDigest);

	REQUIRE(checksum.has_value());
	CHECK(checksum->Algorithm == Core::HashAlgorithm::Sha256);
	CHECK(checksum->Digest.size() == 32);
	CHECK(Graphite::DescribeChecksum(*checksum) == kClangdDigest);
}

TEST_CASE("A checksum of another algorithm or the wrong length reads as nothing")
{
	CHECK_FALSE(Graphite::ParseChecksum("md5:d41d8cd98f00b204e9800998ecf8427e").has_value());
	CHECK_FALSE(Graphite::ParseChecksum("sha256:abcd").has_value());
	CHECK_FALSE(Graphite::ParseChecksum("SHA256:23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810").has_value());
	CHECK_FALSE(Graphite::ParseChecksum("23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810").has_value());
}

TEST_CASE("A file matches its own checksum and not another's")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = temporaryDirectory.MakeFile("server.zip", "the archive");
	const Core::Result<std::vector<std::uint8_t>> digest = Core::HashBytes(Core::HashAlgorithm::Sha512, "the archive");
	REQUIRE(digest);

	CHECK(Graphite::VerifyChecksum(file, {.Algorithm = Core::HashAlgorithm::Sha512, .Digest = digest.GetValue()}));

	std::vector<std::uint8_t> otherDigest = digest.GetValue();
	otherDigest.front() = static_cast<std::uint8_t>(otherDigest.front() ^ 0xFF);
	const Core::Result<void> mismatch =
		Graphite::VerifyChecksum(file, {.Algorithm = Core::HashAlgorithm::Sha512, .Digest = otherDigest});

	REQUIRE_FALSE(mismatch);
	CHECK(mismatch.GetError().Message.starts_with("the download's checksum does not match"));
}
