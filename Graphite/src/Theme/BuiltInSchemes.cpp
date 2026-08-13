#include "Theme/BuiltInSchemes.h"

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

		constexpr EditorEntry kDarkEditor[] = {
			{EditorColor::Background, 27, 29, 33},
			{EditorColor::Text, 214, 219, 226},
			{EditorColor::Selection, 66, 150, 250, 89},
			{EditorColor::CurrentLine, 37, 40, 45, 115},
			{EditorColor::Guide, 123, 130, 141, 89},
			{EditorColor::GutterFill, 27, 29, 33},
			{EditorColor::LineNumber, 123, 130, 141},
			{EditorColor::Caret, 214, 219, 226},
		};

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
		};

		template <std::size_t SyntaxCount, std::size_t EditorCount>
		ResolvedScheme Build(Core::Color text, const SyntaxEntry (&syntax)[SyntaxCount],
							  const EditorEntry (&editor)[EditorCount])
		{
			ResolvedScheme scheme;

			// Every token kind starts at the text colour, so a kind added later draws as plain
			// text rather than as whatever the array was left holding.
			scheme.Syntax.fill(text);
			scheme.Editor.fill(text);

			for (const SyntaxEntry& entry : syntax)
			{
				scheme.Syntax[static_cast<std::size_t>(entry.Kind)] =
					FromBytes(entry.R, entry.G, entry.B, entry.A);
			}

			for (const EditorEntry& entry : editor)
			{
				scheme.Editor[static_cast<std::size_t>(entry.Color)] =
					FromBytes(entry.R, entry.G, entry.B, entry.A);
			}

			return scheme;
		}

		ResolvedScheme MakeDark()
		{
			return Build(FromBytes(214, 219, 226), kDarkSyntax, kDarkEditor);
		}

		ResolvedScheme MakeLight()
		{
			return Build(FromBytes(36, 36, 36), kLightSyntax, kLightEditor);
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

	const ResolvedScheme& Get(BuiltInScheme scheme)
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
