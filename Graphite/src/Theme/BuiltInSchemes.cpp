#include "Theme/BuiltInSchemes.h"

namespace Graphite {

	namespace {

		Core::Color FromBytes(int r, int g, int b, int a = 255)
		{
			return Core::Color{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
							   static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f};
		}

		void Set(ResolvedScheme& scheme, TokenKind kind, Core::Color color)
		{
			scheme.Syntax[static_cast<std::size_t>(kind)] = color;
		}

		void Set(ResolvedScheme& scheme, EditorColor color, Core::Color value)
		{
			scheme.Editor[static_cast<std::size_t>(color)] = value;
		}

		ResolvedScheme MakeDark()
		{
			ResolvedScheme scheme;

			const Core::Color text = FromBytes(214, 219, 226);

			// Every token kind starts at the text colour, so a kind added later draws as plain
			// text rather than as whatever the array was left holding.
			scheme.Syntax.fill(text);
			scheme.Editor.fill(text);

			Set(scheme, TokenKind::Keyword, FromBytes(197, 134, 192));
			Set(scheme, TokenKind::Number, FromBytes(181, 206, 168));
			Set(scheme, TokenKind::String, FromBytes(206, 145, 120));
			Set(scheme, TokenKind::Character, FromBytes(206, 145, 120));
			Set(scheme, TokenKind::Comment, FromBytes(106, 153, 85));
			Set(scheme, TokenKind::Preprocessor, FromBytes(155, 155, 155));

			Set(scheme, EditorColor::Background, FromBytes(27, 29, 33));
			Set(scheme, EditorColor::Text, text);
			Set(scheme, EditorColor::Selection, FromBytes(66, 150, 250, 89));
			Set(scheme, EditorColor::CurrentLine, FromBytes(37, 40, 45, 115));
			Set(scheme, EditorColor::Guide, FromBytes(123, 130, 141, 89));
			Set(scheme, EditorColor::GutterFill, FromBytes(27, 29, 33));
			Set(scheme, EditorColor::LineNumber, FromBytes(123, 130, 141));
			Set(scheme, EditorColor::Caret, text);

			return scheme;
		}

		ResolvedScheme MakeLight()
		{
			ResolvedScheme scheme;

			const Core::Color text = FromBytes(36, 36, 36);

			scheme.Syntax.fill(text);
			scheme.Editor.fill(text);

			Set(scheme, TokenKind::Keyword, FromBytes(0, 0, 255));
			Set(scheme, TokenKind::Number, FromBytes(9, 134, 88));
			Set(scheme, TokenKind::String, FromBytes(163, 21, 21));
			Set(scheme, TokenKind::Character, FromBytes(163, 21, 21));
			Set(scheme, TokenKind::Comment, FromBytes(0, 128, 0));
			Set(scheme, TokenKind::Preprocessor, FromBytes(128, 128, 128));

			Set(scheme, EditorColor::Background, FromBytes(255, 255, 255));
			Set(scheme, EditorColor::Text, text);
			Set(scheme, EditorColor::Selection, FromBytes(173, 214, 255, 153));
			Set(scheme, EditorColor::CurrentLine, FromBytes(0, 0, 0, 20));
			Set(scheme, EditorColor::Guide, FromBytes(0, 0, 0, 40));
			Set(scheme, EditorColor::GutterFill, FromBytes(255, 255, 255));
			Set(scheme, EditorColor::LineNumber, FromBytes(140, 140, 140));
			Set(scheme, EditorColor::Caret, text);

			return scheme;
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

	const ResolvedScheme* BuiltInScheme(std::string_view name)
	{
		if (name == "dark")
		{
			return &DarkScheme();
		}

		if (name == "light")
		{
			return &LightScheme();
		}

		return nullptr;
	}

	bool IsBuiltInName(std::string_view name)
	{
		return BuiltInScheme(name) != nullptr;
	}
}
