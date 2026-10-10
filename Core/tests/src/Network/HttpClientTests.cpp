#include <doctest/doctest.h>

#include "Core/Jobs/CancellationToken.h"
#include "Core/Network/HttpClient.h"

#include "TempDirectory.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <utility>

namespace {

	Core::Scope<Core::HttpClient> MakeClient()
	{
		Core::Result<Core::Scope<Core::HttpClient>> client = Core::CreateHttpClient("Graphite-tests");
		REQUIRE(client);
		return std::move(client).GetValue();
	}
}

TEST_CASE("A URL that is not https is refused before anything is sent")
{
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<Core::HttpResponse> plain = client->Fetch({.Url = "http://api.github.com/"});
	REQUIRE_FALSE(plain);
	CHECK(plain.GetError().Code == Core::ErrorCode::InvalidArgument);

	const Core::Result<Core::HttpResponse> notAUrl = client->Fetch({.Url = "not a url"});
	REQUIRE_FALSE(notAUrl);
	CHECK(notAUrl.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("A refused download leaves no file at its target")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.MakeFile("server.zip.part", "an earlier attempt");
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<void> downloaded =
		client->Download({.Url = "http://example.com/server.zip"}, target, nullptr, Core::CancellationToken{});

	REQUIRE_FALSE(downloaded);
	CHECK(downloaded.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("A download cancelled before it starts sends nothing and leaves no file")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "server.zip.part";
	const Core::Scope<Core::HttpClient> client = MakeClient();
	Core::CancellationToken token;
	token.Cancel();

	const Core::Result<void> downloaded =
		client->Download({.Url = "https://example.invalid/server.zip"}, target, nullptr, token);

	REQUIRE_FALSE(downloaded);
	CHECK(downloaded.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("A live HTTPS API answers with JSON" * doctest::skip())
{
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<Core::HttpResponse> answer =
		client->Fetch({.Url = "https://api.github.com/repos/clangd/clangd/releases/latest",
					   .Headers = {{.Name = "Accept", .Value = "application/vnd.github+json"}}});

	REQUIRE(answer);
	CHECK(answer.GetValue().Status == 200);
	CHECK(answer.GetValue().Body.find("\"tag_name\"") != std::string::npos);
}

TEST_CASE("A gzip-compressed answer arrives as plain JSON" * doctest::skip())
{
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<Core::HttpResponse> answer =
		client->Fetch({.Url = "https://api.nuget.org/v3/registration5-gz-semver2/csharp-ls/index.json"});

	REQUIRE(answer);
	CHECK(answer.GetValue().Status == 200);
	CHECK(answer.GetValue().Body.starts_with("{"));
	CHECK(answer.GetValue().Body.find("catalogEntry") != std::string::npos);
}

TEST_CASE("A file downloads through a redirect and reports progress" * doctest::skip())
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "SHA256SUMS";
	const Core::Scope<Core::HttpClient> client = MakeClient();
	Core::DownloadProgress lastProgress;
	const Core::DownloadObserver observer = [&lastProgress](const Core::DownloadProgress& progress) {
		lastProgress = progress;
	};

	const Core::Result<void> downloaded = client->Download(
		{.Url = "https://github.com/OmniSharp/omnisharp-roslyn/releases/download/v2.0.0/SHA256SUMS"}, target, observer,
		Core::CancellationToken{});

	REQUIRE(downloaded);
	CHECK(lastProgress.ReceivedBytes > 0);
	CHECK(lastProgress.TotalBytes == lastProgress.ReceivedBytes);
	CHECK(std::filesystem::file_size(target) == lastProgress.ReceivedBytes);
}
