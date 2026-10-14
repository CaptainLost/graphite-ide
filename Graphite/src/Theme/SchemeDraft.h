#pragma once

#include "Theme/ColorScheme.h"
#include "UI/ToastEvent.h"

#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class SchemeDraft
	{
	public:
		explicit SchemeDraft(std::filesystem::path directory);

		const std::vector<std::string>& GetNames() const { return m_Names; }

		const std::string& GetSelected() const { return m_Selected; }

		bool IsReadOnly() const;

		void Select(std::string_view name);

		const ResolvedScheme& GetPreview() const { return m_Preview; }

		bool IsOverridden(TokenKind kind) const;
		bool IsOverridden(EditorColor color) const;
		bool IsOverridden(ChromeColor color) const;
		bool IsOverridden(GitColor color) const;
		bool IsOverridden(SessionColor color) const;

		void SetColor(TokenKind kind, const Core::Color& color);
		void SetColor(EditorColor which, const Core::Color& color);
		void SetColor(ChromeColor which, const Core::Color& color);
		void SetColor(GitColor which, const Core::Color& color);
		void SetColor(SessionColor which, const Core::Color& color);

		void Revert(TokenKind kind);
		void Revert(EditorColor which);
		void Revert(ChromeColor which);
		void Revert(GitColor which);
		void Revert(SessionColor which);

		bool Save();

		bool Duplicate();

		std::optional<std::string> NameProblem(std::string_view name) const;

		bool RenameSelected(std::string_view name);

		bool DeleteSelected();

		std::optional<ToastEvent> TakeFailure();

	private:
		void Rebuild();
		void Resolve();
		void Fail(std::string title, std::string message);

		std::filesystem::path m_Directory;

		std::deque<ToastEvent> m_Failures;

		std::vector<std::string> m_Names;

		std::string m_Selected;

		ColorScheme m_Scheme;

		ResolvedScheme m_Preview;
	};

	std::string UniqueSchemeName(const std::filesystem::path& directory, std::string_view base);
}
