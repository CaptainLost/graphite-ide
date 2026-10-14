#pragma once

#include "Editor/Overlays/AnalysisCache.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::uint32_t kAnalysisRecordVersion = 1;

	inline constexpr std::string_view kAnalysisMagic = "GRAF";

	inline constexpr int kBitsPerByte = 8;
	inline constexpr int kUint32Bits = 32;
	inline constexpr int kUint64Bits = 64;
	inline constexpr std::size_t kUint32Bytes = 4;
	inline constexpr std::size_t kUint64Bytes = 8;
	inline constexpr std::uint32_t kLowestByteMask = 0xFF;

	void AppendUint32(std::string& bytes, std::uint32_t value);
	void AppendUint64(std::string& bytes, std::uint64_t value);
	void AppendString(std::string& bytes, std::string_view text);

	std::uint64_t FileTimeToTicks(std::filesystem::file_time_type time);
	std::filesystem::file_time_type TicksToFileTime(std::uint64_t ticks);

	class Reader
	{
	public:
		explicit Reader(std::string_view bytes) : m_Bytes(bytes) {}

		bool HasFailed() const { return m_Failed; }

		std::uint32_t TakeUint32()
		{
			std::uint32_t value = 0;

			if (!EnsureRemaining(kUint32Bytes))
			{
				return 0;
			}

			for (int shift = 0; shift < kUint32Bits; shift += kBitsPerByte)
			{
				const unsigned char byte = static_cast<unsigned char>(m_Bytes[m_Position++]);

				value |= static_cast<std::uint32_t>(byte) << shift;
			}

			return value;
		}

		std::uint64_t TakeUint64()
		{
			std::uint64_t value = 0;

			if (!EnsureRemaining(kUint64Bytes))
			{
				return 0;
			}

			for (int shift = 0; shift < kUint64Bits; shift += kBitsPerByte)
			{
				const unsigned char byte = static_cast<unsigned char>(m_Bytes[m_Position++]);

				value |= static_cast<std::uint64_t>(byte) << shift;
			}

			return value;
		}

		std::string TakeString()
		{
			const std::uint32_t length = TakeUint32();

			if (!EnsureRemaining(length))
			{
				return {};
			}

			std::string text{m_Bytes.substr(m_Position, length)};
			m_Position += length;

			return text;
		}

		bool TakeMagic(std::string_view magic)
		{
			if (!EnsureRemaining(magic.size()))
			{
				return false;
			}

			const bool matches = m_Bytes.compare(m_Position, magic.size(), magic) == 0;
			m_Position += magic.size();

			return matches;
		}

		bool AtEnd() const { return !m_Failed && m_Position == m_Bytes.size(); }

	private:
		bool EnsureRemaining(std::size_t count)
		{
			if (m_Failed || m_Bytes.size() - m_Position < count)
			{
				m_Failed = true;
				return false;
			}

			return true;
		}

		std::string_view m_Bytes;
		std::size_t m_Position = 0;
		bool m_Failed = false;
	};

	std::string EncodeAnalysis(const std::filesystem::path& path, const CachedAnalysis& analysis);

	std::optional<CachedAnalysis> DecodeAnalysis(std::string_view bytes,
												 const std::filesystem::path& expectedPath);
}
