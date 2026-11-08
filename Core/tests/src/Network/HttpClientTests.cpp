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

	const Core::Result<Core::HttpResponse> plain = client->Fetch({.Url = "http://example.invalid/"});
	REQUIRE_FALSE(plain);
	CHECK(plain.GetError().Code == Core::ErrorCode::InvalidArgument);

	const Core::Result<Core::HttpResponse> notAUrl = client->Fetch({.Url = "not a url"});
	REQUIRE_FALSE(notAUrl);
	CHECK(notAUrl.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("A refused download leaves no file at its target")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.MakeFile("archive.zip.part", "an earlier attempt");
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<void> downloaded =
		client->Download({.Url = "http://example.invalid/archive.zip"}, target, nullptr, Core::CancellationToken{});

	REQUIRE_FALSE(downloaded);
	CHECK(downloaded.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("A download cancelled before it starts sends nothing and leaves no file")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "archive.zip.part";
	const Core::Scope<Core::HttpClient> client = MakeClient();
	Core::CancellationToken token;
	token.Cancel();

	const Core::Result<void> downloaded =
		client->Download({.Url = "https://example.invalid/archive.zip"}, target, nullptr, token);

	REQUIRE_FALSE(downloaded);
	CHECK(downloaded.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("A live HTTPS API answers with JSON" * doctest::skip())
{
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<Core::HttpResponse> answer =
		client->Fetch({.Url = "https://httpbin.org/json",
					   .Headers = {{.Name = "Accept", .Value = "application/json"}}});

	REQUIRE(answer);
	CHECK(answer.GetValue().Status == 200);
	CHECK(answer.GetValue().Body.find("\"slideshow\"") != std::string::npos);
}

TEST_CASE("A gzip-compressed answer arrives as plain JSON" * doctest::skip())
{
	const Core::Scope<Core::HttpClient> client = MakeClient();

	const Core::Result<Core::HttpResponse> answer =
		client->Fetch({.Url = "https://httpbin.org/gzip"});

	REQUIRE(answer);
	CHECK(answer.GetValue().Status == 200);
	CHECK(answer.GetValue().Body.starts_with("{"));
	CHECK(answer.GetValue().Body.find("\"gzipped\"") != std::string::npos);
}

TEST_CASE("A file downloads through a redirect and reports progress" * doctest::skip())
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "payload.bin";
	const Core::Scope<Core::HttpClient> client = MakeClient();
	Core::DownloadProgress lastProgress;
	const Core::DownloadObserver observer = [&lastProgress](const Core::DownloadProgress& progress) {
		lastProgress = progress;
	};

	const Core::Result<void> downloaded = client->Download(
		{.Url = "https://httpbin.org/redirect-to?url=https%3A%2F%2Fhttpbin.org%2Fbytes%2F4096"}, target, observer,
		Core::CancellationToken{});

	REQUIRE(downloaded);
	CHECK(lastProgress.ReceivedBytes > 0);
	CHECK(lastProgress.TotalBytes == lastProgress.ReceivedBytes);
	CHECK(std::filesystem::file_size(target) == lastProgress.ReceivedBytes);
}

TEST_CASE("A download cancelled midway stops and leaves no file" * doctest::skip())
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path target = temporaryDirectory.GetPath() / "archive.bin";
	const Core::Scope<Core::HttpClient> client = MakeClient();
	Core::CancellationToken token;
	const Core::DownloadObserver observer = [&token](const Core::DownloadProgress&) { token.Cancel(); };

	const Core::Result<void> downloaded = client->Download(
		{.Url = "https://httpbin.org/bytes/102400"}, target, observer,
		token);

	REQUIRE_FALSE(downloaded);
	CHECK(downloaded.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("A refused request reads as NotFound for a 404 and as an IO failure for any other status")
{
	CHECK(Core::ErrorCodeOfHttpStatus(Core::kHttpStatusNotFound) == Core::ErrorCode::NotFound);
	CHECK(Core::ErrorCodeOfHttpStatus(500) == Core::ErrorCode::IoFailure);
	CHECK(Core::ErrorCodeOfHttpStatus(Core::kHttpStatusProxyAuthenticationRequired) == Core::ErrorCode::IoFailure);
}
TEST_CASE("A refused download says what its status means, and to try again later when that may help")
{
	const std::string url = "https://github.com/clangd/clangd/releases/download/x/clangd.zip";

	const std::string serverTrouble = Core::DescribeRefusedDownload(url, 504);
	CHECK(serverTrouble.find("had a problem of its own (HTTP 504)") != std::string::npos);
	CHECK(serverTrouble.find("Try again in a while") != std::string::npos);
	CHECK(serverTrouble.find(url) != std::string::npos);

	CHECK(Core::DescribeRefusedDownload(url, 404).find("was not found there (HTTP 404)") != std::string::npos);
	CHECK(Core::DescribeRefusedDownload(url, 429).find("Try again in a while") != std::string::npos);
	CHECK(Core::DescribeRefusedDownload(url, 403).find("refused the download (HTTP 403)") != std::string::npos);
	CHECK(Core::DescribeRefusedDownload(url, 302).find("answered HTTP 302") != std::string::npos);
}
