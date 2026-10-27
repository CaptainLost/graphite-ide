#include <doctest/doctest.h>

#include "Editor/Authors/AuthorText.h"

#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

using Graphite::AuthorTextParts;
using Graphite::FitAuthorText;

namespace {

	float CountCodePoints(std::string_view text)
	{
		float count = 0.0f;

		for (const char c : text)
		{
			const auto byte = static_cast<unsigned char>(c);

			if (byte < 0x80 || byte > 0xBF)
			{
				count += 1.0f;
			}
		}

		return count;
	}

	bool IsValidUtf8(std::string_view text)
	{
		std::size_t i = 0;

		while (i < text.size())
		{
			const auto byte = static_cast<unsigned char>(text[i]);
			std::size_t length = 1;

			if (byte >= 0xF0)
			{
				length = 4;
			}
			else if (byte >= 0xE0)
			{
				length = 3;
			}
			else if (byte >= 0xC0)
			{
				length = 2;
			}
			else if (byte >= 0x80)
			{
				return false;
			}

			if (i + length > text.size())
			{
				return false;
			}

			for (std::size_t k = 1; k < length; ++k)
			{
				if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
				{
					return false;
				}
			}

			i += length;
		}

		return true;
	}

	AuthorTextParts AnnaParts()
	{
		return {"Anna Kowalczyk, 3 months ago", "Anna Kowalczyk",
				"Iterate a copy so handlers can unsubscribe while publishing"};
	}

	std::optional<std::string> Fit(const AuthorTextParts& parts, float available)
	{
		return FitAuthorText(parts, available, CountCodePoints);
	}

	Core::Ref<const Graphite::HeadBlame> MakeHead(const Core::Oid& commit, const Core::Signature& author,
												  const std::string& summary)
	{
		auto blame = std::make_shared<Core::Blame>();
		blame->LineCount = 1;
		blame->Hunks = {{1, 1, commit, "a.txt", 1}};
		auto& entry = blame->Commits[commit];
		entry.Id = commit;
		entry.Author = author;
		entry.Summary = summary;

		auto head = std::make_shared<Graphite::HeadBlame>();
		head->Tracked = true;
		head->Blame = std::move(blame);
		return head;
	}
}

TEST_CASE("Author text shows everything when it fits")
{
	const auto parts = AnnaParts();
	CHECK(Fit(parts, 100.0f) == parts.Lead + " \xE2\x80\xA2 " + parts.Title);
}

TEST_CASE("Author text cuts the title first")
{
	const auto text = Fit(AnnaParts(), 50.0f);
	REQUIRE(text.has_value());
	CHECK(text->starts_with("Anna Kowalczyk, 3 months ago \xE2\x80\xA2 Iterate"));
	CHECK(text->ends_with(Graphite::kEllipsis));
	CHECK(CountCodePoints(*text) <= 50.0f);
	CHECK(CountCodePoints(*text) >= 49.0f);
}

TEST_CASE("Author text drops the title when too little of it fits")
{
	CHECK(Fit(AnnaParts(), 38.0f) == "Anna Kowalczyk, 3 months ago");
}

TEST_CASE("Author text falls back to the name alone")
{
	CHECK(Fit(AnnaParts(), 20.0f) == "Anna Kowalczyk");
}

TEST_CASE("Author text shows nothing when the name does not fit")
{
	CHECK_FALSE(Fit(AnnaParts(), 10.0f).has_value());
}

TEST_CASE("Author text shortens an uncommitted line to You")
{
	const AuthorTextParts parts{"You", "You", "Uncommitted changes"};
	CHECK(Fit(parts, 30.0f) == "You \xE2\x80\xA2 Uncommitted changes");
	CHECK(Fit(parts, 10.0f) == "You");
	CHECK_FALSE(Fit(parts, 2.0f).has_value());
}

TEST_CASE("Author text measures a long title a few times only")
{
	constexpr int kTitleWords = 100;
	constexpr int kMostMeasures = 16;

	AuthorTextParts parts{"Anna", "Anna", ""};

	for (int word = 0; word < kTitleWords; ++word)
	{
		parts.Title += "word ";
	}

	int measures = 0;
	const auto measure = [&measures](std::string_view text) {
		++measures;
		return CountCodePoints(text);
	};

	const std::optional<std::string> text = FitAuthorText(parts, 40.0f, measure);

	REQUIRE(text.has_value());
	CHECK(CountCodePoints(*text) <= 40.0f);
	CHECK(CountCodePoints(*text) >= 39.0f);
	CHECK(measures <= kMostMeasures);
}

TEST_CASE("Author text shows the lead alone for a commit with no title")
{
	const AuthorTextParts parts{"Anna Kowalczyk, 3 months ago", "Anna Kowalczyk", ""};

	CHECK(Fit(parts, 100.0f) == "Anna Kowalczyk, 3 months ago");
	CHECK(Fit(parts, 20.0f) == "Anna Kowalczyk");
}

TEST_CASE("Author text cuts a title on a code point boundary")
{
	const AuthorTextParts parts{"Anna", "Anna", "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 ja\xC5\xBA\xC5\x84"};

	for (float available = 14.0f; available < 30.0f; available += 1.0f)
	{
		const auto text = Fit(parts, available);

		if (text.has_value())
		{
			CHECK(IsValidUtf8(*text));
			CHECK(CountCodePoints(*text) <= available);
		}
	}
}

TEST_CASE("Author text parts name the author and age of a committed line")
{
	const auto now = std::chrono::sys_seconds{std::chrono::seconds{1'800'000'000}};
	Core::Oid commit;
	commit.Bytes.fill(0xA1);
	Core::Signature author;
	author.Name = "Anna Kowalczyk";
	author.When = now - std::chrono::hours{24 * 90};

	Graphite::LineAuthors authors;
	authors.Head = MakeHead(commit, author, "Fix the thing");
	authors.Lines = {{Graphite::LineAuthorKind::Committed, 0, 1}};

	const auto parts = Graphite::AuthorTextPartsOf(authors, 0, now);
	REQUIRE(parts.has_value());
	CHECK(parts->Lead == Graphite::AuthorName(author) + ", " + Graphite::RelativeAge(author.When, now));
	CHECK(parts->Name == "Anna Kowalczyk");
	CHECK(parts->Title == "Fix the thing");

	CHECK_FALSE(Graphite::AuthorTextPartsOf(authors, 1, now).has_value());
}

TEST_CASE("Author text parts say You for an uncommitted line")
{
	const auto now = std::chrono::sys_seconds{std::chrono::seconds{1'800'000'000}};
	Core::Oid commit;
	commit.Bytes.fill(0xA1);

	Graphite::LineAuthors authors;
	authors.Head = MakeHead(commit, {}, "x");
	authors.Lines = {{Graphite::LineAuthorKind::Uncommitted, 0, std::nullopt},
					 {Graphite::LineAuthorKind::Unknown, 0, std::nullopt}};

	const auto parts = Graphite::AuthorTextPartsOf(authors, 0, now);
	REQUIRE(parts.has_value());
	CHECK(parts->Lead == "You");
	CHECK(parts->Name == "You");
	CHECK(parts->Title == "Uncommitted changes");

	CHECK_FALSE(Graphite::AuthorTextPartsOf(authors, 1, now).has_value());
}
