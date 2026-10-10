#pragma once

#include "Core/Network/HttpClient.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <system_error>
#include <vector>

// Answers from a table instead of the network. A URL with no answer fails as an unreachable host would. Not for use
// from two threads at once.
class FakeHttpClient final : public Core::HttpClient
{
public:
	static constexpr std::size_t kPieceBytes = 1024;

	void Answer(const std::string& url, int status, std::string body)
	{
		m_Answers[url] = Core::HttpResponse{.Status = status, .Body = std::move(body)};
	}

	Core::Result<Core::HttpResponse> Fetch(const Core::HttpRequest& request) override
	{
		Requests.push_back(request);
		const std::map<std::string, Core::HttpResponse>::const_iterator answer = m_Answers.find(request.Url);

		if (answer == m_Answers.end())
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = "no answer for " + request.Url};
		}

		return answer->second;
	}

	Core::Result<void> Download(const Core::HttpRequest& request, const std::filesystem::path& target,
								const Core::DownloadObserver& observer, const Core::CancellationToken& token) override
	{
		Requests.push_back(request);
		const Core::Result<void> received = Receive(request, target, observer, token);

		if (!received)
		{
			std::error_code ignoredError;
			std::filesystem::remove(target, ignoredError);
		}

		return received;
	}

	// Called after each piece is written, with the bytes received so far.
	std::function<void(std::uint64_t receivedBytes)> AfterPiece;

	std::vector<Core::HttpRequest> Requests;

private:
	Core::Result<void> Receive(const Core::HttpRequest& request, const std::filesystem::path& target,
							   const Core::DownloadObserver& observer, const Core::CancellationToken& token)
	{
		const std::map<std::string, Core::HttpResponse>::const_iterator answer = m_Answers.find(request.Url);

		if (answer == m_Answers.end())
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = "no answer for " + request.Url};
		}

		if (answer->second.Status != 200)
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure,
							   .Message = std::format("'{}' answered {}", request.Url, answer->second.Status)};
		}

		std::ofstream output{target, std::ios::binary | std::ios::trunc};
		const std::string& body = answer->second.Body;
		Core::DownloadProgress progress{.ReceivedBytes = 0, .TotalBytes = body.size()};

		for (std::size_t pieceStart = 0; pieceStart < body.size(); pieceStart += kPieceBytes)
		{
			if (token.IsCancelled())
			{
				return Core::Error{.Code = Core::ErrorCode::Cancelled, .Message = "the download was cancelled"};
			}

			const std::size_t pieceBytes = std::min(kPieceBytes, body.size() - pieceStart);
			output.write(body.data() + pieceStart, static_cast<std::streamsize>(pieceBytes));
			progress.ReceivedBytes += pieceBytes;

			if (observer)
			{
				observer(progress);
			}

			if (AfterPiece)
			{
				AfterPiece(progress.ReceivedBytes);
			}
		}

		if (token.IsCancelled())
		{
			return Core::Error{.Code = Core::ErrorCode::Cancelled, .Message = "the download was cancelled"};
		}

		return Core::Result<void>::Ok();
	}

	std::map<std::string, Core::HttpResponse> m_Answers;
};
