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

		constexpr SyntaxEntry kDarkSyntax[] = {
			{TokenKind::Keyword, 197, 134, 192},
			{TokenKind::Number, 181, 206, 168},
			{TokenKind::String, 206, 145, 120},
			{TokenKind::Character, 206, 145, 120},
			{TokenKind::Comment, 106, 153, 85},
			{TokenKind::Preprocessor, 155, 155, 155},
		};

		// Mirrors Core::Theme::ApplyDark — Text, TextDisabled, WindowBg and FrameBg — plus
		// ImGui's own dark default for TextSelectedBg, which ApplyDark leaves alone. Nothing
		// checks that these still agree: Theme/ is deliberately free of ImGui, so the values
		// are copied rather than read, and an edit to ApplyDark's palette will not fail here.
		//
		// Background and GutterFill are WindowBg. Guide and LineNumber are both TextDisabled;
		// Guide is TextDisabled at 35% and LineNumber is TextDisabled at full strength, so the
		// guide reads clearly dimmer than the numbers next to it rather than the same weight by
		// coincidence. CurrentLine is FrameBg at 45% — structure, not content: dimmer again than
		// either.
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
		};

		// The editor table is not sparse like the syntax one: every EditorColor is meant to be
		// drawn as itself, so a gap here must fail to build rather than silently paint that
		// region of the editor in the text colour.
		static_assert(std::size(kDarkEditor) == static_cast<std::size_t>(EditorColor::Count));

		constexpr SyntaxEntry kLightSyntax[] = {
			{TokenKind::Keyword, 0, 0, 255},
			{TokenKind::Number, 9, 134, 88},
			{TokenKind::String, 163, 21, 21},
			{TokenKind::Character, 163, 21, 21},
			{TokenKind::Comment, 0, 128, 0},
			{TokenKind::Preprocessor, 128, 128, 128},
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
		};

		static_assert(std::size(kLightEditor) == static_cast<std::size_t>(EditorColor::Count));

		template <std::size_t SyntaxCount, std::size_t EditorCount>
		ResolvedScheme Build(const SyntaxEntry (&syntax)[SyntaxCount],
							  const EditorEntry (&editor)[EditorCount])
		{
			ResolvedScheme scheme;

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
			return Build(kDarkSyntax, kDarkEditor);
		}

		ResolvedScheme MakeLight()
		{
			return Build(kLightSyntax, kLightEditor);
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
