#include "Theme/BuiltInSchemes.h"

#include "Core/Base/Color.h"
#include "Theme/DerivedSchemes.h"

#include <array>
#include <iterator>
#include <optional>
#include <span>

namespace Graphite {

	namespace {

		constexpr int kOpaqueAlphaByte = 255;

		Core::Color ColorFromBytes(int red, int green, int blue, int alpha)
		{
			return Core::Color{Core::ChannelFromByte(red), Core::ChannelFromByte(green),
							   Core::ChannelFromByte(blue), Core::ChannelFromByte(alpha)};
		}

		struct SyntaxEntry
		{
			TokenKind Kind;
			int Red;
			int Green;
			int Blue;
			int Alpha = kOpaqueAlphaByte;
		};

		struct EditorEntry
		{
			EditorColor Color;
			int Red;
			int Green;
			int Blue;
			int Alpha = kOpaqueAlphaByte;
		};

		struct ChromeEntry
		{
			ChromeColor Color;
			int Red;
			int Green;
			int Blue;
			int Alpha = kOpaqueAlphaByte;
		};

		struct GitEntry
		{
			GitColor Color;
			int Red;
			int Green;
			int Blue;
			int Alpha = kOpaqueAlphaByte;
		};

		struct SessionEntry
		{
			SessionColor Color;
			int Red;
			int Green;
			int Blue;
			int Alpha = kOpaqueAlphaByte;
		};

		constexpr SyntaxEntry kDarkSyntax[] = {
			{TokenKind::Keyword, 197, 134, 192},
			{TokenKind::Number, 181, 206, 168},
			{TokenKind::String, 206, 145, 120},
			{TokenKind::Character, 206, 145, 120},
			{TokenKind::Comment, 106, 153, 85},
			{TokenKind::Preprocessor, 155, 155, 155},

			{TokenKind::SemanticType, 78, 201, 176},
			{TokenKind::SemanticFunction, 220, 220, 170},
			{TokenKind::SemanticMacro, 190, 128, 116},
			{TokenKind::SemanticMember, 156, 220, 254},
			{TokenKind::SemanticParameter, 156, 190, 220},
			{TokenKind::SemanticNamespace, 106, 190, 180},
		};

		constexpr EditorEntry kDarkEditor[] = {
			{EditorColor::Background, 27, 29, 33},
			{EditorColor::Text, 214, 219, 226},
			{EditorColor::Selection, 66, 150, 250, 89},
			{EditorColor::CurrentLine, 255, 255, 255, 10},
			{EditorColor::Guide, 123, 130, 141, 45},
			{EditorColor::GutterFill, 27, 29, 33},
			{EditorColor::LineNumber, 77, 82, 91},
			{EditorColor::Caret, 214, 219, 226},
			{EditorColor::FindMatch, 88, 88, 40, 140},
			{EditorColor::FindCurrentMatch, 120, 100, 30, 200},
			{EditorColor::Lens, 107, 113, 128},
			{EditorColor::MatchingBracket, 90, 96, 112, 179},
			{EditorColor::UnmatchedBracket, 192, 57, 43, 179},
			{EditorColor::DiagnosticError, 224, 108, 117},
			{EditorColor::DiagnosticWarning, 229, 192, 123},
		};

		static_assert(std::size(kDarkEditor) == static_cast<std::size_t>(EditorColor::Count));

		constexpr ChromeEntry kDarkChrome[] = {
			{ChromeColor::Background, 20, 21, 24},
			{ChromeColor::Surface, 37, 40, 45},
			{ChromeColor::SurfaceHovered, 50, 54, 61},
			{ChromeColor::SurfaceActive, 62, 67, 76},
			{ChromeColor::Accent, 66, 133, 244},
			{ChromeColor::Text, 214, 219, 226},
			{ChromeColor::TextDisabled, 123, 130, 141},
			{ChromeColor::Border, 36, 39, 44},
		};

		static_assert(std::size(kDarkChrome) == static_cast<std::size_t>(ChromeColor::Count));

		constexpr GitEntry kDarkGit[] = {
			{GitColor::DiffAddedLine, 46, 80, 46, 90},
			{GitColor::DiffRemovedLine, 90, 46, 46, 90},
			{GitColor::DiffAddedText, 152, 195, 121},
			{GitColor::DiffRemovedText, 224, 108, 117},
			{GitColor::GutterAdded, 106, 153, 85},
			{GitColor::GutterModified, 97, 175, 239},
			{GitColor::GutterRemoved, 224, 108, 117},
			{GitColor::GraphLane1, 97, 175, 239},
			{GitColor::GraphLane2, 152, 195, 121},
			{GitColor::GraphLane3, 229, 192, 123},
			{GitColor::GraphLane4, 198, 120, 221},
			{GitColor::GraphLane5, 86, 182, 194},
			{GitColor::GraphLane6, 224, 108, 117},
		};

		static_assert(std::size(kDarkGit) == static_cast<std::size_t>(GitColor::Count));

		constexpr SessionEntry kDarkSession[] = {
			{SessionColor::Read, 97, 175, 239},
			{SessionColor::Edit, 229, 192, 123},
			{SessionColor::Run, 152, 195, 121},
			{SessionColor::Agent, 198, 120, 221},
			{SessionColor::Web, 86, 182, 194},
			{SessionColor::Other, 127, 132, 142},
			{SessionColor::Failed, 224, 108, 117},
			{SessionColor::UserBlock, 62, 68, 81, 170},
			{SessionColor::ClaudeBlock, 40, 44, 52, 110},
			{SessionColor::CodeBlock, 24, 26, 31, 255},
			{SessionColor::Injected, 127, 132, 142},
		};

		static_assert(std::size(kDarkSession) == static_cast<std::size_t>(SessionColor::Count));

		constexpr SyntaxEntry kLightSyntax[] = {
			{TokenKind::Keyword, 0, 0, 255},
			{TokenKind::Number, 9, 134, 88},
			{TokenKind::String, 163, 21, 21},
			{TokenKind::Character, 163, 21, 21},
			{TokenKind::Comment, 0, 128, 0},
			{TokenKind::Preprocessor, 128, 128, 128},

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
			{EditorColor::CurrentLine, 0, 0, 0, 10},
			{EditorColor::Guide, 0, 0, 0, 20},
			{EditorColor::GutterFill, 255, 255, 255},
			{EditorColor::LineNumber, 180, 180, 180},
			{EditorColor::Caret, 36, 36, 36},
			{EditorColor::FindMatch, 250, 235, 130, 160},
			{EditorColor::FindCurrentMatch, 250, 200, 60, 220},
			{EditorColor::Lens, 160, 160, 160},
			{EditorColor::MatchingBracket, 176, 196, 222, 179},
			{EditorColor::UnmatchedBracket, 231, 76, 60, 128},
			{EditorColor::DiagnosticError, 200, 40, 40},
			{EditorColor::DiagnosticWarning, 170, 120, 20},
		};

		static_assert(std::size(kLightEditor) == static_cast<std::size_t>(EditorColor::Count));

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

		constexpr GitEntry kLightGit[] = {
			{GitColor::DiffAddedLine, 200, 235, 200, 140},
			{GitColor::DiffRemovedLine, 245, 200, 200, 140},
			{GitColor::DiffAddedText, 34, 102, 34},
			{GitColor::DiffRemovedText, 150, 30, 30},
			{GitColor::GutterAdded, 40, 120, 40},
			{GitColor::GutterModified, 30, 100, 180},
			{GitColor::GutterRemoved, 170, 40, 40},
			{GitColor::GraphLane1, 30, 100, 180},
			{GitColor::GraphLane2, 40, 120, 40},
			{GitColor::GraphLane3, 170, 120, 20},
			{GitColor::GraphLane4, 130, 60, 170},
			{GitColor::GraphLane5, 20, 130, 140},
			{GitColor::GraphLane6, 170, 40, 40},
		};

		static_assert(std::size(kLightGit) == static_cast<std::size_t>(GitColor::Count));

		constexpr SessionEntry kLightSession[] = {
			{SessionColor::Read, 30, 100, 180},
			{SessionColor::Edit, 170, 120, 20},
			{SessionColor::Run, 40, 120, 40},
			{SessionColor::Agent, 130, 60, 170},
			{SessionColor::Web, 20, 130, 140},
			{SessionColor::Other, 120, 120, 120},
			{SessionColor::Failed, 170, 40, 40},
			{SessionColor::UserBlock, 222, 230, 242, 255},
			{SessionColor::ClaudeBlock, 246, 246, 246, 255},
			{SessionColor::CodeBlock, 236, 236, 236, 255},
			{SessionColor::Injected, 130, 130, 130},
		};

		static_assert(std::size(kLightSession) == static_cast<std::size_t>(SessionColor::Count));

		ResolvedScheme Build(std::span<const SyntaxEntry> syntax, std::span<const EditorEntry> editor,
							 std::span<const ChromeEntry> chrome, std::span<const GitEntry> git,
							 std::span<const SessionEntry> session)
		{
			ResolvedScheme scheme;

			for (const ChromeEntry& entry : chrome)
			{
				scheme.Chrome[static_cast<std::size_t>(entry.Color)] =
					ColorFromBytes(entry.Red, entry.Green, entry.Blue, entry.Alpha);
			}

			for (const GitEntry& entry : git)
			{
				scheme.Git[static_cast<std::size_t>(entry.Color)] =
					ColorFromBytes(entry.Red, entry.Green, entry.Blue, entry.Alpha);
			}

			for (const SessionEntry& entry : session)
			{
				scheme.Session[static_cast<std::size_t>(entry.Color)] =
					ColorFromBytes(entry.Red, entry.Green, entry.Blue, entry.Alpha);
			}

			for (const EditorEntry& entry : editor)
			{
				scheme.Editor[static_cast<std::size_t>(entry.Color)] =
					ColorFromBytes(entry.Red, entry.Green, entry.Blue, entry.Alpha);
			}

			const Core::Color& plainTextColor =
				scheme.Editor[static_cast<std::size_t>(EditorColor::Text)];
			scheme.Syntax.fill(plainTextColor);

			for (const SyntaxEntry& entry : syntax)
			{
				scheme.Syntax[static_cast<std::size_t>(entry.Kind)] =
					ColorFromBytes(entry.Red, entry.Green, entry.Blue, entry.Alpha);
			}

			return scheme;
		}

		ResolvedScheme MakeDark()
		{
			return Build(kDarkSyntax, kDarkEditor, kDarkChrome, kDarkGit, kDarkSession);
		}

		ResolvedScheme MakeLight()
		{
			return Build(kLightSyntax, kLightEditor, kLightChrome, kLightGit, kLightSession);
		}
	}

	const ResolvedScheme& DarkScheme()
	{
		static const ResolvedScheme scheme = MakeDark();

		return scheme;
	}

	const ResolvedScheme& LightScheme()
	{
		static const ResolvedScheme scheme = MakeLight();

		return scheme;
	}

	namespace {

		struct BuiltInName
		{
			BuiltInScheme Scheme;
			std::string_view Name;
		};

		constexpr BuiltInName kBuiltInNames[] = {
			{BuiltInScheme::Dark, "dark"},
			{BuiltInScheme::Light, "light"},
			{BuiltInScheme::Graphite, "graphite"},
			{BuiltInScheme::GraphiteBright, "graphite-bright"},
			{BuiltInScheme::GraphiteWarm, "graphite-warm"},
			{BuiltInScheme::GraphiteCards, "graphite-cards"},
			{BuiltInScheme::GraphiteLight, "graphite-light"},
			{BuiltInScheme::GruvboxDarkHard, "gruvbox-dark-hard"},
			{BuiltInScheme::GruvboxDarkMedium, "gruvbox-dark-medium"},
			{BuiltInScheme::GruvboxDarkSoft, "gruvbox-dark-soft"},
			{BuiltInScheme::GruvboxLightHard, "gruvbox-light-hard"},
			{BuiltInScheme::GruvboxLightMedium, "gruvbox-light-medium"},
			{BuiltInScheme::GruvboxLightSoft, "gruvbox-light-soft"},
		};

		static_assert(std::size(kBuiltInNames) == kBuiltInSchemeCount);

		constexpr std::array<BuiltInScheme, kBuiltInSchemeCount> kEveryBuiltIn = [] {
			std::array<BuiltInScheme, kBuiltInSchemeCount> every{};

			for (std::size_t index = 0; index < every.size(); ++index)
			{
				every[index] = kBuiltInNames[index].Scheme;
			}

			return every;
		}();
	}

	std::span<const BuiltInScheme> AllBuiltInSchemes()
	{
		return kEveryBuiltIn;
	}

	const ResolvedScheme& SchemeFor(BuiltInScheme scheme)
	{
		switch (scheme)
		{
		case BuiltInScheme::Dark:
			return DarkScheme();
		case BuiltInScheme::Light:
			return LightScheme();
		case BuiltInScheme::Count:
			return DarkScheme();
		default:
			break;
		}

		// WARNING: built once per scheme, on first use, and never moved: callers hold the reference.
		static std::array<std::optional<ResolvedScheme>, kBuiltInSchemeCount> derived;

		std::optional<ResolvedScheme>& slot = derived[static_cast<std::size_t>(scheme)];

		if (!slot)
		{
			slot = BuildDerivedScheme(scheme);
		}

		return *slot;
	}

	std::string_view NameOf(BuiltInScheme scheme)
	{
		for (const BuiltInName& entry : kBuiltInNames)
		{
			if (entry.Scheme == scheme)
			{
				return entry.Name;
			}
		}

		return {};
	}

	std::optional<BuiltInScheme> ParseBuiltInName(std::string_view name)
	{
		for (const BuiltInName& entry : kBuiltInNames)
		{
			if (entry.Name == name)
			{
				return entry.Scheme;
			}
		}

		return std::nullopt;
	}
}
