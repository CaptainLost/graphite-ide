#include "corepch.h"

#include "Core/Network/HttpClient.h"

#include "Core/FileSystem/FileSystem.h"

#include <fstream>
#include <system_error>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <winhttp.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {

		constexpr DWORD kReadPieceBytes = 64 * 1024;
		constexpr DWORD kStageTimeoutMilliseconds = 30 * 1000;
		constexpr int kStatusOk = 200;
		constexpr int kStatusNotFound = 404;

		class InternetHandle
		{
		public:
			InternetHandle() = default;
			explicit InternetHandle(HINTERNET handle) : m_Handle(handle) {}
			InternetHandle(const InternetHandle&) = delete;
			InternetHandle& operator=(const InternetHandle&) = delete;
			InternetHandle(InternetHandle&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)) {}

			InternetHandle& operator=(InternetHandle&& other) noexcept
			{
				if (this != &other)
				{
					Close();
					m_Handle = std::exchange(other.m_Handle, nullptr);
				}

				return *this;
			}

			~InternetHandle() { Close(); }

			HINTERNET Get() const { return m_Handle; }

			explicit operator bool() const { return m_Handle != nullptr; }

		private:
			void Close()
			{
				if (m_Handle != nullptr)
				{
					::WinHttpCloseHandle(std::exchange(m_Handle, nullptr));
				}
			}

			HINTERNET m_Handle = nullptr;
		};

		// The connection is closed after the request that uses it, which member order gives.
		struct OpenedRequest
		{
			InternetHandle Connection;
			InternetHandle Request;
		};

		struct UrlParts
		{
			std::wstring Host;
			INTERNET_PORT Port = INTERNET_DEFAULT_HTTPS_PORT;
			std::wstring PathAndQuery;
		};

		std::string_view DescribeWinHttpError(DWORD errorCode)
		{
			switch (errorCode)
			{
			case ERROR_WINHTTP_NAME_NOT_RESOLVED:
				return "the server name could not be resolved";
			case ERROR_WINHTTP_CANNOT_CONNECT:
				return "the connection could not be made";
			case ERROR_WINHTTP_TIMEOUT:
				return "the request timed out";
			case ERROR_WINHTTP_CONNECTION_ERROR:
				return "the connection was reset";
			case ERROR_WINHTTP_SECURE_FAILURE:
				return "the secure connection could not be established";
			case ERROR_WINHTTP_REDIRECT_FAILED:
				return "a redirect could not be followed";
			default:
				return "the request failed";
			}
		}

		Error RequestFailure(std::string_view url, DWORD errorCode)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot fetch '{}': {} (WinHTTP error {})", url,
												DescribeWinHttpError(errorCode), errorCode)};
		}

		Error CancelledDownload(std::string_view url)
		{
			return Error{.Code = ErrorCode::Cancelled, .Message = std::format("the download of '{}' was cancelled", url)};
		}

		Result<UrlParts> SplitHttpsUrl(std::string_view url)
		{
			const std::wstring wideUrl = ToUtf16(url);
			URL_COMPONENTS components{};
			components.dwStructSize = sizeof(components);

			// The parts point into wideUrl, so they must not outlive it.
			constexpr DWORD kPointIntoUrl = static_cast<DWORD>(-1);
			components.dwSchemeLength = kPointIntoUrl;
			components.dwHostNameLength = kPointIntoUrl;
			components.dwUrlPathLength = kPointIntoUrl;
			components.dwExtraInfoLength = kPointIntoUrl;

			if (::WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components) == FALSE)
			{
				return Error{.Code = ErrorCode::InvalidArgument, .Message = std::format("'{}' is not a URL", url)};
			}

			if (components.nScheme != INTERNET_SCHEME_HTTPS)
			{
				return Error{.Code = ErrorCode::InvalidArgument,
							 .Message = std::format("'{}' is not an https:// URL, and nothing else is fetched", url)};
			}

			std::wstring pathAndQuery{components.lpszUrlPath, components.dwUrlPathLength};
			pathAndQuery.append(components.lpszExtraInfo, components.dwExtraInfoLength);

			return UrlParts{.Host = std::wstring{components.lpszHostName, components.dwHostNameLength},
							.Port = components.nPort,
							.PathAndQuery = pathAndQuery.empty() ? std::wstring{L"/"} : pathAndQuery};
		}

		Result<OpenedRequest> SendRequest(HINTERNET session, const HttpRequest& request, bool decompress)
		{
			const Result<UrlParts> parts = SplitHttpsUrl(request.Url);

			if (!parts)
			{
				return parts.GetError();
			}

			InternetHandle connection{::WinHttpConnect(session, parts.GetValue().Host.c_str(), parts.GetValue().Port, 0)};

			if (!connection)
			{
				return RequestFailure(request.Url, ::GetLastError());
			}

			InternetHandle opened{::WinHttpOpenRequest(connection.Get(), L"GET", parts.GetValue().PathAndQuery.c_str(),
													   nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
													   WINHTTP_FLAG_SECURE)};

			if (!opened)
			{
				return RequestFailure(request.Url, ::GetLastError());
			}

			if (decompress)
			{
				DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;

				if (::WinHttpSetOption(opened.Get(), WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression)) == FALSE)
				{
					return RequestFailure(request.Url, ::GetLastError());
				}
			}

			for (const HttpHeader& header : request.Headers)
			{
				const std::wstring line = ToUtf16(header.Name + ": " + header.Value);

				if (::WinHttpAddRequestHeaders(opened.Get(), line.c_str(), static_cast<DWORD>(line.size()),
											   WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) == FALSE)
				{
					return RequestFailure(request.Url, ::GetLastError());
				}
			}

			if (::WinHttpSendRequest(opened.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE)
			{
				return RequestFailure(request.Url, ::GetLastError());
			}

			if (::WinHttpReceiveResponse(opened.Get(), nullptr) == FALSE)
			{
				return RequestFailure(request.Url, ::GetLastError());
			}

			return OpenedRequest{.Connection = std::move(connection), .Request = std::move(opened)};
		}

		Result<int> StatusOf(const OpenedRequest& opened, std::string_view url)
		{
			DWORD status = 0;
			DWORD statusBytes = sizeof(status);

			if (::WinHttpQueryHeaders(opened.Request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
									  WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX) == FALSE)
			{
				return RequestFailure(url, ::GetLastError());
			}

			return static_cast<int>(status);
		}

		std::optional<std::uint64_t> ContentLengthOf(const OpenedRequest& opened)
		{
			std::uint64_t length = 0;
			DWORD lengthBytes = sizeof(length);

			if (::WinHttpQueryHeaders(opened.Request.Get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
									  WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthBytes, WINHTTP_NO_HEADER_INDEX) == FALSE)
			{
				return std::nullopt;
			}

			return length;
		}

		Result<std::string> ReadWholeBody(const OpenedRequest& opened, std::string_view url)
		{
			std::string body;
			std::vector<char> piece(kReadPieceBytes);

			for (;;)
			{
				DWORD readBytes = 0;

				if (::WinHttpReadData(opened.Request.Get(), piece.data(), kReadPieceBytes, &readBytes) == FALSE)
				{
					return RequestFailure(url, ::GetLastError());
				}

				if (readBytes == 0)
				{
					return body;
				}

				if (body.size() + readBytes > kLargestFetchedBody)
				{
					return Error{.Code = ErrorCode::TooLarge,
								 .Message = std::format("the answer from '{}' is larger than {} bytes", url, kLargestFetchedBody)};
				}

				body.append(piece.data(), readBytes);
			}
		}

		Result<void> ReceiveIntoFile(const OpenedRequest& opened, const HttpRequest& request,
									 const std::filesystem::path& target, const DownloadObserver& observer,
									 const CancellationToken& token)
		{
			std::ofstream output{target, std::ios::binary | std::ios::trunc};

			if (!output.is_open())
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot write '{}'", ToUtf8(target))};
			}

			DownloadProgress progress{.ReceivedBytes = 0, .TotalBytes = ContentLengthOf(opened)};
			std::vector<char> piece(kReadPieceBytes);

			for (;;)
			{
				if (token.IsCancelled())
				{
					return CancelledDownload(request.Url);
				}

				DWORD readBytes = 0;

				if (::WinHttpReadData(opened.Request.Get(), piece.data(), kReadPieceBytes, &readBytes) == FALSE)
				{
					return RequestFailure(request.Url, ::GetLastError());
				}

				if (readBytes == 0)
				{
					break;
				}

				output.write(piece.data(), readBytes);

				if (!output.good())
				{
					return Error{.Code = ErrorCode::IoFailure,
								 .Message = std::format("cannot write '{}'; the disk may be full", ToUtf8(target))};
				}

				progress.ReceivedBytes += readBytes;

				if (observer)
				{
					observer(progress);
				}
			}

			const bool endedEarly = progress.TotalBytes.has_value() && progress.ReceivedBytes != *progress.TotalBytes;

			if (endedEarly)
			{
				return Error{.Code = ErrorCode::IoFailure,
							 .Message = std::format("the download of '{}' received {} bytes where {} were announced", request.Url,
													progress.ReceivedBytes, *progress.TotalBytes)};
			}

			output.close();

			if (output.fail())
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot write '{}'", ToUtf8(target))};
			}

			return Result<void>::Ok();
		}

		bool RemovePartialDownload(const std::filesystem::path& target)
		{
			std::error_code removalError;
			std::filesystem::remove(target, removalError);
			return !removalError;
		}

		class WinHttpClient final : public HttpClient
		{
		public:
			explicit WinHttpClient(InternetHandle session) : m_Session(std::move(session)) {}

			Result<HttpResponse> Fetch(const HttpRequest& request) override
			{
				constexpr bool kDecompress = true;
				const Result<OpenedRequest> opened = SendRequest(m_Session.Get(), request, kDecompress);

				if (!opened)
				{
					return opened.GetError();
				}

				const Result<int> status = StatusOf(opened.GetValue(), request.Url);

				if (!status)
				{
					return status.GetError();
				}

				Result<std::string> body = ReadWholeBody(opened.GetValue(), request.Url);

				if (!body)
				{
					return body.GetError();
				}

				return HttpResponse{.Status = status.GetValue(), .Body = std::move(body).GetValue()};
			}

			Result<void> Download(const HttpRequest& request, const std::filesystem::path& target,
								  const DownloadObserver& observer, const CancellationToken& token) override
			{
				const Result<void> received = DownloadInto(request, target, observer, token);

				if (!received && !RemovePartialDownload(target))
				{
					return Error{.Code = received.GetError().Code,
								 .Message = std::format("{}; the partial file '{}' could not be removed",
														received.GetError().Message, ToUtf8(target))};
				}

				return received;
			}

		private:
			// The output stream is closed by the time this returns, so a failure's partial file can be removed.
			Result<void> DownloadInto(const HttpRequest& request, const std::filesystem::path& target,
									  const DownloadObserver& observer, const CancellationToken& token)
			{
				if (token.IsCancelled())
				{
					return CancelledDownload(request.Url);
				}

				constexpr bool kDecompress = false;
				const Result<OpenedRequest> opened = SendRequest(m_Session.Get(), request, kDecompress);

				if (!opened)
				{
					return opened.GetError();
				}

				const Result<int> status = StatusOf(opened.GetValue(), request.Url);

				if (!status)
				{
					return status.GetError();
				}

				if (status.GetValue() != kStatusOk)
				{
					const ErrorCode code = status.GetValue() == kStatusNotFound ? ErrorCode::NotFound : ErrorCode::IoFailure;
					return Error{.Code = code, .Message = std::format("'{}' answered {}", request.Url, status.GetValue())};
				}

				return ReceiveIntoFile(opened.GetValue(), request, target, observer, token);
			}

			InternetHandle m_Session;
		};
	}
#endif

	Result<Scope<HttpClient>> CreateHttpClient([[maybe_unused]] std::string_view userAgent)
	{
#ifdef CORE_PLATFORM_WINDOWS
		InternetHandle session{::WinHttpOpen(ToUtf16(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
											 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};

		if (!session)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot open an HTTP session (error {})", ::GetLastError())};
		}

		if (::WinHttpSetTimeouts(session.Get(), kStageTimeoutMilliseconds, kStageTimeoutMilliseconds,
								 kStageTimeoutMilliseconds, kStageTimeoutMilliseconds) == FALSE)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot set the HTTP timeouts (error {})", ::GetLastError())};
		}

		return Scope<HttpClient>{CreateScope<WinHttpClient>(std::move(session))};
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "HTTP is not built for this platform yet"};
#endif
	}
}
