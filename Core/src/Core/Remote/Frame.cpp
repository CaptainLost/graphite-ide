#include "corepch.h"

#include "Core/Remote/Frame.h"

namespace Core {

	namespace {

		constexpr std::size_t kLengthBytes = 4;

		void AppendLength(std::string& out, std::uint32_t length)
		{
			for (std::size_t shift = 0; shift < kLengthBytes; ++shift)
			{
				out.push_back(static_cast<char>((length >> (shift * 8)) & 0xFFu));
			}
		}

		std::uint32_t ReadLength(std::string_view bytes)
		{
			std::uint32_t length = 0;

			for (std::size_t shift = 0; shift < kLengthBytes; ++shift)
			{
				length |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[shift])) << (shift * 8);
			}

			return length;
		}
	}

	std::string EncodeFrame(std::string_view header, std::string_view payload)
	{
		std::string frame;
		frame.reserve(2 * kLengthBytes + header.size() + payload.size());

		AppendLength(frame, static_cast<std::uint32_t>(header.size()));
		frame.append(header);
		AppendLength(frame, static_cast<std::uint32_t>(payload.size()));
		frame.append(payload);

		return frame;
	}

	void FrameReader::Feed(std::string_view bytes)
	{
		if (m_Failed)
		{
			return;
		}

		// What earlier frames used is dropped before the buffer grows, so it never holds more
		// than one frame's worth of history.
		if (m_Consumed > 0)
		{
			m_Buffer.erase(0, m_Consumed);
			m_Consumed = 0;
		}

		m_Buffer.append(bytes);
	}

	std::optional<Frame> FrameReader::Take()
	{
		if (m_Failed)
		{
			return std::nullopt;
		}

		const std::string_view pending = std::string_view{m_Buffer}.substr(m_Consumed);

		if (pending.size() < kLengthBytes)
		{
			return std::nullopt;
		}

		const std::uint32_t headerLength = ReadLength(pending);

		if (headerLength > kMaxFrameHeaderBytes)
		{
			m_Failed = true;
			return std::nullopt;
		}

		if (pending.size() < kLengthBytes + headerLength + kLengthBytes)
		{
			return std::nullopt;
		}

		const std::uint32_t payloadLength = ReadLength(pending.substr(kLengthBytes + headerLength));

		if (payloadLength > kMaxFramePayloadBytes)
		{
			m_Failed = true;
			return std::nullopt;
		}

		const std::size_t frameLength = 2 * kLengthBytes + headerLength + payloadLength;

		if (pending.size() < frameLength)
		{
			return std::nullopt;
		}

		Frame frame{
			.Header = std::string{pending.substr(kLengthBytes, headerLength)},
			.Payload = std::string{pending.substr(2 * kLengthBytes + headerLength, payloadLength)},
		};

		m_Consumed += frameLength;

		return frame;
	}
}
