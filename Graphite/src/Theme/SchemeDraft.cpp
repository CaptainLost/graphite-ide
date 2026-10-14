#include "Theme/SchemeDraft.h"

#include "Core/Base/Log.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/SchemeStore.h"

#include <format>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::size_t kLastCopySuffix = 999;
		constexpr std::size_t kUnnumberedCopySuffix = 1;

		std::string DefaultName()
		{
			return std::string(NameOf(BuiltInScheme::Dark));
		}
	}

	std::string UniqueSchemeName(const std::filesystem::path& directory, std::string_view base)
	{
		const std::string stem = std::string(base) + "-copy";

		std::error_code errorCode;

		for (std::size_t suffix = kUnnumberedCopySuffix; suffix <= kLastCopySuffix; ++suffix)
		{
			std::string candidate = suffix == kUnnumberedCopySuffix ? stem : stem + "-" + std::to_string(suffix);

			const bool isBuiltInName = ParseBuiltInName(candidate).has_value();

			if (!isBuiltInName && !std::filesystem::exists(SchemeFile(directory, candidate), errorCode))
			{
				return candidate;
			}
		}

		return stem;
	}

	SchemeDraft::SchemeDraft(std::filesystem::path directory)
		: m_Directory(std::move(directory))
	{
		Rebuild();

		Select(DefaultName());
	}

	void SchemeDraft::Rebuild()
	{
		m_Names.clear();
		m_Names.push_back(std::string(NameOf(BuiltInScheme::Dark)));
		m_Names.push_back(std::string(NameOf(BuiltInScheme::Light)));

		for (std::string& name : ListSchemes(m_Directory))
		{
			m_Names.push_back(std::move(name));
		}
	}

	bool SchemeDraft::IsReadOnly() const
	{
		return ParseBuiltInName(m_Selected).has_value();
	}

	void SchemeDraft::Resolve()
	{
		m_Preview = Graphite::ResolveSchemeOverBase(m_Scheme, SchemeFor(m_Scheme.Base));
	}

	void SchemeDraft::Select(std::string_view name)
	{
		// WARNING: name must not view m_Selected, which is assigned before name is read again.
		m_Selected = name.empty() ? DefaultName() : std::string(name);

		if (const std::optional<BuiltInScheme> built = ParseBuiltInName(m_Selected))
		{
			m_Scheme = ColorScheme{};
			m_Scheme.Base = *built;
		}
		else
		{
			m_Scheme = LoadScheme(SchemeFile(m_Directory, m_Selected));
		}

		Resolve();
	}

	bool SchemeDraft::IsOverridden(TokenKind kind) const
	{
		return m_Scheme.Syntax[static_cast<std::size_t>(kind)].has_value();
	}

	bool SchemeDraft::IsOverridden(EditorColor color) const
	{
		return m_Scheme.Editor[static_cast<std::size_t>(color)].has_value();
	}

	bool SchemeDraft::IsOverridden(ChromeColor color) const
	{
		return m_Scheme.Chrome[static_cast<std::size_t>(color)].has_value();
	}

	bool SchemeDraft::IsOverridden(GitColor color) const
	{
		return m_Scheme.Git[static_cast<std::size_t>(color)].has_value();
	}

	bool SchemeDraft::IsOverridden(SessionColor color) const
	{
		return m_Scheme.Session[static_cast<std::size_t>(color)].has_value();
	}

	void SchemeDraft::SetColor(TokenKind kind, const Core::Color& color)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Syntax[static_cast<std::size_t>(kind)] = color;
		Resolve();
	}

	void SchemeDraft::SetColor(EditorColor which, const Core::Color& color)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Editor[static_cast<std::size_t>(which)] = color;
		Resolve();
	}

	void SchemeDraft::SetColor(ChromeColor which, const Core::Color& color)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Chrome[static_cast<std::size_t>(which)] = color;

		Resolve();
	}

	void SchemeDraft::SetColor(GitColor which, const Core::Color& color)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Git[static_cast<std::size_t>(which)] = color;

		Resolve();
	}

	void SchemeDraft::SetColor(SessionColor which, const Core::Color& color)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Session[static_cast<std::size_t>(which)] = color;

		Resolve();
	}

	void SchemeDraft::Revert(TokenKind kind)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Syntax[static_cast<std::size_t>(kind)].reset();
		Resolve();
	}

	void SchemeDraft::Revert(EditorColor which)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Editor[static_cast<std::size_t>(which)].reset();
		Resolve();
	}

	void SchemeDraft::Revert(ChromeColor which)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Chrome[static_cast<std::size_t>(which)].reset();

		Resolve();
	}

	void SchemeDraft::Revert(GitColor which)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Git[static_cast<std::size_t>(which)].reset();

		Resolve();
	}

	void SchemeDraft::Revert(SessionColor which)
	{
		if (IsReadOnly())
		{
			return;
		}

		m_Scheme.Session[static_cast<std::size_t>(which)].reset();

		Resolve();
	}

	bool SchemeDraft::Save()
	{
		if (IsReadOnly())
		{
			return false;
		}

		if (const Core::Result<void> saved = SaveScheme(m_Directory, m_Selected, m_Scheme); !saved)
		{
			APP_WARN("Could not save colour scheme '{}': {}", m_Selected, saved.GetError().Message);
			Fail("Could not save scheme", std::format("'{}': {}", m_Selected, saved.GetError().Message));
			return false;
		}

		return true;
	}

	bool SchemeDraft::Duplicate()
	{
		const std::string name = UniqueSchemeName(m_Directory, m_Selected);

		if (const Core::Result<void> saved = SaveScheme(m_Directory, name, m_Scheme); !saved)
		{
			APP_WARN("Could not duplicate colour scheme '{}' as '{}': {}", m_Selected, name,
					 saved.GetError().Message);
			Fail("Could not duplicate scheme", std::format("'{}' as '{}': {}", m_Selected, name, saved.GetError().Message));
			return false;
		}

		Rebuild();
		Select(name);

		return true;
	}

	std::optional<std::string> SchemeDraft::NameProblem(std::string_view name) const
	{
		std::vector<std::string> namesExceptSelected;

		for (const std::string& existing : m_Names)
		{
			if (existing != m_Selected)
			{
				namesExceptSelected.push_back(existing);
			}
		}

		return SchemeNameProblem(name, namesExceptSelected);
	}

	bool SchemeDraft::RenameSelected(std::string_view name)
	{
		if (IsReadOnly() || name == m_Selected || NameProblem(name).has_value())
		{
			return false;
		}

		if (const Core::Result<void> renamed = RenameScheme(m_Directory, m_Selected, name); !renamed)
		{
			APP_WARN("Could not rename colour scheme '{}' to '{}': {}", m_Selected, name, renamed.GetError().Message);
			return false;
		}

		const std::string renamedTo(name);

		Rebuild();
		Select(renamedTo);

		return true;
	}

	bool SchemeDraft::DeleteSelected()
	{
		if (IsReadOnly())
		{
			return false;
		}

		const BuiltInScheme fallback = m_Scheme.Base;

		if (const Core::Result<void> deleted = DeleteScheme(m_Directory, m_Selected); !deleted)
		{
			APP_WARN("Could not delete colour scheme '{}': {}", m_Selected, deleted.GetError().Message);
			Fail("Could not delete scheme", std::format("'{}': {}", m_Selected, deleted.GetError().Message));
			return false;
		}

		Rebuild();
		Select(NameOf(fallback));

		return true;
	}

	std::optional<ToastEvent> SchemeDraft::TakeFailure()
	{
		if (m_Failures.empty())
		{
			return std::nullopt;
		}

		ToastEvent failure = std::move(m_Failures.front());
		m_Failures.pop_front();
		return failure;
	}

	void SchemeDraft::Fail(std::string title, std::string message)
	{
		m_Failures.push_back(ApplicationToast(Core::ToastKind::Error, std::move(title), std::move(message)));
	}
}
