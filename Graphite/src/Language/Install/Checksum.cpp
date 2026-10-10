#include "Language/Install/Checksum.h"

#include "Core/Base/ByteText.h"

#include <format>

namespace Graphite {

	namespace {

		constexpr std::string_view kSha256Prefix = "sha256:";
		constexpr std::string_view kSha512Prefix = "sha512:";

		std::string_view PrefixOf(Core::HashAlgorithm algorithm)
		{
			switch (algorithm)
			{
			case Core::HashAlgorithm::Sha256:
				return kSha256Prefix;
			case Core::HashAlgorithm::Sha512:
				break;
			}

			return kSha512Prefix;
		}

		std::size_t DigestBytesOf(Core::HashAlgorithm algorithm)
		{
			constexpr std::size_t kSha256Bytes = 32;
			constexpr std::size_t kSha512Bytes = 64;

			switch (algorithm)
			{
			case Core::HashAlgorithm::Sha256:
				return kSha256Bytes;
			case Core::HashAlgorithm::Sha512:
				break;
			}

			return kSha512Bytes;
		}
	}

	std::string DescribeChecksum(const Checksum& checksum)
	{
		return std::string{PrefixOf(checksum.Algorithm)} + Core::BytesToHex(checksum.Digest);
	}

	std::optional<Checksum> ParseChecksum(std::string_view text)
	{
		Core::HashAlgorithm algorithm = Core::HashAlgorithm::Sha256;

		if (text.starts_with(kSha512Prefix))
		{
			algorithm = Core::HashAlgorithm::Sha512;
		}
		else if (!text.starts_with(kSha256Prefix))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::uint8_t>> digest = Core::BytesFromHex(text.substr(PrefixOf(algorithm).size()));

		if (!digest || digest->size() != DigestBytesOf(algorithm))
		{
			return std::nullopt;
		}

		return Checksum{.Algorithm = algorithm, .Digest = std::move(*digest)};
	}

	Core::Result<void> VerifyChecksum(const std::filesystem::path& file, const Checksum& expected)
	{
		Core::Result<std::vector<std::uint8_t>> actual = Core::HashFile(expected.Algorithm, file);

		if (!actual)
		{
			return actual.GetError();
		}

		if (actual.GetValue() != expected.Digest)
		{
			const Checksum found{.Algorithm = expected.Algorithm, .Digest = std::move(actual).GetValue()};
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
							   .Message = std::format("the download's checksum does not match: expected {}, got {}",
													  DescribeChecksum(expected), DescribeChecksum(found))};
		}

		return Core::Result<void>::Ok();
	}
}
