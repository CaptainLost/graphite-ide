#include <doctest/doctest.h>

#include "Theme/ColorScheme.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace {

	// A base with every entry filled, so a resolution against it can only produce a colour that
	// came from one side or the other.
	Graphite::ResolvedScheme FlatBase(float value)
	{
		Graphite::ResolvedScheme base;

		base.Syntax.fill(Core::Color{value, value, value, 1.0f});
		base.Editor.fill(Core::Color{value, value, value, 1.0f});

		return base;
	}
}

TEST_CASE("An overridden colour wins over the base")
{
	Graphite::ColorScheme scheme;
	scheme.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)] =
		Core::Color{1.0f, 0.0f, 0.0f, 1.0f};

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, FlatBase(0.5f));

	CHECK(resolved.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)].R ==
		  doctest::Approx(1.0f));
}

TEST_CASE("A colour the scheme omits comes from the base")
{
	const Graphite::ColorScheme scheme;

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, FlatBase(0.5f));

	CHECK(resolved.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)].R ==
		  doctest::Approx(0.5f));
	CHECK(resolved.Editor[static_cast<std::size_t>(Graphite::EditorColor::Selection)].R ==
		  doctest::Approx(0.5f));
}

// The degradation rule under test. When a later stage adds a token kind, every scheme a user
// has already written must keep working — and this is what proves it, because a scheme built
// today knows nothing about a kind added tomorrow.
TEST_CASE("A token kind the scheme never mentions is filled from the base")
{
	const Graphite::ColorScheme scheme;

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, FlatBase(0.25f));

	for (std::size_t kind = 0; kind < static_cast<std::size_t>(Graphite::TokenKind::Count);
		 ++kind)
	{
		CHECK(resolved.Syntax[kind].R == doctest::Approx(0.25f));
	}
}

// Checked within each set, not across them. TokenKind::Text and EditorColor::Text are both
// "text" on purpose: the file prefixes them as syntax.text and editor.text, so they never
// collide as keys. What must not happen is two names colliding inside one set, where the
// prefix cannot separate them and the second would silently overwrite the first.
TEST_CASE("Every colour has a key name, and no two in a set share one")
{
	std::vector<std::string_view> syntax;

	for (std::size_t kind = 0; kind < static_cast<std::size_t>(Graphite::TokenKind::Count);
		 ++kind)
	{
		syntax.push_back(Graphite::NameOf(static_cast<Graphite::TokenKind>(kind)));
	}

	std::vector<std::string_view> editor;

	for (std::size_t color = 0; color < static_cast<std::size_t>(Graphite::EditorColor::Count);
		 ++color)
	{
		editor.push_back(Graphite::NameOf(static_cast<Graphite::EditorColor>(color)));
	}

	for (const std::vector<std::string_view>* set : {&syntax, &editor})
	{
		for (const std::string_view name : *set)
		{
			CHECK_FALSE(name.empty());
		}

		std::vector<std::string_view> sorted = *set;
		std::sort(sorted.begin(), sorted.end());

		CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
	}
}
