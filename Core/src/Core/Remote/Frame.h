#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// One message between Graphite and its WSL agent (ADR 0695): a JSON header and raw bytes.
	struct Frame
	{
		std::string Header;
		std::string Payload;
	};

	// Bounds on what a reader accepts, so a corrupt length cannot make it wait for gigabytes.
	inline constexpr std::uint32_t kMaxFrameHeaderBytes = 1u << 20;
	inline constexpr std::uint32_t kMaxFramePayloadBytes = 256u << 20;

	// u32 header length (little-endian), header, u32 payload length (little-endian), payload.
	std::string EncodeFrame(std::string_view header, std::string_view payload);

	// Takes bytes in whatever pieces a pipe hands them over and gives back whole frames.
	class FrameReader
	{
	public:
		void Feed(std::string_view bytes);

		// The next complete frame, once; empty until one is whole, and for good once failed.
		std::optional<Frame> Take();

		// A length past the bounds arrived: the stream cannot be trusted from there on.
		bool HasFailed() const { return m_Failed; }

	private:
		std::string m_Buffer;
		std::size_t m_Consumed = 0;
		bool m_Failed = false;
	};
}
