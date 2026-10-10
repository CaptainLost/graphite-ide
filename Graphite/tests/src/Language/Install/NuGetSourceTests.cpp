#include <doctest/doctest.h>

#include "Language/Install/NuGetSource.h"

#include "Core/Base/ByteText.h"

#include "FakeHttpClient.h"

#include <nlohmann/json.hpp>

#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};

	constexpr Graphite::NuGetToolPackage kRoslyn{.PackageIdTemplate = "roslyn-language-server.{rid}", .AllowPrerelease = true};
	constexpr Graphite::NuGetToolPackage kCsharpLs{.PackageIdTemplate = "CSharp-LS", .AllowPrerelease = false};

	constexpr const char* kRoslynIndex =
		"https://api.nuget.org/v3/registration5-gz-semver2/roslyn-language-server.win-x64/index.json";
	constexpr const char* kCsharpLsIndex = "https://api.nuget.org/v3/registration5-gz-semver2/csharp-ls/index.json";

	constexpr const char* kRoslynHash =
		"BTuCo+sIfXQ9+0ocb18CS6WLqVAzG4DqViQ1L6suxiweGzTwpTU8T5Zf/aAscWhOVebInJleMRVq0Q8dxZb6bA==";

	struct RegisteredVersion
	{
		std::string Version;
		bool Listed = true;
	};

	std::string CatalogUrlOf(const std::string& packageId, const std::string& version)
	{
		return std::format("https://api.nuget.org/v3/catalog0/data/2026.09.30.21.56.16/{}.{}.json", packageId, version);
	}

	nlohmann::json LeavesOf(const std::string& packageId, const std::vector<RegisteredVersion>& versions)
	{
		nlohmann::json leaves = nlohmann::json::array();

		for (const RegisteredVersion& registered : versions)
		{
			nlohmann::json catalogEntry = nlohmann::json::object();
			catalogEntry["@id"] = CatalogUrlOf(packageId, registered.Version);
			catalogEntry["version"] = registered.Version;
			catalogEntry["listed"] = registered.Listed;

			nlohmann::json leaf = nlohmann::json::object();
			leaf["catalogEntry"] = catalogEntry;
			leaves.push_back(leaf);
		}

		return leaves;
	}

	std::string IndexWithPages(const std::vector<nlohmann::json>& pages)
	{
		nlohmann::json index = nlohmann::json::object();
		index["count"] = pages.size();
		index["items"] = nlohmann::json::array();

		for (const nlohmann::json& page : pages)
		{
			index["items"].push_back(page);
		}

		return index.dump();
	}

	nlohmann::json InlinedPage(const std::string& packageId, const std::vector<RegisteredVersion>& versions)
	{
		nlohmann::json page = nlohmann::json::object();
		page["@id"] = std::format("https://api.nuget.org/v3/registration5-gz-semver2/{}/index.json#page/0", packageId);
		page["items"] = LeavesOf(packageId, versions);
		return page;
	}

	std::string CatalogEntryWithHash(const std::optional<std::string>& packageHash, const std::string& algorithm)
	{
		nlohmann::json entry = nlohmann::json::object();

		if (packageHash)
		{
			entry["packageHash"] = *packageHash;
			entry["packageHashAlgorithm"] = algorithm;
		}

		return entry.dump();
	}
}

TEST_CASE("The highest listed version is resolved with the SHA-512 from its catalogue entry")
{
	FakeHttpClient http;
	const std::string packageId = "roslyn-language-server.win-x64";
	http.Answer(kRoslynIndex, 200,
				IndexWithPages({InlinedPage(packageId, {{.Version = "5.9.0-1.26303.1"},
														{.Version = "5.11.0", .Listed = false},
														{.Version = "5.12.0-1.26426.8"},
														{.Version = "5.12.0-1.26475.2"}})}));
	http.Answer(CatalogUrlOf(packageId, "5.12.0-1.26475.2"), 200, CatalogEntryWithHash(kRoslynHash, "SHA512"));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kRoslyn, kWindowsX64);

	REQUIRE(resolved);
	const Graphite::ResolvedPackage& package = resolved.GetValue();
	CHECK(package.Version == "5.12.0-1.26475.2");
	CHECK(package.FileName == "roslyn-language-server.win-x64.5.12.0-1.26475.2.nupkg");
	CHECK(package.DownloadUrl ==
		  "https://api.nuget.org/v3-flatcontainer/roslyn-language-server.win-x64/5.12.0-1.26475.2/"
		  "roslyn-language-server.win-x64.5.12.0-1.26475.2.nupkg");
	CHECK(package.Archive == Graphite::ArchiveKind::Zip);
	CHECK(package.ExecutableInArchive.empty());
	REQUIRE(package.ExpectedChecksum.has_value());
	CHECK(package.ExpectedChecksum->Algorithm == Core::HashAlgorithm::Sha512);
	CHECK(Core::BytesToHex(package.ExpectedChecksum->Digest) ==
		  "053b82a3eb087d743dfb4a1c6f5f024ba58ba950331b80ea5624352fab2ec62c"
		  "1e1b34f0a5353c4f965ffda02c71684e55e6c89c995e31156ad10f1dc596fa6c");
}

TEST_CASE("Without prereleases allowed the highest stable listed version wins")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200,
				IndexWithPages({InlinedPage("csharp-ls", {{.Version = "0.27.0"}, {.Version = "0.28.0"}, {.Version = "0.29.0-beta.1"}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, CatalogEntryWithHash(std::nullopt, ""));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().Version == "0.28.0");
	CHECK_FALSE(resolved.GetValue().ExpectedChecksum.has_value());
}

TEST_CASE("An unlisted version is never chosen even when it is the highest")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200,
				IndexWithPages({InlinedPage("csharp-ls", {{.Version = "1.0.0"}, {.Version = "2.0.0", .Listed = false}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "1.0.0"), 200, CatalogEntryWithHash(std::nullopt, ""));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().Version == "1.0.0");
}

TEST_CASE("A page that does not inline its versions is fetched by its address")
{
	FakeHttpClient http;
	const std::string pageUrl = "https://api.nuget.org/v3/registration5-gz-semver2/csharp-ls/page/0.1.0/0.28.0.json";
	nlohmann::json pointer = nlohmann::json::object();
	pointer["@id"] = pageUrl;
	pointer["count"] = 1;

	nlohmann::json page = nlohmann::json::object();
	page["@id"] = pageUrl;
	page["items"] = LeavesOf("csharp-ls", {{.Version = "0.28.0"}});

	http.Answer(kCsharpLsIndex, 200, IndexWithPages({pointer}));
	http.Answer(pageUrl, 200, page.dump());
	http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, CatalogEntryWithHash(std::nullopt, ""));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().Version == "0.28.0");
}

TEST_CASE("A package id is lower-cased in every address")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200, IndexWithPages({InlinedPage("csharp-ls", {{.Version = "0.28.0"}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, CatalogEntryWithHash(std::nullopt, ""));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().DownloadUrl ==
		  "https://api.nuget.org/v3-flatcontainer/csharp-ls/0.28.0/csharp-ls.0.28.0.nupkg");
	REQUIRE_FALSE(http.Requests.empty());
	CHECK(http.Requests.front().Url == kCsharpLsIndex);
}

TEST_CASE("A hash that is present but unreadable fails the resolution")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200, IndexWithPages({InlinedPage("csharp-ls", {{.Version = "0.28.0"}})}));

	SUBCASE("another algorithm")
	{
		http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, CatalogEntryWithHash(kRoslynHash, "SHA3-512"));
	}

	SUBCASE("not base64")
	{
		http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, CatalogEntryWithHash("not a hash", "SHA512"));
	}

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Message.find("csharp-ls 0.28.0") != std::string::npos);
}

TEST_CASE("A package NuGet does not know is reported as not found")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 404, "<Error><Code>BlobNotFound</Code></Error>");

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Code == Core::ErrorCode::NotFound);
	CHECK(resolved.GetError().Message.find("csharp-ls") != std::string::npos);
}

TEST_CASE("A hash of another JSON type fails the resolution")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200, IndexWithPages({InlinedPage("csharp-ls", {{.Version = "0.28.0"}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, R"({"packageHash": 42, "packageHashAlgorithm": "SHA512"})");

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Message.find("csharp-ls 0.28.0") != std::string::npos);
}

TEST_CASE("A null hash resolves without a checksum")
{
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200, IndexWithPages({InlinedPage("csharp-ls", {{.Version = "0.28.0"}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "0.28.0"), 200, R"({"packageHash": null})");

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, kCsharpLs, kWindowsX64);

	REQUIRE(resolved);
	CHECK_FALSE(resolved.GetValue().ExpectedChecksum.has_value());
}

TEST_CASE("A version with build metadata and an upper-case label installs lower-cased and without the metadata")
{
	constexpr Graphite::NuGetToolPackage prereleaseAllowed{.PackageIdTemplate = "CSharp-LS", .AllowPrerelease = true};
	FakeHttpClient http;
	http.Answer(kCsharpLsIndex, 200, IndexWithPages({InlinedPage("csharp-ls", {{.Version = "1.0.0-Beta.2+build.7"}})}));
	http.Answer(CatalogUrlOf("csharp-ls", "1.0.0-Beta.2+build.7"), 200, CatalogEntryWithHash(std::nullopt, ""));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveNuGetTool(http, prereleaseAllowed, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().Version == "1.0.0-beta.2");
	CHECK(resolved.GetValue().FileName == "csharp-ls.1.0.0-beta.2.nupkg");
	CHECK(resolved.GetValue().DownloadUrl ==
		  "https://api.nuget.org/v3-flatcontainer/csharp-ls/1.0.0-beta.2/csharp-ls.1.0.0-beta.2.nupkg");
}
