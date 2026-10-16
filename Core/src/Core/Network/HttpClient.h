#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"
#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct HttpHeader
	{
		std::string Name;
		std::string Value;
	};

	struct HttpRequest
	{
		std::string Url;
		std::vector<HttpHeader> Headers;
	};

	struct HttpResponse
	{
		int Status = 0;
		std::string Body;
	};

	struct DownloadProgress
	{
		std::uint64_t ReceivedBytes = 0;
		std::optional<std::uint64_t> TotalBytes;
	};

	using DownloadObserver = std::function<void(const DownloadProgress&)>;

	inline constexpr std::size_t kLargestFetchedBody = 32 * 1024 * 1024;

	inline constexpr int kHttpStatusOk = 200;
	inline constexpr int kHttpStatusNotFound = 404;
	inline constexpr int kHttpStatusProxyAuthenticationRequired = 407;

	// What a request refused with this status reads as: NotFound for a 404, which a caller may tell apart, and an IO
	// failure for any other.
	constexpr ErrorCode ErrorCodeOfHttpStatus(int status)
	{
		return status == kHttpStatusNotFound ? ErrorCode::NotFound : ErrorCode::IoFailure;
	}

	// HTTPS only: any other scheme is refused before anything is sent. One client may serve several threads at once.
	class HttpClient
	{
	public:
		HttpClient() = default;
		HttpClient(const HttpClient&) = delete;
		HttpClient& operator=(const HttpClient&) = delete;
		virtual ~HttpClient() = default;

		// Any status is an answer, so a 404 comes back as a response; only failing to get one is an error.
		virtual Result<HttpResponse> Fetch(const HttpRequest& request) = 0;

		// A status other than 200 is an error, and so is a cancel (Cancelled); on every error the target is removed.
		// The observer runs on the calling thread after each piece is written.
		virtual Result<void> Download(const HttpRequest& request, const std::filesystem::path& target,
									  const DownloadObserver& observer, const CancellationToken& token) = 0;
	};

	// Unsupported on a platform that has no backend yet.
	Result<Scope<HttpClient>> CreateHttpClient(std::string_view userAgent);

	// Fetch hands a 407 back as an answer; word it through this, as Download does, so the user looks at the proxy.
	Error ProxyCredentialsError(std::string_view url);
}
