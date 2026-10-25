#include "Theme/DerivedSchemes.h"

#include "Core/Base/Color.h"
#include "Theme/BuiltInSchemes.h"

#include <array>
#include <cstdint>

namespace Graphite {

	namespace {

		constexpr int kOpaque = 255;
		constexpr int kDarkSelectionAlpha = 89;
		constexpr int kLightSelectionAlpha = 70;
		constexpr int kDarkOccurrenceAlpha = 45;
		constexpr int kLightOccurrenceAlpha = 30;
		constexpr int kDarkGuideAlpha = 45;
		constexpr int kLightGuideAlpha = 40;
		constexpr int kBracketAlpha = 179;
		constexpr int kCurrentLineAlpha = 10;
		constexpr int kDarkUserBlockAlpha = 170;
		constexpr int kDarkClaudeBlockAlpha = 110;

		constexpr int kRedShift = 16;
		constexpr int kGreenShift = 8;
		constexpr std::uint32_t kByteMask = 0xFF;

		struct Palette
		{
			bool Light = false;

			std::uint32_t ChromeBackground = 0;
			std::uint32_t Surface = 0;
			std::uint32_t SurfaceHovered = 0;
			std::uint32_t SurfaceActive = 0;
			std::uint32_t Border = 0;
			std::uint32_t Text = 0;
			std::uint32_t TextDisabled = 0;
			std::uint32_t Accent = 0;

			std::uint32_t EditorBackground = 0;
			std::uint32_t LineNumber = 0;

			std::uint32_t Keyword = 0;
			std::uint32_t Number = 0;
			std::uint32_t String = 0;
			std::uint32_t Comment = 0;
			std::uint32_t Preprocessor = 0;
			std::uint32_t Type = 0;
			std::uint32_t Function = 0;
			std::uint32_t Macro = 0;
			std::uint32_t Member = 0;
			std::uint32_t Parameter = 0;
			std::uint32_t Namespace = 0;

			std::array<float, kLayoutMetricCount> Layout = DefaultLayout();
		};

		Core::Color FromHex(std::uint32_t hex, int alpha = kOpaque)
		{
			return Core::Color{Core::ChannelFromByte(static_cast<int>((hex >> kRedShift) & kByteMask)),
							   Core::ChannelFromByte(static_cast<int>((hex >> kGreenShift) & kByteMask)),
							   Core::ChannelFromByte(static_cast<int>(hex & kByteMask)), Core::ChannelFromByte(alpha)};
		}

		// The syntax colours the Graphite schemes share; the light one has its own.
		Palette GraphiteDark(std::uint32_t chrome, std::uint32_t editor, std::uint32_t surface, std::uint32_t hovered,
							 std::uint32_t active, std::uint32_t border, std::uint32_t text, std::uint32_t textDisabled,
							 std::uint32_t accent, std::uint32_t lineNumber, std::uint32_t comment)
		{
			Palette palette;

			palette.ChromeBackground = chrome;
			palette.EditorBackground = editor;
			palette.Surface = surface;
			palette.SurfaceHovered = hovered;
			palette.SurfaceActive = active;
			palette.Border = border;
			palette.Text = text;
			palette.TextDisabled = textDisabled;
			palette.Accent = accent;
			palette.LineNumber = lineNumber;

			palette.Keyword = 0xB4A0F0;
			palette.Number = 0xE8A87C;
			palette.String = 0xA5CF8A;
			palette.Comment = comment;
			palette.Preprocessor = 0x8D95A3;
			palette.Type = 0x6FD1C0;
			palette.Function = 0xE6CC8A;
			palette.Macro = 0xE19AA0;
			palette.Member = 0x9CCBF0;
			palette.Parameter = 0xE9B5A1;
			palette.Namespace = 0x79B8B0;

			return palette;
		}

		Palette GraphiteLightPalette()
		{
			Palette palette;

			palette.Light = true;
			palette.ChromeBackground = 0xE7E8EA;
			palette.EditorBackground = 0xF2F2F3;
			palette.Surface = 0xDCDDE0;
			palette.SurfaceHovered = 0xD0D1D5;
			palette.SurfaceActive = 0xC3C5CA;
			palette.Border = 0xCFD0D4;
			palette.Text = 0x26272A;
			palette.TextDisabled = 0x6A6C72;
			palette.Accent = 0x2F6FD0;
			palette.LineNumber = 0x9A9CA2;

			palette.Keyword = 0x6F42C1;
			palette.Number = 0xB3541E;
			palette.String = 0x2F7D32;
			palette.Comment = 0x7A7C82;
			palette.Preprocessor = 0x6A6C72;
			palette.Type = 0x0F7B6C;
			palette.Function = 0x8A6A00;
			palette.Macro = 0xA63F4A;
			palette.Member = 0x1D5FA8;
			palette.Parameter = 0x9A4F2A;
			palette.Namespace = 0x137A6E;

			return palette;
		}

		// Gruvbox keeps one set of syntax colours per brightness; only the backgrounds differ between hard, medium and soft.
		Palette GruvboxDark(std::uint32_t chrome, std::uint32_t editor, std::uint32_t surface, std::uint32_t hovered,
							std::uint32_t active)
		{
			Palette palette;

			palette.ChromeBackground = chrome;
			palette.EditorBackground = editor;
			palette.Surface = surface;
			palette.SurfaceHovered = hovered;
			palette.SurfaceActive = active;
			palette.Border = surface;
			palette.Text = 0xEBDBB2;
			palette.TextDisabled = 0xA89984;
			palette.Accent = 0xFABD2F;
			palette.LineNumber = 0x7C6F64;

			palette.Keyword = 0xFB4934;
			palette.Number = 0xD3869B;
			palette.String = 0xB8BB26;
			palette.Comment = 0x928374;
			palette.Preprocessor = 0x8EC07C;
			palette.Type = 0xFABD2F;
			palette.Function = 0x83A598;
			palette.Macro = 0xFE8019;
			palette.Member = 0xD5C4A1;
			palette.Parameter = 0xBDAE93;
			palette.Namespace = 0x8EC07C;

			return palette;
		}

		Palette GruvboxLight(std::uint32_t chrome, std::uint32_t editor, std::uint32_t surface, std::uint32_t hovered,
							 std::uint32_t active)
		{
			Palette palette;

			palette.Light = true;
			palette.ChromeBackground = chrome;
			palette.EditorBackground = editor;
			palette.Surface = surface;
			palette.SurfaceHovered = hovered;
			palette.SurfaceActive = active;
			palette.Border = hovered;
			palette.Text = 0x3C3836;
			palette.TextDisabled = 0x7C6F64;
			palette.Accent = 0xB57614;
			palette.LineNumber = 0xA89984;

			palette.Keyword = 0x9D0006;
			palette.Number = 0x8F3F71;
			palette.String = 0x79740E;
			palette.Comment = 0x928374;
			palette.Preprocessor = 0x427B58;
			palette.Type = 0xB57614;
			palette.Function = 0x076678;
			palette.Macro = 0xAF3A03;
			palette.Member = 0x504945;
			palette.Parameter = 0x665C54;
			palette.Namespace = 0x427B58;

			return palette;
		}

		Palette PaletteOf(BuiltInScheme which)
		{
			switch (which)
			{
			case BuiltInScheme::Graphite:
				return GraphiteDark(0x26272A, 0x2B2C2F, 0x303236, 0x3A3C40, 0x46484D, 0x3B3D41, 0xD9DADD, 0x93959B, 0x5B9BF0,
									0x6B6D73, 0x6B7380);
			case BuiltInScheme::GraphiteBright:
				return GraphiteDark(0x2F3033, 0x343538, 0x3A3B3F, 0x444549, 0x505156, 0x46474B, 0xE0E1E4, 0x9A9CA2, 0x6AA5F2,
									0x74767C, 0x7A818D);
			case BuiltInScheme::GraphiteWarm:
				return GraphiteDark(0x29282A, 0x2E2D2F, 0x343336, 0x3E3D40, 0x4A494C, 0x404043, 0xDEDCDC, 0x979597, 0x6B9EE6,
									0x706E70, 0x78757A);
			case BuiltInScheme::GraphiteCards:
			{
				Palette palette = GraphiteDark(0x2A2B2E, 0x2F3033, 0x35363A, 0x3F4044, 0x4B4C51, 0x3A3B3F, 0xDADBDE, 0x94969C,
											   0x5B9BF0, 0x6D6F75, 0x6B7380);

				palette.Layout[static_cast<std::size_t>(LayoutMetric::PanelGap)] = 6.0f;
				palette.Layout[static_cast<std::size_t>(LayoutMetric::PanelRounding)] = 8.0f;

				return palette;
			}
			case BuiltInScheme::GraphiteLight:
				return GraphiteLightPalette();
			case BuiltInScheme::GruvboxDarkHard:
				return GruvboxDark(0x191B1C, 0x1D2021, 0x32302F, 0x3C3836, 0x504945);
			case BuiltInScheme::GruvboxDarkMedium:
				return GruvboxDark(0x1D2021, 0x282828, 0x3C3836, 0x504945, 0x665C54);
			case BuiltInScheme::GruvboxDarkSoft:
				return GruvboxDark(0x282828, 0x32302F, 0x3C3836, 0x504945, 0x665C54);
			case BuiltInScheme::GruvboxLightHard:
				return GruvboxLight(0xF2E5BC, 0xF9F5D7, 0xF9F5D7, 0xEBDBB2, 0xD5C4A1);
			case BuiltInScheme::GruvboxLightMedium:
				return GruvboxLight(0xEBDBB2, 0xFBF1C7, 0xF9F5D7, 0xD5C4A1, 0xBDAE93);
			case BuiltInScheme::GruvboxLightSoft:
				return GruvboxLight(0xEBDBB2, 0xF2E5BC, 0xF2E5BC, 0xD5C4A1, 0xBDAE93);
			case BuiltInScheme::Dark:
			case BuiltInScheme::Light:
			case BuiltInScheme::Count:
				break;
			}

			return Palette{};
		}

		void SetSyntax(ResolvedScheme& scheme, TokenKind kind, std::uint32_t hex)
		{
			scheme.Syntax[static_cast<std::size_t>(kind)] = FromHex(hex);
		}

		void SetEditor(ResolvedScheme& scheme, EditorColor slot, const Core::Color& color)
		{
			scheme.Editor[static_cast<std::size_t>(slot)] = color;
		}

		void SetChrome(ResolvedScheme& scheme, ChromeColor slot, std::uint32_t hex)
		{
			scheme.Chrome[static_cast<std::size_t>(slot)] = FromHex(hex);
		}

		void SetSession(ResolvedScheme& scheme, SessionColor slot, const Core::Color& color)
		{
			scheme.Session[static_cast<std::size_t>(slot)] = color;
		}

		void ApplySyntax(ResolvedScheme& scheme, const Palette& palette)
		{
			for (const TokenKind plain : {TokenKind::Text, TokenKind::Identifier, TokenKind::Punctuation, TokenKind::Invalid})
			{
				SetSyntax(scheme, plain, palette.Text);
			}

			SetSyntax(scheme, TokenKind::Keyword, palette.Keyword);
			SetSyntax(scheme, TokenKind::Number, palette.Number);
			SetSyntax(scheme, TokenKind::String, palette.String);
			SetSyntax(scheme, TokenKind::Character, palette.String);
			SetSyntax(scheme, TokenKind::Comment, palette.Comment);
			SetSyntax(scheme, TokenKind::Preprocessor, palette.Preprocessor);
			SetSyntax(scheme, TokenKind::SemanticType, palette.Type);
			SetSyntax(scheme, TokenKind::SemanticFunction, palette.Function);
			SetSyntax(scheme, TokenKind::SemanticMacro, palette.Macro);
			SetSyntax(scheme, TokenKind::SemanticMember, palette.Member);
			SetSyntax(scheme, TokenKind::SemanticParameter, palette.Parameter);
			SetSyntax(scheme, TokenKind::SemanticNamespace, palette.Namespace);
		}

		void ApplyChrome(ResolvedScheme& scheme, const Palette& palette)
		{
			SetChrome(scheme, ChromeColor::Background, palette.ChromeBackground);
			SetChrome(scheme, ChromeColor::Surface, palette.Surface);
			SetChrome(scheme, ChromeColor::SurfaceHovered, palette.SurfaceHovered);
			SetChrome(scheme, ChromeColor::SurfaceActive, palette.SurfaceActive);
			SetChrome(scheme, ChromeColor::Accent, palette.Accent);
			SetChrome(scheme, ChromeColor::Text, palette.Text);
			SetChrome(scheme, ChromeColor::TextDisabled, palette.TextDisabled);
			SetChrome(scheme, ChromeColor::Border, palette.Border);
		}

		void ApplyEditor(ResolvedScheme& scheme, const Palette& palette)
		{
			const int lineTint = palette.Light ? 0 : kOpaque;

			SetEditor(scheme, EditorColor::Background, FromHex(palette.EditorBackground));
			SetEditor(scheme, EditorColor::GutterFill, FromHex(palette.EditorBackground));
			SetEditor(scheme, EditorColor::Text, FromHex(palette.Text));
			SetEditor(scheme, EditorColor::Caret, FromHex(palette.Text));
			SetEditor(scheme, EditorColor::LineNumber, FromHex(palette.LineNumber));
			SetEditor(scheme, EditorColor::Lens, FromHex(palette.TextDisabled));
			SetEditor(scheme, EditorColor::Selection, FromHex(palette.Accent, palette.Light ? kLightSelectionAlpha : kDarkSelectionAlpha));
			SetEditor(scheme, EditorColor::SelectionOccurrence,
					  FromHex(palette.Accent, palette.Light ? kLightOccurrenceAlpha : kDarkOccurrenceAlpha));
			SetEditor(scheme, EditorColor::Guide, FromHex(palette.TextDisabled, palette.Light ? kLightGuideAlpha : kDarkGuideAlpha));
			SetEditor(scheme, EditorColor::MatchingBracket, FromHex(palette.SurfaceActive, kBracketAlpha));
			SetEditor(scheme, EditorColor::CurrentLine,
					  Core::Color{Core::ChannelFromByte(lineTint), Core::ChannelFromByte(lineTint), Core::ChannelFromByte(lineTint),
								  Core::ChannelFromByte(kCurrentLineAlpha)});
		}

		void ApplySessionBlocks(ResolvedScheme& scheme, const Palette& palette)
		{
			const int userAlpha = palette.Light ? kOpaque : kDarkUserBlockAlpha;
			const int claudeAlpha = palette.Light ? kOpaque : kDarkClaudeBlockAlpha;

			SetSession(scheme, SessionColor::UserBlock, FromHex(palette.SurfaceActive, userAlpha));
			SetSession(scheme, SessionColor::ClaudeBlock, FromHex(palette.Surface, claudeAlpha));
			SetSession(scheme, SessionColor::CodeBlock, FromHex(palette.ChromeBackground));
		}
	}

	ResolvedScheme BuildDerivedScheme(BuiltInScheme which)
	{
		const Palette palette = PaletteOf(which);

		ResolvedScheme scheme = palette.Light ? LightScheme() : DarkScheme();

		ApplySyntax(scheme, palette);
		ApplyEditor(scheme, palette);
		ApplyChrome(scheme, palette);
		ApplySessionBlocks(scheme, palette);

		scheme.Layout = palette.Layout;

		return scheme;
	}
}
