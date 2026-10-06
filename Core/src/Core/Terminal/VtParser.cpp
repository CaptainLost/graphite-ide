#include "corepch.h"

#include "Core/Terminal/VtParser.h"

#include "Core/Base/Assert.h"
#include "Core/Terminal/CharacterWidth.h"
#include "Core/Terminal/ControlCharacters.h"

#include <algorithm>

namespace Core {

	namespace {
		// Bytes from an arbitrary program enter the process here, so no buffer may grow with the
		// stream: a run of ';' or an OSC that never terminates would otherwise reallocate until
		// memory runs out. Past each cap the excess is dropped; real terminals cap at about these
		// sizes, and no sequence a program emits comes near them.
		constexpr std::size_t kMostParameters = 32;
		constexpr std::size_t kLongestOperatingSystemCommandPayload = 4096;
		constexpr std::size_t kMostIntermediateBytes = 8;

		constexpr std::uint16_t kOmittedParameter = 0;
		constexpr int kLargestParameter = 65535;
		constexpr int kDecimalBase = 10;

		constexpr std::size_t kFirstParameterIndex = 0;
		constexpr std::size_t kSecondParameterIndex = 1;
		constexpr std::uint16_t kDefaultCount = 1;
		constexpr std::uint16_t kDefaultPosition = 1;

		constexpr unsigned char kFirstByteAfterC0Controls = 0x20;

		constexpr char kParameterSeparator = ';';
		constexpr char kSubParameterSeparator = ':';
		constexpr char kDecPrivatePrefix = '?';
		constexpr char kSecondaryDeviceAttributesPrefix = '>';

		constexpr char kEscapeOpensControlSequence = '[';
		constexpr char kEscapeOpensOperatingSystemCommand = ']';
		constexpr char kEscapeOpensDeviceControlString = 'P';
		constexpr char kEscapeOpensPrivacyMessage = '^';
		constexpr char kEscapeOpensApplicationProgramCommand = '_';
		constexpr char kStringTerminatorAfterEscape = '\\';

		constexpr char kEscapeSaveCursor = '7';
		constexpr char kEscapeRestoreCursor = '8';
		constexpr char kEscapeIndex = 'D';
		constexpr char kEscapeNextLine = 'E';
		constexpr char kEscapeReverseIndex = 'M';
		constexpr char kEscapeFullReset = 'c';

		constexpr char kControlSequenceCursorUp = 'A';
		constexpr char kControlSequenceCursorDown = 'B';
		constexpr char kControlSequenceCursorForward = 'C';
		constexpr char kControlSequenceCursorBack = 'D';
		constexpr char kControlSequenceCursorColumn = 'G';
		constexpr char kControlSequenceCursorRow = 'd';
		constexpr char kControlSequenceCursorPosition = 'H';
		constexpr char kControlSequenceHorizontalVerticalPosition = 'f';
		constexpr char kControlSequenceEraseInDisplay = 'J';
		constexpr char kControlSequenceEraseInLine = 'K';
		constexpr char kControlSequenceInsertLines = 'L';
		constexpr char kControlSequenceDeleteLines = 'M';
		constexpr char kControlSequenceInsertCharacters = '@';
		constexpr char kControlSequenceDeleteCharacters = 'P';
		constexpr char kControlSequenceEraseCharacters = 'X';
		constexpr char kControlSequenceScrollUp = 'S';
		constexpr char kControlSequenceScrollDown = 'T';
		constexpr char kControlSequenceSetScrollRegion = 'r';
		constexpr char kControlSequenceSaveCursor = 's';
		constexpr char kControlSequenceRestoreCursor = 'u';
		constexpr char kControlSequenceSelectGraphicRendition = 'm';
		constexpr char kControlSequenceSetMode = 'h';
		constexpr char kControlSequenceResetMode = 'l';
		constexpr char kControlSequenceDeviceAttributes = 'c';
		constexpr char kControlSequenceDeviceStatusReport = 'n';
		constexpr char kControlSequenceCursorPositionReport = 'R';

		constexpr std::uint16_t kCursorPositionRequest = 6;

		constexpr std::string_view kSecondaryDeviceAttributesReply = "\x1B[>0;10;1c";
		// Every emulator gives this answer and every program accepts it.
		constexpr std::string_view kVt100WithAdvancedVideoAttributes = "\x1B[?1;2c";

		constexpr std::uint16_t kEraseToEndParameter = 0;
		constexpr std::uint16_t kEraseToStartParameter = 1;

		constexpr std::string_view kOperatingSystemCommandSetIconNameAndWindowTitle = "0";
		constexpr std::string_view kOperatingSystemCommandSetWindowTitle = "2";

		constexpr std::uint16_t kGraphicRenditionReset = 0;
		constexpr std::uint16_t kGraphicRenditionBold = 1;
		constexpr std::uint16_t kGraphicRenditionFaint = 2;
		constexpr std::uint16_t kGraphicRenditionItalic = 3;
		constexpr std::uint16_t kGraphicRenditionUnderline = 4;
		constexpr std::uint16_t kGraphicRenditionInverse = 7;
		constexpr std::uint16_t kGraphicRenditionHidden = 8;
		constexpr std::uint16_t kGraphicRenditionNormalIntensity = 22;
		constexpr std::uint16_t kGraphicRenditionNotItalic = 23;
		constexpr std::uint16_t kGraphicRenditionNotUnderlined = 24;
		constexpr std::uint16_t kGraphicRenditionNotInverse = 27;
		constexpr std::uint16_t kGraphicRenditionNotHidden = 28;
		constexpr std::uint16_t kGraphicRenditionFirstForeground = 30;
		constexpr std::uint16_t kGraphicRenditionLastForeground = 37;
		constexpr std::uint16_t kGraphicRenditionExtendedForeground = 38;
		constexpr std::uint16_t kGraphicRenditionDefaultForeground = 39;
		constexpr std::uint16_t kGraphicRenditionFirstBackground = 40;
		constexpr std::uint16_t kGraphicRenditionLastBackground = 47;
		constexpr std::uint16_t kGraphicRenditionExtendedBackground = 48;
		constexpr std::uint16_t kGraphicRenditionDefaultBackground = 49;
		constexpr std::uint16_t kGraphicRenditionFirstBrightForeground = 90;
		constexpr std::uint16_t kGraphicRenditionLastBrightForeground = 97;
		constexpr std::uint16_t kGraphicRenditionFirstBrightBackground = 100;
		constexpr std::uint16_t kGraphicRenditionLastBrightBackground = 107;

		constexpr std::uint16_t kExtendedPaletteForm = 5;
		constexpr std::uint16_t kExtendedRgbForm = 2;
		constexpr std::uint8_t kFirstBrightPaletteIndex = 8;

		constexpr std::uint16_t kAutoWrapMode = 7;
		constexpr std::uint16_t kCursorVisibleMode = 25;
		constexpr std::uint16_t kMouseClicksMode = 1000;
		constexpr std::uint16_t kMouseDragsMode = 1002;
		constexpr std::uint16_t kMouseMotionMode = 1003;
		constexpr std::uint16_t kSgrMouseMode = 1006;
		constexpr std::uint16_t kAlternateBufferMode = 1049;
		constexpr std::uint16_t kBracketedPasteMode = 2004;

		constexpr bool kAlternateBufferAtPowerOn = false;
		constexpr bool kAutoWrapAtPowerOn = true;
		constexpr bool kBracketedPasteAtPowerOn = false;
		constexpr bool kMouseModeAtPowerOn = false;
		constexpr bool kSgrMouseAtPowerOn = false;
		constexpr bool kCursorVisibleAtPowerOn = true;
		constexpr std::uint16_t kHomeRow = 0;
		constexpr std::uint16_t kHomeColumn = 0;

		constexpr std::uint8_t kReplacementCharacterWidth = kNarrowCharacterCells;
		constexpr int kNoMovement = 0;

		bool IsControlByte(char byte)
		{
			return static_cast<unsigned char>(byte) < kFirstByteAfterC0Controls;
		}

		bool IsIntermediateByte(char byte)
		{
			return byte >= ' ' && byte <= '/';
		}

		bool IsDecimalDigit(char byte)
		{
			return byte >= '0' && byte <= '9';
		}

		bool IsParameterByte(char byte)
		{
			return byte >= '0' && byte <= '?';
		}

		bool IsControlSequenceFinal(char byte)
		{
			return byte >= '@' && byte <= '~';
		}

		bool IsControlSequencePrefix(char byte)
		{
			return byte >= '<' && byte <= '?';
		}

		bool OpensDiscardedString(char byte)
		{
			return byte == kEscapeOpensDeviceControlString || byte == kEscapeOpensPrivacyMessage || byte == kEscapeOpensApplicationProgramCommand;
		}

		std::uint16_t ZeroBased(std::uint16_t oneBased)
		{
			CORE_ASSERT(oneBased > 0, "A one-based coordinate is never zero");
			return static_cast<std::uint16_t>(oneBased - 1);
		}

		int OneBased(std::uint16_t zeroBased)
		{
			return zeroBased + 1;
		}

		EraseExtent ToEraseExtent(std::uint16_t parameter)
		{
			if (parameter == kEraseToEndParameter)
			{
				return EraseExtent::ToEnd;
			}

			if (parameter == kEraseToStartParameter)
			{
				return EraseExtent::ToStart;
			}

			return EraseExtent::All;
		}
	}

	std::string VtParser::Consume(std::string_view bytes)
	{
		std::string replies;

		for (const char byte : bytes)
		{
			ConsumeByte(byte, replies);
		}

		return replies;
	}

	void VtParser::Reset()
	{
		m_State = State::Ground;
		m_Parameters.clear();
		m_ParametersFull = false;
		m_IntermediateBytes.clear();
		m_OperatingSystemCommandPayload.clear();
		m_ControlSequencePrefix = kNoControlSequencePrefix;
	}

	bool VtParser::IsInStringState() const
	{
		return m_State == State::OperatingSystemCommandString || m_State == State::OperatingSystemCommandEscape || m_State == State::StringSink;
	}

	void VtParser::CollectIntermediate(char byte)
	{
		if (m_IntermediateBytes.size() < kMostIntermediateBytes)
		{
			m_IntermediateBytes.push_back(byte);
		}
	}

	void VtParser::OpenFirstParameterIfNone()
	{
		if (m_Parameters.empty())
		{
			m_Parameters.push_back(kOmittedParameter);
		}
	}

	std::uint16_t VtParser::ParameterOr(std::size_t parameterIndex, std::uint16_t fallback) const
	{
		const bool isOmitted = parameterIndex >= m_Parameters.size() || m_Parameters[parameterIndex] == kOmittedParameter;

		if (isOmitted)
		{
			return fallback;
		}

		return m_Parameters[parameterIndex];
	}

	std::optional<VtParser::ExtendedTerminalColor> VtParser::ReadExtendedTerminalColor(std::size_t codeIndex) const
	{
		const std::size_t formParameterIndex = codeIndex + 1;
		const std::size_t paletteParameterIndex = codeIndex + 2;
		const std::size_t redParameterIndex = codeIndex + 2;
		const std::size_t greenParameterIndex = codeIndex + 3;
		const std::size_t blueParameterIndex = codeIndex + 4;
		const std::uint16_t extendedForm = ParameterOr(formParameterIndex, kOmittedParameter);
		const bool hasPaletteIndex = extendedForm == kExtendedPaletteForm && paletteParameterIndex < m_Parameters.size();
		const bool hasRgbComponents = extendedForm == kExtendedRgbForm && blueParameterIndex < m_Parameters.size();

		if (hasPaletteIndex)
		{
			return ExtendedTerminalColor{
				.Value = TerminalColor::FromPalette(static_cast<std::uint8_t>(m_Parameters[paletteParameterIndex])),
				.LastParameterIndex = paletteParameterIndex};
		}

		if (hasRgbComponents)
		{
			return ExtendedTerminalColor{
				.Value = TerminalColor::FromRgb(static_cast<std::uint8_t>(m_Parameters[redParameterIndex]),
												static_cast<std::uint8_t>(m_Parameters[greenParameterIndex]),
												static_cast<std::uint8_t>(m_Parameters[blueParameterIndex])),
				.LastParameterIndex = blueParameterIndex};
		}

		return std::nullopt;
	}

	void VtParser::ConsumeByte(char byte, std::string& replies)
	{
		// An ESC is not left to the state's own reset: that would swallow it, and the body of the
		// sequence it opens would land on the screen as text. A string state handles its ESC
		// itself, because there it may be the first half of the terminator ESC \ instead.
		if (byte == kEscapeCharacter && !IsInStringState())
		{
			Reset();
			m_State = State::Escape;
			return;
		}

		switch (m_State)
		{
		case State::Ground:
		{
			if (IsControlByte(byte))
			{
				HandleControl(byte);
				return;
			}

			if (byte == kDeleteCharacter)
			{
				return;
			}

			PrintByte(byte);
			return;
		}
		case State::Escape:
		{
			if (byte == kEscapeOpensControlSequence)
			{
				m_State = State::ControlSequenceEntry;
				return;
			}

			if (byte == kEscapeOpensOperatingSystemCommand)
			{
				m_State = State::OperatingSystemCommandString;
				return;
			}

			if (OpensDiscardedString(byte))
			{
				m_State = State::StringSink;
				return;
			}

			if (IsIntermediateByte(byte))
			{
				CollectIntermediate(byte);
				m_State = State::EscapeIntermediate;
				return;
			}

			HandleEscape(byte);
			Reset();
			return;
		}
		case State::EscapeIntermediate:
		{
			if (IsIntermediateByte(byte))
			{
				CollectIntermediate(byte);
				return;
			}

			Reset();
			return;
		}
		case State::ControlSequenceEntry:
		case State::ControlSequenceParameter:
		{
			if (m_State == State::ControlSequenceEntry && IsControlSequencePrefix(byte))
			{
				m_ControlSequencePrefix = byte;
				m_State = State::ControlSequenceParameter;
				return;
			}

			if (IsDecimalDigit(byte))
			{
				m_State = State::ControlSequenceParameter;

				if (m_ParametersFull)
				{
					return;
				}

				OpenFirstParameterIfNone();

				const int digitValue = byte - '0';
				m_Parameters.back() = static_cast<std::uint16_t>(
					std::min(m_Parameters.back() * kDecimalBase + digitValue, kLargestParameter));
				return;
			}

			if (byte == kParameterSeparator)
			{
				m_State = State::ControlSequenceParameter;

				if (m_ParametersFull)
				{
					return;
				}

				OpenFirstParameterIfNone();

				if (m_Parameters.size() >= kMostParameters)
				{
					m_ParametersFull = true;
					return;
				}

				m_Parameters.push_back(kOmittedParameter);
				return;
			}

			if (IsIntermediateByte(byte))
			{
				CollectIntermediate(byte);
				m_State = State::ControlSequenceIntermediate;
				return;
			}

			if (IsControlSequenceFinal(byte))
			{
				HandleControlSequence(byte, replies);
				Reset();
				return;
			}

			const bool opensUninterpretedSequence = byte == kSubParameterSeparator || IsControlSequencePrefix(byte);

			if (opensUninterpretedSequence)
			{
				m_State = State::ControlSequenceIgnore;
				return;
			}

			Reset();
			return;
		}
		case State::ControlSequenceIgnore:
		{
			if (IsControlByte(byte))
			{
				Reset();
				return;
			}

			if (IsControlSequenceFinal(byte))
			{
				Reset();
			}

			return;
		}
		case State::ControlSequenceIntermediate:
		{
			if (IsIntermediateByte(byte))
			{
				CollectIntermediate(byte);
				return;
			}

			if (IsControlSequenceFinal(byte))
			{
				HandleControlSequence(byte, replies);
			}
			else if (IsParameterByte(byte))
			{
				m_State = State::ControlSequenceIgnore;
				return;
			}

			Reset();
			return;
		}
		case State::OperatingSystemCommandString:
		{
			if (byte == kBellCharacter)
			{
				HandleOperatingSystemCommand();
				Reset();
				return;
			}

			if (byte == kEscapeCharacter)
			{
				m_State = State::OperatingSystemCommandEscape;
				return;
			}

			if (m_OperatingSystemCommandPayload.size() < kLongestOperatingSystemCommandPayload)
			{
				m_OperatingSystemCommandPayload.push_back(byte);
			}

			return;
		}
		case State::OperatingSystemCommandEscape:
		{
			if (byte == kStringTerminatorAfterEscape)
			{
				HandleOperatingSystemCommand();
				Reset();
				return;
			}

			// Any other byte means the ESC abandoned the string, so none of its payload is
			// committed: a half-written title is worse than none. That byte belongs to the sequence
			// the ESC opened, so it is replayed in the Escape state.
			Reset();
			m_State = State::Escape;
			ConsumeByte(byte, replies);
			return;
		}
		case State::StringSink:
		{
			if (byte == kBellCharacter)
			{
				Reset();
			}

			if (byte == kEscapeCharacter)
			{
				Reset();
				m_State = State::Escape;
			}

			return;
		}
		}
	}

	void VtParser::PrintByte(char byte)
	{
		Utf8Step step = m_Utf8Decoder.Feed(byte);

		const bool byteBrokeCharacterInProgress = step.Status == Utf8Status::Interrupted;

		if (byteBrokeCharacterInProgress)
		{
			m_Screen.Print(kReplacementCharacter, kReplacementCharacterWidth);
			step = m_Utf8Decoder.Feed(byte);
			CORE_ASSERT(step.Status != Utf8Status::Interrupted, "A byte fed with no character pending cannot interrupt one");
		}

		switch (step.Status)
		{
		case Utf8Status::Decoded:
			m_Screen.Print(step.CodePoint, CharacterWidth(step.CodePoint));
			break;
		case Utf8Status::Invalid:
			m_Screen.Print(kReplacementCharacter, kReplacementCharacterWidth);
			break;
		case Utf8Status::Pending:
		case Utf8Status::Interrupted:
			break;
		}
	}

	void VtParser::HandleControl(char byte)
	{
		switch (byte)
		{
		case '\r':
			m_Screen.CarriageReturn();
			break;
		case '\n':
		case '\v':
		case '\f':
			m_Screen.LineFeed();
			break;
		case '\b':
			m_Screen.Backspace();
			break;
		case '\t':
			m_Screen.Tab();
			break;
		default:
			break;
		}
	}

	void VtParser::HandleEscape(char byte)
	{
		switch (byte)
		{
		case kEscapeSaveCursor:
			m_Screen.SaveCursor();
			break;
		case kEscapeRestoreCursor:
			m_Screen.RestoreCursor();
			break;
		case kEscapeReverseIndex:
			m_Screen.ReverseIndex();
			break;
		case kEscapeIndex:
			m_Screen.LineFeed();
			break;
		case kEscapeNextLine:
			m_Screen.CarriageReturn();
			m_Screen.LineFeed();
			break;
		case kEscapeFullReset:
			// The alternate buffer is left before the display is erased, so that the erase clears
			// the primary screen.
			m_Screen.UseAlternateBuffer(kAlternateBufferAtPowerOn);
			m_Screen.ResetScrollRegion();
			m_Screen.SetAutoWrap(kAutoWrapAtPowerOn);
			m_Screen.SetBracketedPaste(kBracketedPasteAtPowerOn);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Clicks, kMouseModeAtPowerOn);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Drags, kMouseModeAtPowerOn);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Motion, kMouseModeAtPowerOn);
			m_Screen.SetSgrMouse(kSgrMouseAtPowerOn);
			m_Screen.SetCursorVisible(kCursorVisibleAtPowerOn);
			m_Screen.EraseInDisplay(EraseExtent::All);
			m_Screen.SetCursor(kHomeRow, kHomeColumn);
			// The screen has no call to clear the saved cursor, so saving the home position puts
			// back its power-on value.
			m_Screen.SaveCursor();
			m_Screen.SetPen(Pen{});
			break;
		default:
			break;
		}
	}

	void VtParser::HandleControlSequence(char finalByte, std::string& replies)
	{
		const bool intermediateChangesTheFinal = !m_IntermediateBytes.empty();

		if (intermediateChangesTheFinal)
		{
			return;
		}

		const bool asksSecondaryDeviceAttributes = m_ControlSequencePrefix == kSecondaryDeviceAttributesPrefix && finalByte == kControlSequenceDeviceAttributes;

		if (asksSecondaryDeviceAttributes)
		{
			replies += kSecondaryDeviceAttributesReply;
			return;
		}

		const bool hasUnimplementedPrefix = m_ControlSequencePrefix != kNoControlSequencePrefix && m_ControlSequencePrefix != kDecPrivatePrefix;

		if (hasUnimplementedPrefix)
		{
			return;
		}

		switch (finalByte)
		{
		case kControlSequenceCursorUp:
			m_Screen.MoveCursor(-static_cast<int>(ParameterOr(kFirstParameterIndex, kDefaultCount)), kNoMovement);
			break;
		case kControlSequenceCursorDown:
			m_Screen.MoveCursor(static_cast<int>(ParameterOr(kFirstParameterIndex, kDefaultCount)), kNoMovement);
			break;
		case kControlSequenceCursorForward:
			m_Screen.MoveCursor(kNoMovement, static_cast<int>(ParameterOr(kFirstParameterIndex, kDefaultCount)));
			break;
		case kControlSequenceCursorBack:
			m_Screen.MoveCursor(kNoMovement, -static_cast<int>(ParameterOr(kFirstParameterIndex, kDefaultCount)));
			break;
		case kControlSequenceCursorColumn:
			m_Screen.SetCursor(m_Screen.GetCursor().Row,
							   ZeroBased(ParameterOr(kFirstParameterIndex, kDefaultPosition)));
			break;
		case kControlSequenceCursorRow:
			m_Screen.SetCursor(ZeroBased(ParameterOr(kFirstParameterIndex, kDefaultPosition)),
							   m_Screen.GetCursor().Column);
			break;
		case kControlSequenceCursorPosition:
		case kControlSequenceHorizontalVerticalPosition:
			m_Screen.SetCursor(ZeroBased(ParameterOr(kFirstParameterIndex, kDefaultPosition)),
							   ZeroBased(ParameterOr(kSecondParameterIndex, kDefaultPosition)));
			break;
		case kControlSequenceEraseInDisplay:
			m_Screen.EraseInDisplay(ToEraseExtent(ParameterOr(kFirstParameterIndex, kOmittedParameter)));
			break;
		case kControlSequenceEraseInLine:
			m_Screen.EraseInLine(ToEraseExtent(ParameterOr(kFirstParameterIndex, kOmittedParameter)));
			break;
		case kControlSequenceInsertLines:
			m_Screen.InsertLines(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceDeleteLines:
			m_Screen.DeleteLines(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceInsertCharacters:
			m_Screen.InsertCharacters(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceDeleteCharacters:
			m_Screen.DeleteCharacters(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceEraseCharacters:
			m_Screen.EraseCharacters(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceScrollUp:
			m_Screen.ScrollUp(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceScrollDown:
			m_Screen.ScrollDown(ParameterOr(kFirstParameterIndex, kDefaultCount));
			break;
		case kControlSequenceSetScrollRegion:
			m_Screen.SetScrollRegion(
				ZeroBased(ParameterOr(kFirstParameterIndex, kDefaultPosition)),
				ZeroBased(ParameterOr(kSecondParameterIndex, m_Screen.GetRows())));
			break;
		case kControlSequenceSaveCursor:
			m_Screen.SaveCursor();
			break;
		case kControlSequenceRestoreCursor:
			m_Screen.RestoreCursor();
			break;
		case kControlSequenceSelectGraphicRendition:
			HandleGraphicRendition();
			break;
		case kControlSequenceSetMode:
			HandleMode(ModeRequest::TurnOn);
			break;
		case kControlSequenceResetMode:
			HandleMode(ModeRequest::TurnOff);
			break;
		case kControlSequenceDeviceAttributes:
			replies += kVt100WithAdvancedVideoAttributes;
			break;
		case kControlSequenceDeviceStatusReport:
			if (ParameterOr(kFirstParameterIndex, kOmittedParameter) == kCursorPositionRequest)
			{
				const CursorPosition cursor = m_Screen.GetCursor();
				replies += kControlSequenceIntroducer;
				replies += std::to_string(OneBased(cursor.Row)) + kParameterSeparator + std::to_string(OneBased(cursor.Column)) + kControlSequenceCursorPositionReport;
			}
			break;
		default:
			break;
		}
	}

	void VtParser::HandleOperatingSystemCommand()
	{
		const std::size_t separatorOffset = m_OperatingSystemCommandPayload.find(kParameterSeparator);

		if (separatorOffset == std::string::npos)
		{
			return;
		}

		const std::string_view command{m_OperatingSystemCommandPayload.data(), separatorOffset};
		const bool setsWindowTitle = command == kOperatingSystemCommandSetIconNameAndWindowTitle || command == kOperatingSystemCommandSetWindowTitle;

		if (setsWindowTitle)
		{
			const std::size_t titleOffset = separatorOffset + 1;
			m_Screen.SetTitle(m_OperatingSystemCommandPayload.substr(titleOffset));
		}
	}

	void VtParser::HandleGraphicRendition()
	{
		if (m_Parameters.empty())
		{
			m_Parameters.push_back(kGraphicRenditionReset);
		}

		Pen pen = m_Screen.GetPen();

		for (std::size_t parameterIndex = 0; parameterIndex < m_Parameters.size(); ++parameterIndex)
		{
			const std::uint16_t renditionCode = m_Parameters[parameterIndex];

			switch (renditionCode)
			{
			case kGraphicRenditionReset:
				pen = Pen{};
				break;
			case kGraphicRenditionBold:
				pen.Flags = pen.Flags | CellFlags::Bold;
				break;
			case kGraphicRenditionFaint:
				pen.Flags = pen.Flags | CellFlags::Faint;
				break;
			case kGraphicRenditionItalic:
				pen.Flags = pen.Flags | CellFlags::Italic;
				break;
			case kGraphicRenditionUnderline:
				pen.Flags = pen.Flags | CellFlags::Underline;
				break;
			case kGraphicRenditionInverse:
				pen.Flags = pen.Flags | CellFlags::Inverse;
				break;
			case kGraphicRenditionHidden:
				pen.Flags = pen.Flags | CellFlags::Hidden;
				break;
			case kGraphicRenditionNormalIntensity:
				pen.Flags = pen.Flags & ~(CellFlags::Bold | CellFlags::Faint);
				break;
			case kGraphicRenditionNotItalic:
				pen.Flags = pen.Flags & ~CellFlags::Italic;
				break;
			case kGraphicRenditionNotUnderlined:
				pen.Flags = pen.Flags & ~CellFlags::Underline;
				break;
			case kGraphicRenditionNotInverse:
				pen.Flags = pen.Flags & ~CellFlags::Inverse;
				break;
			case kGraphicRenditionNotHidden:
				pen.Flags = pen.Flags & ~CellFlags::Hidden;
				break;
			case kGraphicRenditionDefaultForeground:
				pen.Foreground = TerminalColor{};
				break;
			case kGraphicRenditionDefaultBackground:
				pen.Background = TerminalColor{};
				break;
			case kGraphicRenditionExtendedForeground:
			case kGraphicRenditionExtendedBackground:
			{
				const std::optional<ExtendedTerminalColor> extended = ReadExtendedTerminalColor(parameterIndex);

				if (!extended.has_value())
				{
					continue;
				}

				if (renditionCode == kGraphicRenditionExtendedForeground)
				{
					pen.Foreground = extended->Value;
				}
				else
				{
					pen.Background = extended->Value;
				}

				parameterIndex = extended->LastParameterIndex;
				break;
			}
			default:
			{
				if (renditionCode >= kGraphicRenditionFirstForeground && renditionCode <= kGraphicRenditionLastForeground)
				{
					pen.Foreground = TerminalColor::FromPalette(static_cast<std::uint8_t>(renditionCode - kGraphicRenditionFirstForeground));
				}
				else if (renditionCode >= kGraphicRenditionFirstBackground && renditionCode <= kGraphicRenditionLastBackground)
				{
					pen.Background = TerminalColor::FromPalette(static_cast<std::uint8_t>(renditionCode - kGraphicRenditionFirstBackground));
				}
				else if (renditionCode >= kGraphicRenditionFirstBrightForeground && renditionCode <= kGraphicRenditionLastBrightForeground)
				{
					pen.Foreground = TerminalColor::FromPalette(static_cast<std::uint8_t>(renditionCode - kGraphicRenditionFirstBrightForeground + kFirstBrightPaletteIndex));
				}
				else if (renditionCode >= kGraphicRenditionFirstBrightBackground && renditionCode <= kGraphicRenditionLastBrightBackground)
				{
					pen.Background = TerminalColor::FromPalette(static_cast<std::uint8_t>(renditionCode - kGraphicRenditionFirstBrightBackground + kFirstBrightPaletteIndex));
				}

				break;
			}
			}
		}

		m_Screen.SetPen(pen);
	}

	void VtParser::HandleMode(ModeRequest request)
	{
		const bool requestsPublicMode = m_ControlSequencePrefix != kDecPrivatePrefix;

		if (requestsPublicMode)
		{
			return;
		}

		const bool turnOn = request == ModeRequest::TurnOn;

		for (const std::uint16_t mode : m_Parameters)
		{
			switch (mode)
			{
			case kAutoWrapMode:
				m_Screen.SetAutoWrap(turnOn);
				break;
			case kCursorVisibleMode:
				m_Screen.SetCursorVisible(turnOn);
				break;
			case kMouseClicksMode:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Clicks, turnOn);
				break;
			case kMouseDragsMode:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Drags, turnOn);
				break;
			case kMouseMotionMode:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Motion, turnOn);
				break;
			case kSgrMouseMode:
				m_Screen.SetSgrMouse(turnOn);
				break;
			case kAlternateBufferMode:
				m_Screen.UseAlternateBuffer(turnOn);
				break;
			case kBracketedPasteMode:
				m_Screen.SetBracketedPaste(turnOn);
				break;
			default:
				break;
			}
		}
	}
}
