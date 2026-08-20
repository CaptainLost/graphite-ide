#include "Theme/BuiltInSchemes.h"

#include <iterator>

namespace Graphite {

	namespace {

		Core::Color FromBytes(int r, int g, int b, int a = 255)
		{
			return Core::Color{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
							   static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f};
		}

		struct SyntaxEntry
		{
			TokenKind Kind;
			int R;
			int G;
			int B;
			int A = 255;
		};

		struct EditorEntry
		{
			EditorColor Color;
			int R;
			int G;
			int B;
			int A = 255;
		};

		struct ChromeEntry
		{
			ChromeColor Color;
			int R;
			int G;
			int B;
			int A = 255;
		};

		constexpr SyntaxEntry kDarkSyntax[] = {
			{TokenKind::Keyword, 197, 134, 192},
			{TokenKind::Number, 181, 206, 168},
			{TokenKind::String, 206, 145, 120},
			{TokenKind::Character, 206, 145, 120},
			{TokenKind::Comment, 106, 153, 85},
			{TokenKind::Preprocessor, 155, 155, 155},

			// Chosen to separate by hue rather than by brightness, so the distinctions survive
			// being glanced at: types and namespaces share a teal family because both name
			// places rather than values, functions take the warm yellow a call is read by, and
			// macros a red that says "this is not what it looks like". Members and parameters
			// stay close to plain text on purpose - they are the most common identifiers on any
			// line, and colouring them loudly would make ordinary code look alarming.
			{TokenKind::SemanticType, 78, 201, 176},
			{TokenKind::SemanticFunction, 220, 220, 170},
			{TokenKind::SemanticMacro, 190, 128, 116},
			{TokenKind::SemanticMember, 156, 220, 254},
			{TokenKind::SemanticParameter, 156, 190, 220},
			{TokenKind::SemanticNamespace, 106, 190, 180},
		};

		// Background and GutterFill are the window background, so the gutter reads as part of
		// the same surface as the text rather than as a strip beside it. Guide and LineNumber
		// are both the disabled-text colour; Guide takes it at 35% and LineNumber at full
		// strength, so the guide reads clearly dimmer than the numbers next to it rather than
		// the same weight by coincidence. CurrentLine is the frame background at 45% —
		// structure, not content: dimmer again than either.
		constexpr EditorEntry kDarkEditor[] = {
			{EditorColor::Background, 27, 29, 33},
			{EditorColor::Text, 214, 219, 226},
			{EditorColor::Selection, 66, 150, 250, 89},
			{EditorColor::CurrentLine, 37, 40, 45, 115},
			{EditorColor::Guide, 123, 130, 141, 89},
			{EditorColor::GutterFill, 27, 29, 33},
			{EditorColor::LineNumber, 123, 130, 141},
			{EditorColor::Caret, 214, 219, 226},
			{EditorColor::FindMatch, 88, 88, 40, 140},
			{EditorColor::FindCurrentMatch, 120, 100, 30, 200},
			{EditorColor::Lens, 128, 128, 128},
			{EditorColor::MatchingBracket, 90, 96, 112, 179},
			{EditorColor::UnmatchedBracket, 192, 57, 43, 179},
		};

		// The editor table is not sparse like the syntax one: every EditorColor is meant to be
		// drawn as itself, so a gap here must fail to build rather than silently paint that
		// region of the editor in the text colour.
		static_assert(std::size(kDarkEditor) == static_cast<std::size_t>(EditorColor::Count));

		// These are Core::Theme's own default palette, transcribed once. From here the
		// application runs on this table and Core's default is only what an application built
		// on Core alone falls back to — which is what ends the silent-drift problem the editor
		// colours above used to have.
		constexpr ChromeEntry kDarkChrome[] = {
			{ChromeColor::Background, 27, 29, 33},
			{ChromeColor::Surface, 37, 40, 45},
			{ChromeColor::SurfaceHovered, 50, 54, 61},
			{ChromeColor::SurfaceActive, 62, 67, 76},
			{ChromeColor::Accent, 66, 133, 244},
			{ChromeColor::Text, 214, 219, 226},
			{ChromeColor::TextDisabled, 123, 130, 141},
			{ChromeColor::Border, 52, 56, 63},
		};

		static_assert(std::size(kDarkChrome) == static_cast<std::size_t>(ChromeColor::Count));

		constexpr SyntaxEntry kLightSyntax[] = {
			{TokenKind::Keyword, 0, 0, 255},
			{TokenKind::Number, 9, 134, 88},
			{TokenKind::String, 163, 21, 21},
			{TokenKind::Character, 163, 21, 21},
			{TokenKind::Comment, 0, 128, 0},
			{TokenKind::Preprocessor, 128, 128, 128},

			// The same reasoning as the dark scheme's, darkened to hold contrast on white.
			{TokenKind::SemanticType, 38, 127, 153},
			{TokenKind::SemanticFunction, 121, 94, 38},
			{TokenKind::SemanticMacro, 175, 0, 219},
			{TokenKind::SemanticMember, 0, 16, 128},
			{TokenKind::SemanticParameter, 43, 65, 115},
			{TokenKind::SemanticNamespace, 38, 127, 153},
		};

		constexpr EditorEntry kLightEditor[] = {
			{EditorColor::Background, 255, 255, 255},
			{EditorColor::Text, 36, 36, 36},
			{EditorColor::Selection, 173, 214, 255, 153},
			{EditorColor::CurrentLine, 0, 0, 0, 20},
			{EditorColor::Guide, 0, 0, 0, 40},
			{EditorColor::GutterFill, 255, 255, 255},
			{EditorColor::LineNumber, 140, 140, 140},
			{EditorColor::Caret, 36, 36, 36},
			{EditorColor::FindMatch, 250, 235, 130, 160},
			{EditorColor::FindCurrentMatch, 250, 200, 60, 220},
			{EditorColor::Lens, 150, 150, 150},
			{EditorColor::MatchingBracket, 176, 196, 222, 179},
			{EditorColor::UnmatchedBracket, 231, 76, 60, 128},
		};

		static_assert(std::size(kLightEditor) == static_cast<std::size_t>(EditorColor::Count));

		// Neutral greys, and the dark scheme's blue accent darkened to hold contrast against
		// them.
		constexpr ChromeEntry kLightChrome[] = {
			{ChromeColor::Background, 243, 243, 243},
			{ChromeColor::Surface, 255, 255, 255},
			{ChromeColor::SurfaceHovered, 229, 229, 229},
			{ChromeColor::SurfaceActive, 214, 214, 214},
			{ChromeColor::Accent, 10, 98, 208},
			{ChromeColor::Text, 36, 36, 36},
			{ChromeColor::TextDisabled, 110, 110, 110},
			{ChromeColor::Border, 200, 200, 200},
		};

		static_assert(std::size(kLightChrome) == static_cast<std::size_t>(ChromeColor::Count));

		template <std::size_t SyntaxCount, std::size_t EditorCount, std::size_t ChromeCount>
		ResolvedScheme Build(const SyntaxEntry (&syntax)[SyntaxCount],
							  const EditorEntry (&editor)[EditorCount],
							  const ChromeEntry (&chrome)[ChromeCount])
		{
			ResolvedScheme scheme;

			for (const ChromeEntry& entry : chrome)
			{
				scheme.Chrome[static_cast<std::size_t>(entry.Color)] =
					FromBytes(entry.R, entry.G, entry.B, entry.A);
			}

			for (const EditorEntry& entry : editor)
			{
				scheme.Editor[static_cast<std::size_t>(entry.Color)] =
					FromBytes(entry.R, entry.G, entry.B, entry.A);
			}

			// The syntax fallback IS the editor's text colour: a kind with no colour of its own
			// must draw exactly as ordinary text does, and deriving it here is what keeps that
			// true when one of them is edited.
			scheme.Syntax.fill(scheme.Editor[static_cast<std::size_t>(EditorColor::Text)]);

			for (const SyntaxEntry& entry : syntax)
			{
				scheme.Syntax[static_cast<std::size_t>(entry.Kind)] =
					FromBytes(entry.R, entry.G, entry.B, entry.A);
			}

			return scheme;
		}

		ResolvedScheme MakeDark()
		{
			return Build(kDarkSyntax, kDarkEditor, kDarkChrome);
		}

		ResolvedScheme MakeLight()
		{
			return Build(kLightSyntax, kLightEditor, kLightChrome);
		}
	}

	const ResolvedScheme& DarkScheme()
	{
		// Function-local rather than namespace-scope, per ADR 0033 on complex initialization of
		// global objects.
		static const ResolvedScheme scheme = MakeDark();

		return scheme;
	}

	const ResolvedScheme& LightScheme()
	{
		static const ResolvedScheme scheme = MakeLight();

		return scheme;
	}

	const ResolvedScheme& SchemeFor(BuiltInScheme scheme)
	{
		switch (scheme)
		{
			case BuiltInScheme::Dark: return DarkScheme();
			case BuiltInScheme::Light: return LightScheme();
		}

		return DarkScheme();
	}

	std::string_view NameOf(BuiltInScheme scheme)
	{
		switch (scheme)
		{
			case BuiltInScheme::Dark: return "dark";
			case BuiltInScheme::Light: return "light";
		}

		return {};
	}

	std::optional<BuiltInScheme> ParseBuiltInName(std::string_view name)
	{
		if (name == "dark")
		{
			return BuiltInScheme::Dark;
		}

		if (name == "light")
		{
			return BuiltInScheme::Light;
		}

		return std::nullopt;
	}
}
