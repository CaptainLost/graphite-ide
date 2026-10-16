#include "Theme/Palettes.h"

#include "Core/Base/Color.h"
#include "Theme/SessionPalette.h"

namespace Graphite {

	namespace {

		ImU32 ToImU32(const Core::Color& color)
		{
			return IM_COL32(Core::ChannelToByte(color.R), Core::ChannelToByte(color.G),
							Core::ChannelToByte(color.B), Core::ChannelToByte(color.A));
		}

		ImU32 ImU32FromScheme(const ResolvedScheme& scheme, EditorColor slot)
		{
			return ToImU32(ColourFromScheme(scheme, slot));
		}

		ImU32 ImU32FromScheme(const ResolvedScheme& scheme, GitColor slot)
		{
			return ToImU32(ColourFromScheme(scheme, slot));
		}

		ImU32 ImU32FromScheme(const ResolvedScheme& scheme, SessionColor slot)
		{
			return ToImU32(ColourFromScheme(scheme, slot));
		}

		ImU32 ImU32FromScheme(const ResolvedScheme& scheme, ChromeColor slot)
		{
			return ToImU32(ColourFromScheme(scheme, slot));
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

		palette.Background = ImU32FromScheme(scheme, EditorColor::Background);
		palette.Text = ImU32FromScheme(scheme, EditorColor::Text);
		palette.Number = ImU32FromScheme(scheme, EditorColor::LineNumber);
		palette.Selection = ImU32FromScheme(scheme, EditorColor::Selection);
		palette.CurrentLine = ImU32FromScheme(scheme, EditorColor::CurrentLine);
		palette.Guide = ImU32FromScheme(scheme, EditorColor::Guide);
		palette.GutterFill = ImU32FromScheme(scheme, EditorColor::GutterFill);
		palette.Caret = ImU32FromScheme(scheme, EditorColor::Caret);
		palette.FindMatch = ImU32FromScheme(scheme, EditorColor::FindMatch);
		palette.FindCurrentMatch = ImU32FromScheme(scheme, EditorColor::FindCurrentMatch);
		palette.Lens = ImU32FromScheme(scheme, EditorColor::Lens);
		palette.MatchingBracket = ImU32FromScheme(scheme, EditorColor::MatchingBracket);
		palette.UnmatchedBracket = ImU32FromScheme(scheme, EditorColor::UnmatchedBracket);
		palette.DiagnosticError = ImU32FromScheme(scheme, EditorColor::DiagnosticError);
		palette.DiagnosticWarning = ImU32FromScheme(scheme, EditorColor::DiagnosticWarning);
		palette.ChangeAdded = ImU32FromScheme(scheme, GitColor::GutterAdded);
		palette.ChangeModified = ImU32FromScheme(scheme, GitColor::GutterModified);
		palette.ChangeRemoved = ImU32FromScheme(scheme, GitColor::GutterRemoved);
		palette.ChangeOldText = ImU32FromScheme(scheme, GitColor::DiffRemovedText);
		palette.ChangeOldRemoved = ImU32FromScheme(scheme, GitColor::DiffRemovedLine);

		return palette;
	}

	ImVec4 DangerColourOf(const ResolvedScheme& scheme)
	{
		return ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(scheme).DiagnosticError);
	}

	GitPalette ReadGitPalette(const ResolvedScheme& scheme)
	{
		GitPalette palette;
		palette.DiffAddedLine = ImU32FromScheme(scheme, GitColor::DiffAddedLine);
		palette.DiffRemovedLine = ImU32FromScheme(scheme, GitColor::DiffRemovedLine);
		palette.DiffAddedText = ImU32FromScheme(scheme, GitColor::DiffAddedText);
		palette.DiffRemovedText = ImU32FromScheme(scheme, GitColor::DiffRemovedText);
		palette.GutterAdded = ImU32FromScheme(scheme, GitColor::GutterAdded);
		palette.GutterModified = ImU32FromScheme(scheme, GitColor::GutterModified);
		palette.GutterRemoved = ImU32FromScheme(scheme, GitColor::GutterRemoved);
		palette.LineNumber = ImU32FromScheme(scheme, EditorColor::LineNumber);

		palette.Lanes = {ImU32FromScheme(scheme, GitColor::GraphLane1),
						 ImU32FromScheme(scheme, GitColor::GraphLane2),
						 ImU32FromScheme(scheme, GitColor::GraphLane3),
						 ImU32FromScheme(scheme, GitColor::GraphLane4),
						 ImU32FromScheme(scheme, GitColor::GraphLane5),
						 ImU32FromScheme(scheme, GitColor::GraphLane6)};

		return palette;
	}

	SessionPalette ReadSessionPalette(const ResolvedScheme& scheme)
	{
		SessionPalette palette;
		palette.Read = ImU32FromScheme(scheme, SessionColor::Read);
		palette.Edit = ImU32FromScheme(scheme, SessionColor::Edit);
		palette.Run = ImU32FromScheme(scheme, SessionColor::Run);
		palette.Agent = ImU32FromScheme(scheme, SessionColor::Agent);
		palette.Web = ImU32FromScheme(scheme, SessionColor::Web);
		palette.Other = ImU32FromScheme(scheme, SessionColor::Other);
		palette.Failed = ImU32FromScheme(scheme, SessionColor::Failed);
		palette.UserBlock = ImU32FromScheme(scheme, SessionColor::UserBlock);
		palette.ClaudeBlock = ImU32FromScheme(scheme, SessionColor::ClaudeBlock);
		palette.CodeBlock = ImU32FromScheme(scheme, SessionColor::CodeBlock);
		palette.Injected = ImU32FromScheme(scheme, SessionColor::Injected);
		palette.Accent = ImU32FromScheme(scheme, ChromeColor::Accent);
		palette.TextDisabled = ImU32FromScheme(scheme, ChromeColor::TextDisabled);
		return palette;
	}
}
