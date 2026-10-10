#include <doctest/doctest.h>

#include "Core/Base/ByteText.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Hash.h"

#include "TempDirectory.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr std::string_view kFourHundredFortyEightBits = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

	constexpr std::string_view kEightHundredNinetySixBits =
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

	std::string HexDigestOf(Core::HashAlgorithm algorithm, std::string_view bytes)
	{
		const Core::Result<std::vector<std::uint8_t>> digest = Core::HashBytes(algorithm, bytes);
		REQUIRE(digest);
		return Core::BytesToHex(digest.GetValue());
	}
}

TEST_CASE("SHA-256 gives the FIPS 180-4 example digests")
{
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha256, "abc") ==
		  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha256, "") ==
		  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha256, kFourHundredFortyEightBits) ==
		  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-512 gives the FIPS 180-4 example digests")
{
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha512, "abc") ==
		  "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
		  "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha512, "") ==
		  "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
		  "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
	CHECK(HexDigestOf(Core::HashAlgorithm::Sha512, kEightHundredNinetySixBits) ==
		  "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
		  "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

TEST_CASE("Hashing a file gives the digest of its bytes across pieces and outside the ANSI code page")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = temporaryDirectory.GetPath() / Core::FromUtf8("pobrany plik ł.zip");
	const std::string contents(3 * 1024 * 1024 + 7, 'a');

	{
		std::ofstream stream{file, std::ios::binary};
		stream << contents;
	}

	const Core::Result<std::vector<std::uint8_t>> fromFile = Core::HashFile(Core::HashAlgorithm::Sha256, file);
	const Core::Result<std::vector<std::uint8_t>> fromBytes = Core::HashBytes(Core::HashAlgorithm::Sha256, contents);

	REQUIRE(fromFile);
	REQUIRE(fromBytes);
	CHECK(fromFile.GetValue() == fromBytes.GetValue());
}

TEST_CASE("Hashing a file that does not exist is NotFound")
{
	TempDirectory temporaryDirectory;

	const Core::Result<std::vector<std::uint8_t>> digest =
		Core::HashFile(Core::HashAlgorithm::Sha512, temporaryDirectory.GetPath() / "missing.zip");

	REQUIRE_FALSE(digest);
	CHECK(digest.GetError().Code == Core::ErrorCode::NotFound);
}
