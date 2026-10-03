#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Core {

	class Uuid
	{
	public:
		static constexpr std::size_t kByteCount = 16;

		static Uuid Generate();

		std::string ToString() const;

		const std::array<std::uint8_t, kByteCount>& GetBytes() const
		{
			return m_Bytes;
		}

	private:
		std::array<std::uint8_t, kByteCount> m_Bytes{};
	};
}