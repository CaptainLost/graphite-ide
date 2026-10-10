#include "corepch.h"

#include "Core/Platform/Hash.h"

#include "Core/FileSystem/FileSystem.h"

#include <fstream>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <bcrypt.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {

		constexpr std::size_t kFilePieceBytes = 1024 * 1024;
		constexpr std::size_t kLargestHashCallBytes = 1024 * 1024 * 1024;

		LPCWSTR ProviderNameOf(HashAlgorithm algorithm)
		{
			switch (algorithm)
			{
			case HashAlgorithm::Sha256:
				return BCRYPT_SHA256_ALGORITHM;
			case HashAlgorithm::Sha512:
				break;
			}

			return BCRYPT_SHA512_ALGORITHM;
		}

		std::size_t DigestBytesOf(HashAlgorithm algorithm)
		{
			constexpr std::size_t kSha256Bytes = 32;
			constexpr std::size_t kSha512Bytes = 64;

			switch (algorithm)
			{
			case HashAlgorithm::Sha256:
				return kSha256Bytes;
			case HashAlgorithm::Sha512:
				break;
			}

			return kSha512Bytes;
		}

		Error HashFailure(std::string_view step, NTSTATUS status)
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot hash: {} failed with status 0x{:08X}", step,
												static_cast<std::uint32_t>(status))};
		}

		// The hash object is destroyed before the provider it came from, on every path out.
		class HashSession
		{
		public:
			HashSession() = default;
			HashSession(const HashSession&) = delete;
			HashSession& operator=(const HashSession&) = delete;

			~HashSession()
			{
				if (m_Hash != nullptr)
				{
					::BCryptDestroyHash(m_Hash);
				}

				if (m_Provider != nullptr)
				{
					::BCryptCloseAlgorithmProvider(m_Provider, 0);
				}
			}

			Result<void> Begin(HashAlgorithm algorithm)
			{
				const NTSTATUS opened = ::BCryptOpenAlgorithmProvider(&m_Provider, ProviderNameOf(algorithm), nullptr, 0);

				if (!BCRYPT_SUCCESS(opened))
				{
					return HashFailure("BCryptOpenAlgorithmProvider", opened);
				}

				const NTSTATUS created = ::BCryptCreateHash(m_Provider, &m_Hash, nullptr, 0, nullptr, 0, 0);

				if (!BCRYPT_SUCCESS(created))
				{
					return HashFailure("BCryptCreateHash", created);
				}

				m_DigestBytes = DigestBytesOf(algorithm);
				return Result<void>::Ok();
			}

			// BCryptHashData takes a ULONG count and a non-const pointer it never writes through.
			Result<void> Add(std::string_view bytes)
			{
				while (!bytes.empty())
				{
					const std::string_view piece = bytes.substr(0, kLargestHashCallBytes);
					PUCHAR pieceStart = reinterpret_cast<PUCHAR>(const_cast<char*>(piece.data()));
					const NTSTATUS hashed = ::BCryptHashData(m_Hash, pieceStart, static_cast<ULONG>(piece.size()), 0);

					if (!BCRYPT_SUCCESS(hashed))
					{
						return HashFailure("BCryptHashData", hashed);
					}

					bytes.remove_prefix(piece.size());
				}

				return Result<void>::Ok();
			}

			Result<std::vector<std::uint8_t>> Finish()
			{
				std::vector<std::uint8_t> digest(m_DigestBytes);
				const NTSTATUS finished = ::BCryptFinishHash(m_Hash, digest.data(), static_cast<ULONG>(digest.size()), 0);

				if (!BCRYPT_SUCCESS(finished))
				{
					return HashFailure("BCryptFinishHash", finished);
				}

				return digest;
			}

		private:
			BCRYPT_ALG_HANDLE m_Provider = nullptr;
			BCRYPT_HASH_HANDLE m_Hash = nullptr;
			std::size_t m_DigestBytes = 0;
		};
	}
#endif

	Result<std::vector<std::uint8_t>> HashBytes([[maybe_unused]] HashAlgorithm algorithm,
												[[maybe_unused]] std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		HashSession session;

		if (const Result<void> begun = session.Begin(algorithm); !begun)
		{
			return begun.GetError();
		}

		if (const Result<void> added = session.Add(bytes); !added)
		{
			return added.GetError();
		}

		return session.Finish();
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "hashing is not built for this platform yet"};
#endif
	}

	Result<std::vector<std::uint8_t>> HashFile([[maybe_unused]] HashAlgorithm algorithm,
											   [[maybe_unused]] const std::filesystem::path& file)
	{
#ifdef CORE_PLATFORM_WINDOWS
		std::ifstream input{file, std::ios::binary};

		if (!input.is_open())
		{
			const ErrorCode code = PathExists(file) ? ErrorCode::IoFailure : ErrorCode::NotFound;
			return Error{.Code = code, .Message = std::format("cannot open '{}' to hash it", ToUtf8(file))};
		}

		HashSession session;

		if (const Result<void> begun = session.Begin(algorithm); !begun)
		{
			return begun.GetError();
		}

		std::vector<char> piece(kFilePieceBytes);

		while (input)
		{
			input.read(piece.data(), static_cast<std::streamsize>(piece.size()));
			const std::size_t readBytes = static_cast<std::size_t>(input.gcount());

			if (const Result<void> added = session.Add(std::string_view{piece.data(), readBytes}); !added)
			{
				return added.GetError();
			}
		}

		if (input.bad())
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot read '{}' to hash it", ToUtf8(file))};
		}

		return session.Finish();
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "hashing is not built for this platform yet"};
#endif
	}
}
