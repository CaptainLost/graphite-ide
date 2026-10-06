#pragma once

#include "Core/Terminal/TerminalScreen.h"
#include "Core/Text/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	// A state machine rather than a ladder of conditionals, so that an unrecognised sequence is
	// consumed in full and its tail never lands on the screen as text. Its state survives from
	// one call to Consume to the next, because a read from a pipe can end in the middle of an
	// escape sequence or of a UTF-8 character.
	class VtParser
	{
	public:
		explicit VtParser(TerminalScreen& screen) : m_Screen(screen) {}

		// Some sequences are questions, such as where the cursor is, and the program that asks
		// one waits for the answer: a caller must write the returned bytes back to it.
		std::string Consume(std::string_view bytes);

	private:
		enum class State : std::uint8_t
		{
			Ground,
			Escape,
			EscapeIntermediate,
			ControlSequenceEntry,
			ControlSequenceParameter,
			ControlSequenceIntermediate,
			ControlSequenceIgnore,
			OperatingSystemCommandString,
			OperatingSystemCommandEscape,
			StringSink
		};

		enum class ModeRequest : std::uint8_t
		{
			TurnOn,
			TurnOff
		};

		static constexpr char kNoControlSequencePrefix = '\0';

		void ConsumeByte(char byte, std::string& replies);
		void PrintByte(char byte);
		void HandleControl(char byte);
		void HandleEscape(char byte);
		void HandleControlSequence(char finalByte, std::string& replies);
		void HandleOperatingSystemCommand();
		void HandleGraphicRendition();
		void HandleMode(ModeRequest request);
		void Reset();

		bool IsInStringState() const;
		void CollectIntermediate(char byte);
		void OpenFirstParameterIfNone();

		std::uint16_t ParameterOr(std::size_t parameterIndex, std::uint16_t fallback) const;

		struct ExtendedTerminalColor
		{
			TerminalColor Value;
			std::size_t LastParameterIndex;
		};

		std::optional<ExtendedTerminalColor> ReadExtendedTerminalColor(std::size_t codeIndex) const;

		TerminalScreen& m_Screen;
		State m_State = State::Ground;
		std::vector<std::uint16_t> m_Parameters;
		bool m_ParametersFull = false;
		std::string m_IntermediateBytes;
		std::string m_OperatingSystemCommandPayload;
		char m_ControlSequencePrefix = kNoControlSequencePrefix;
		Utf8Decoder m_Utf8Decoder;
	};
}
