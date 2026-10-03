#include "Theme/SchemeDraft.h"

#include "Core/Base/Log.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/SchemeStore.h"

#include <utility>

namespace Graphite {

	namespace {

		std::string DefaultName() { return std::string(NameOf(BuiltInScheme::Dark)); }
	}

	std::string UniqueSchemeName(const std::filesystem::path& directory, std::string_view base)
	{
		const std::string stem = std::string(base) + "-copy";

		std::error_code ec;

		// Guards against a built-in's name as well as an existing file: SaveScheme refuses a
		// built-in name, so proposing one would produce a copy that cannot be saved.
		for (std::size_t suffix = 1; suffix < 1000; ++suffix)
		{
			std::string candidate = suffix == 1 ? stem : stem + "-" + std::to_string(suffix);

			if (!ParseBuiltInName(candidate) &&
				!std::filesystem::exists(directory / (candidate + ".json"), ec))
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

		// Deliberately not Select(m_Selected) after initialising m_Selected in the list above:
		// that hands Select a view of the very string it assigns to, which is self-assignment
		// through a std::string_view.
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

	bool SchemeDraft::IsReadOnly() const { return ParseBuiltInName(m_Selected).has_value(); }

	void SchemeDraft::Resolve()
	{
		m_Preview = Graphite::Resolve(m_Scheme, SchemeFor(m_Scheme.Base));
	}

	void SchemeDraft::Select(std::string_view name)
	{
		// Empty means "never chosen" — the state a fresh graphite.json is in — and resolves to the
		// dark built-in, matching what ResolveNamed already does with an empty name. Without this
		// the first run would look for a file called ".json".
		m_Selected = name.empty() ? DefaultName() : std::string(name);

		if (const std::optional<BuiltInScheme> built = ParseBuiltInName(m_Selected))
		{
			// A built-in has no overrides by definition: it IS the base.
			m_Scheme = ColorScheme{};
			m_Scheme.Base = *built;
		}
		else
		{
			m_Scheme = LoadScheme(m_Directory / (m_Selected + ".json"));
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
			return false;
		}

		return true;
	}

	bool SchemeDraft::Duplicate()
	{
		const std::string name = UniqueSchemeName(m_Directory, m_Selected);

		// Saved from the scheme currently held, so a built-in duplicates as an empty override
		// set over that base, and a user scheme duplicates with its overrides intact.
		if (const Core::Result<void> saved = SaveScheme(m_Directory, name, m_Scheme); !saved)
		{
			APP_WARN("Could not duplicate colour scheme '{}' as '{}': {}", m_Selected, name,
					 saved.GetError().Message);
			return false;
		}

		Rebuild();
		Select(name);

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
			return false;
		}

		Rebuild();
		Select(NameOf(fallback));

		return true;
	}
}
