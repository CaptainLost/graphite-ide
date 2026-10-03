#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"

#include <algorithm>
#include <optional>
#include <ostream>
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

// The point of one switch per enum: a key and its label cannot drift apart, because there is
// no second place to edit. This test would still pass with two switches — what it actually
// guards is that every enumerator has a label at all, and that no two rows in one group would
// render under the same text.
TEST_CASE("Every colour has a label, and no two in a group share one")
{
	std::vector<std::string_view> syntax;

	for (std::size_t kind = 0; kind < static_cast<std::size_t>(Graphite::TokenKind::Count);
		 ++kind)
	{
		syntax.push_back(Graphite::LabelOf(static_cast<Graphite::TokenKind>(kind)));
	}

	std::vector<std::string_view> editor;

	for (std::size_t color = 0; color < static_cast<std::size_t>(Graphite::EditorColor::Count);
		 ++color)
	{
		editor.push_back(Graphite::LabelOf(static_cast<Graphite::EditorColor>(color)));
	}

	for (const std::vector<std::string_view>* group : {&syntax, &editor})
	{
		for (const std::string_view label : *group)
		{
			CHECK_FALSE(label.empty());
		}

		std::vector<std::string_view> sorted = *group;
		std::sort(sorted.begin(), sorted.end());

		CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
	}
}

// NameOf must keep returning the file key after the refactor, or every scheme already on disk
// stops loading. This pins the two that are camelCase, which are the ones a label refactor
// would be tempted to "tidy".
TEST_CASE("Keys keep their file spelling after gaining labels")
{
	CHECK(Graphite::NameOf(Graphite::EditorColor::CurrentLine) == "currentLine");
	CHECK(Graphite::NameOf(Graphite::EditorColor::GutterFill) == "gutterFill");
	CHECK(Graphite::NameOf(Graphite::EditorColor::LineNumber) == "lineNumber");
	CHECK(Graphite::LabelOf(Graphite::EditorColor::CurrentLine) == "Current line");
}

TEST_CASE("Resolve takes a chrome override and leaves the rest to the base")
{
	Graphite::ResolvedScheme base;
	base.Chrome.fill(Core::Color{0.0f, 0.0f, 0.0f, 1.0f});

	Graphite::ColorScheme scheme;
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)] =
		Core::Color{1.0f, 0.0f, 0.0f, 1.0f};

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, base);

	CHECK(resolved.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)].R ==
		  doctest::Approx(1.0f));
	CHECK(resolved.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].R ==
		  doctest::Approx(0.0f));
}

// The key is what the file stores and the label is what the settings row shows. A colour
// missing either is a colour that is either unwritable or invisible.
TEST_CASE("Every chrome colour has a key and a label")
{
	for (std::size_t index = 0; index < static_cast<std::size_t>(Graphite::ChromeColor::Count);
		 ++index)
	{
		const Graphite::ColorNames names =
			Graphite::NamesOf(static_cast<Graphite::ChromeColor>(index));

		CHECK_FALSE(names.Key.empty());
		CHECK_FALSE(names.Label.empty());
	}
}

TEST_CASE("A git colour overrides its base and leaves the others alone")
{
	Graphite::ColorScheme scheme;
	scheme.Git[static_cast<std::size_t>(Graphite::GitColor::DiffAddedLine)] =
		Core::Color{0.1f, 0.2f, 0.3f, 1.0f};

	Graphite::ResolvedScheme base = Graphite::DarkScheme();
	const Core::Color untouched =
		base.Git[static_cast<std::size_t>(Graphite::GitColor::DiffRemovedLine)];

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, base);

	CHECK(resolved.Git[static_cast<std::size_t>(Graphite::GitColor::DiffAddedLine)].R == doctest::Approx(0.1f));
	CHECK(resolved.Git[static_cast<std::size_t>(Graphite::GitColor::DiffRemovedLine)].R == doctest::Approx(untouched.R));
}

TEST_CASE("Every git colour has a key and a label, and no two keys collide")
{
	std::vector<std::string_view> keys;

	for (std::size_t index = 0; index < static_cast<std::size_t>(Graphite::GitColor::Count);
		 ++index)
	{
		const Graphite::ColorNames names = Graphite::NamesOf(static_cast<Graphite::GitColor>(index));

		// A colour with no key cannot be written to a scheme file, and one with no label
		// shows as a blank row in the settings panel. Both are silent failures.
		CHECK_FALSE(names.Key.empty());
		CHECK_FALSE(names.Label.empty());

		keys.push_back(names.Key);
	}

	std::sort(keys.begin(), keys.end());
	CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());
}

TEST_CASE("A session colour overrides its base and leaves the others alone")
{
	Graphite::ColorScheme scheme;
	scheme.Session[static_cast<std::size_t>(Graphite::SessionColor::Read)] =
		Core::Color{0.1f, 0.2f, 0.3f, 1.0f};

	Graphite::ResolvedScheme base = Graphite::DarkScheme();
	const Core::Color untouched =
		base.Session[static_cast<std::size_t>(Graphite::SessionColor::Edit)];

	const Graphite::ResolvedScheme resolved = Graphite::Resolve(scheme, base);

	CHECK(resolved.Session[static_cast<std::size_t>(Graphite::SessionColor::Read)].R == doctest::Approx(0.1f));
	CHECK(resolved.Session[static_cast<std::size_t>(Graphite::SessionColor::Edit)].R == doctest::Approx(untouched.R));
}

TEST_CASE("Every session colour has a key and a label, and no two keys collide")
{
	std::vector<std::string_view> keys;

	for (std::size_t index = 0; index < static_cast<std::size_t>(Graphite::SessionColor::Count);
		 ++index)
	{
		const Graphite::ColorNames names =
			Graphite::NamesOf(static_cast<Graphite::SessionColor>(index));

		// A colour with no key cannot be written to a scheme file, and one with no label
		// shows as a blank row in the settings panel. Both are silent failures.
		CHECK_FALSE(names.Key.empty());
		CHECK_FALSE(names.Label.empty());

		keys.push_back(names.Key);
	}

	std::sort(keys.begin(), keys.end());
	CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());
}
