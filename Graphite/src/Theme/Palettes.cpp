#include "Theme/Palettes.h"

#include "Theme/SessionPalette.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {

		// The one conversion from Core::Color to ImU32 in this sub-project — everything else
		// reaches ImGui only through the palettes this builds.
		ImU32 ToImU32(const Core::Color& color)
		{
			const auto byte = [](float channel) {
				return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
			};

			return IM_COL32(byte(color.R), byte(color.G), byte(color.B), byte(color.A));
		}
	}

	SyntaxPalette ReadSyntaxPalette(const ResolvedScheme& scheme)
	{
		SyntaxPalette palette;

		for (std::size_t kind = 0; kind < scheme.Syntax.size(); ++kind)
		{
			palette.Colours[kind] = ToImU32(scheme.Syntax[kind]);
		}

		return palette;
	}

	EditorPalette ReadEditorPalette(const ResolvedScheme& scheme)
	{
		EditorPalette palette;

		palette.Background = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Background)]);
		palette.Text = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Text)]);
		palette.Number = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::LineNumber)]);
		palette.Selection = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Selection)]);
		palette.CurrentLine = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::CurrentLine)]);
		palette.Guide = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Guide)]);
		palette.GutterFill = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::GutterFill)]);
		palette.Caret = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Caret)]);
		palette.FindMatch = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::FindMatch)]);
		palette.FindCurrentMatch =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::FindCurrentMatch)]);
		palette.Lens = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Lens)]);
		palette.MatchingBracket =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::MatchingBracket)]);
		palette.UnmatchedBracket =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::UnmatchedBracket)]);
		palette.DiagnosticError =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::DiagnosticError)]);
		palette.DiagnosticWarning =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::DiagnosticWarning)]);
		palette.ChangeAdded = ToImU32(scheme.Git[static_cast<std::size_t>(GitColor::GutterAdded)]);
		palette.ChangeModified = ToImU32(scheme.Git[static_cast<std::size_t>(GitColor::GutterModified)]);
		palette.ChangeRemoved = ToImU32(scheme.Git[static_cast<std::size_t>(GitColor::GutterRemoved)]);
		palette.ChangeOldText = ToImU32(scheme.Git[static_cast<std::size_t>(GitColor::DiffRemovedText)]);
		palette.ChangeOldRemoved = ToImU32(scheme.Git[static_cast<std::size_t>(GitColor::DiffRemovedLine)]);

		return palette;
	}

	ImVec4 DangerColourOf(const ResolvedScheme& scheme)
	{
		return ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(scheme).DiagnosticError);
	}

	GitPalette ReadGitPalette(const ResolvedScheme& scheme)
	{
		const auto of = [&scheme](GitColor color) {
			return ToImU32(scheme.Git[static_cast<std::size_t>(color)]);
		};

		GitPalette palette;
		palette.DiffAddedLine = of(GitColor::DiffAddedLine);
		palette.DiffRemovedLine = of(GitColor::DiffRemovedLine);
		palette.DiffAddedText = of(GitColor::DiffAddedText);
		palette.DiffRemovedText = of(GitColor::DiffRemovedText);
		palette.GutterAdded = of(GitColor::GutterAdded);
		palette.GutterModified = of(GitColor::GutterModified);
		palette.GutterRemoved = of(GitColor::GutterRemoved);

		// Read through the enum rather than by arithmetic on GraphLane1, so adding a lane
		// to the enum without adding it here fails to compile instead of drawing black.
		palette.Lanes = {of(GitColor::GraphLane1), of(GitColor::GraphLane2),
						 of(GitColor::GraphLane3), of(GitColor::GraphLane4),
						 of(GitColor::GraphLane5), of(GitColor::GraphLane6)};

		return palette;
	}

	SessionPalette ReadSessionPalette(const ResolvedScheme& scheme)
	{
		const auto of = [&scheme](SessionColor color) {
			return ToImU32(scheme.Session[static_cast<std::size_t>(color)]);
		};

		SessionPalette palette;
		palette.Read = of(SessionColor::Read);
		palette.Edit = of(SessionColor::Edit);
		palette.Run = of(SessionColor::Run);
		palette.Agent = of(SessionColor::Agent);
		palette.Web = of(SessionColor::Web);
		palette.Other = of(SessionColor::Other);
		palette.Failed = of(SessionColor::Failed);
		palette.UserBlock = of(SessionColor::UserBlock);
		palette.ClaudeBlock = of(SessionColor::ClaudeBlock);
		palette.CodeBlock = of(SessionColor::CodeBlock);
		palette.Injected = of(SessionColor::Injected);
		palette.Accent = ToImU32(scheme.Chrome[static_cast<std::size_t>(ChromeColor::Accent)]);
		palette.TextDisabled =
			ToImU32(scheme.Chrome[static_cast<std::size_t>(ChromeColor::TextDisabled)]);
		return palette;
	}
}
