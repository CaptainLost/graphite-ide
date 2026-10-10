#include <doctest/doctest.h>

#include "Language/Install/GitHubReleaseSource.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/Base/ByteText.h"

#include "FakeHttpClient.h"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace {

	constexpr Core::PlatformTarget kWindowsX64{.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64};
	constexpr Core::PlatformTarget kLinuxX64{.System = Core::OperatingSystem::Linux, .Machine = Core::Architecture::X64};

	constexpr std::array<Graphite::GitHubAsset, 1> kToolAssets{{
		{.Platform = kWindowsX64,
		 .AssetTemplate = "tool-{os}-{version}.zip",
		 .Archive = Graphite::ArchiveKind::Zip,
		 .ExecutableTemplate = "tool_{version}/bin/tool{exe}"},
	}};

	constexpr Graphite::GitHubReleasePackage kTool{.Repository = "example/tool", .Assets = kToolAssets};

	constexpr const char* kClangdLatest = "https://api.github.com/repos/clangd/clangd/releases/latest";
	constexpr const char* kToolLatest = "https://api.github.com/repos/example/tool/releases/latest";
	constexpr const char* kClangdDigest = "sha256:23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810";

	struct ReleaseAsset
	{
		std::string Name;
		std::optional<nlohmann::json> Digest;
	};

	std::string ReleaseJson(const std::string& repository, const std::string& tag, const std::vector<ReleaseAsset>& assets)
	{
		nlohmann::json release = nlohmann::json::object();
		release["tag_name"] = tag;
		release["assets"] = nlohmann::json::array();

		for (const ReleaseAsset& asset : assets)
		{
			nlohmann::json entry = nlohmann::json::object();
			entry["name"] = asset.Name;
			entry["browser_download_url"] = "https://github.com/" + repository + "/releases/download/" + tag + "/" + asset.Name;

			if (asset.Digest)
			{
				entry["digest"] = *asset.Digest;
			}

			release["assets"].push_back(entry);
		}

		return release.dump();
	}

	const Graphite::GitHubReleasePackage& ClangdPackage()
	{
		const Graphite::GitHubReleasePackage* package =
			std::get_if<Graphite::GitHubReleasePackage>(&Graphite::FindServer("clangd")->Package);
		REQUIRE(package != nullptr);
		return *package;
	}
}

TEST_CASE("The latest GitHub release resolves to the platform's asset and its digest and the server inside it")
{
	FakeHttpClient http;
	http.Answer(kClangdLatest, 200,
				ReleaseJson("clangd/clangd", "23.1.0",
							{{.Name = "clangd_indexing_tools-windows-23.1.0.zip", .Digest = nlohmann::json{"sha256:00"}},
							 {.Name = "clangd-windows-23.1.0.zip", .Digest = nlohmann::json(kClangdDigest)}}));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, ClangdPackage(), kWindowsX64);

	REQUIRE(resolved);
	const Graphite::ResolvedPackage& package = resolved.GetValue();
	CHECK(package.Version == "23.1.0");
	CHECK(package.FileName == "clangd-windows-23.1.0.zip");
	CHECK(package.DownloadUrl == "https://github.com/clangd/clangd/releases/download/23.1.0/clangd-windows-23.1.0.zip");
	CHECK(package.Archive == Graphite::ArchiveKind::Zip);
	CHECK(package.ExecutableInArchive == "clangd_23.1.0/bin/clangd.exe");
	REQUIRE(package.ExpectedChecksum.has_value());
	CHECK(Graphite::DescribeChecksum(*package.ExpectedChecksum) == kClangdDigest);

	REQUIRE(http.Requests.size() == 1);
	CHECK(http.Requests.front().Url == kClangdLatest);
	REQUIRE(http.Requests.front().Headers.size() == 1);
	CHECK(http.Requests.front().Headers.front().Name == "Accept");
	CHECK(http.Requests.front().Headers.front().Value == "application/vnd.github+json");
}

TEST_CASE("A tag starting with v gives the version without it")
{
	FakeHttpClient http;
	http.Answer(kToolLatest, 200, ReleaseJson("example/tool", "v2.0.0", {{.Name = "tool-windows-2.0.0.zip"}}));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

	REQUIRE(resolved);
	CHECK(resolved.GetValue().Version == "2.0.0");
	CHECK(resolved.GetValue().FileName == "tool-windows-2.0.0.zip");
	CHECK(resolved.GetValue().ExecutableInArchive == "tool_2.0.0/bin/tool.exe");
}

TEST_CASE("An asset without a digest or with a null one resolves without a checksum")
{
	FakeHttpClient http;

	SUBCASE("absent")
	{
		http.Answer(kToolLatest, 200, ReleaseJson("example/tool", "1.0.0", {{.Name = "tool-windows-1.0.0.zip"}}));
	}

	SUBCASE("null")
	{
		http.Answer(kToolLatest, 200,
					ReleaseJson("example/tool", "1.0.0", {{.Name = "tool-windows-1.0.0.zip", .Digest = nlohmann::json(nullptr)}}));
	}

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

	REQUIRE(resolved);
	CHECK_FALSE(resolved.GetValue().ExpectedChecksum.has_value());
}

TEST_CASE("A digest that cannot be read fails the resolution")
{
	FakeHttpClient http;
	http.Answer(kToolLatest, 200,
				ReleaseJson("example/tool", "1.0.0",
							{{.Name = "tool-windows-1.0.0.zip", .Digest = nlohmann::json("md5:d41d8cd98f00b204e9800998ecf8427e")}}));

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Message.find("tool-windows-1.0.0.zip") != std::string::npos);
}

TEST_CASE("GitHub's refusal is reported with its own message")
{
	FakeHttpClient http;
	http.Answer(kToolLatest, 403, R"({"message":"API rate limit exceeded for 192.0.2.1."})");

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Message == "GitHub answered 403 for example/tool: API rate limit exceeded for 192.0.2.1.");
}

TEST_CASE("A proxy's demand for credentials is named as the proxy's")
{
	FakeHttpClient http;
	http.Answer(kToolLatest, 407, "<html>Proxy Authentication Required</html>");

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Code == Core::ErrorCode::AccessDenied);
	CHECK(resolved.GetError().Message ==
		  "the proxy asked for credentials (HTTP 407) when fetching 'https://api.github.com/repos/example/tool/releases/latest'");
}

TEST_CASE("A release without the platform's asset or with a tag that cannot name a directory is refused")
{
	FakeHttpClient http;

	SUBCASE("no such asset")
	{
		http.Answer(kToolLatest, 200, ReleaseJson("example/tool", "1.0.0", {{.Name = "tool-linux-1.0.0.zip"}}));

		const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

		REQUIRE_FALSE(resolved);
		CHECK(resolved.GetError().Code == Core::ErrorCode::NotFound);
		CHECK(resolved.GetError().Message.find("tool-windows-1.0.0.zip") != std::string::npos);
	}

	SUBCASE("a tag that climbs")
	{
		http.Answer(kToolLatest, 200, ReleaseJson("example/tool", "../evil", {{.Name = "tool-windows-../evil.zip"}}));

		const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kWindowsX64);

		REQUIRE_FALSE(resolved);
		CHECK(resolved.GetError().Code == Core::ErrorCode::InvalidArgument);
	}
}

TEST_CASE("A platform with no build is refused before GitHub is asked")
{
	FakeHttpClient http;

	const Core::Result<Graphite::ResolvedPackage> resolved = Graphite::ResolveGitHubRelease(http, kTool, kLinuxX64);

	REQUIRE_FALSE(resolved);
	CHECK(resolved.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(http.Requests.empty());
}
