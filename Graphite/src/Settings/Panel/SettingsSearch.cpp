#include "Settings/Panel/SettingsSearch.h"

#include "UI/TypographicMarks.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		struct SearchTerms
		{
			std::vector<std::string> Words;
			std::string Query;
		};

		constexpr std::size_t kMostFindingsShown = 8;

		bool AnyTextContains(std::span<const std::string_view> texts, std::string_view loweredWord)
		{
			for (const std::string_view text : texts)
			{
				if (Core::LoweredTextContains(text, loweredWord))
				{
					return true;
				}
			}

			return false;
		}

		std::vector<std::string_view> SearchedTextsOf(const SettingsRow& row, std::string_view cardHeading,
													  std::span<const std::string_view> pageTitles)
		{
			std::vector<std::string_view> texts{row.Title, row.Description, cardHeading};
			texts.insert(texts.end(), pageTitles.begin(), pageTitles.end());

			for (const std::string& keyword : row.Keywords)
			{
				texts.push_back(keyword);
			}

			return texts;
		}

		SettingsRow RowForTheRest(std::size_t count, const std::string& query)
		{
			return SettingsRow{.Key = "more",
							   .Title = Core::Counted(count, "more match", "more matches"),
							   .Description = "Open the page to see them all.",
							   .OpenWith = query};
		}

		std::vector<SettingsRow> ShownOf(std::vector<SettingsRow> findings, const std::string& query)
		{
			if (findings.size() <= kMostFindingsShown)
			{
				return findings;
			}

			const std::size_t hidden = findings.size() - kMostFindingsShown;
			findings.resize(kMostFindingsShown);
			findings.push_back(RowForTheRest(hidden, query));

			return findings;
		}

		std::vector<SettingsRow> MatchRow(const SettingsRow& row, std::string_view cardHeading, std::span<const std::string_view> pageTitles,
										  const SearchTerms& terms)
		{
			if (row.FindInside)
			{
				std::vector<SettingsRow> findings = row.FindInside(terms.Words);

				if (!findings.empty())
				{
					return ShownOf(std::move(findings), terms.Query);
				}
			}

			if (EveryWordFound(terms.Words, SearchedTextsOf(row, cardHeading, pageTitles)))
			{
				return {row};
			}

			return {};
		}

		SettingsCardHits MatchCard(const SettingsCard& card, std::span<const std::string_view> pageTitles, const SearchTerms& terms)
		{
			SettingsCardHits cardHits{.Heading = card.Heading, .Note = card.Note};

			for (const SettingsRow& row : card.Rows)
			{
				std::vector<SettingsRow> hits = MatchRow(row, card.Heading, pageTitles, terms);
				cardHits.Rows.insert(cardHits.Rows.end(), std::make_move_iterator(hits.begin()), std::make_move_iterator(hits.end()));
			}

			return cardHits;
		}

		void AppendPageHits(std::vector<SettingsPageHits>& hits, SettingsPage& page, SettingsPageHits pageHits,
							std::span<const std::string_view> pageTitles, const SearchTerms& terms)
		{
			for (const SettingsCard& card : page.Describe())
			{
				SettingsCardHits cardHits = MatchCard(card, pageTitles, terms);

				if (!cardHits.Rows.empty())
				{
					pageHits.Cards.push_back(std::move(cardHits));
				}
			}

			if (!pageHits.Cards.empty())
			{
				hits.push_back(std::move(pageHits));
			}
		}

		void AppendChildHits(std::vector<SettingsPageHits>& hits, const SettingsPage& parent, const SettingsDirectory& children,
							 const SearchTerms& terms)
		{
			for (const SettingsGroup& group : children.GetGroups())
			{
				for (const Core::Scope<SettingsPage>& child : group.Pages)
				{
					const std::array<std::string_view, 2> pageTitles{parent.GetTitle(), child->GetTitle()};
					SettingsPageHits pageHits{
						.Path = {std::string{parent.GetId()}, std::string{child->GetId()}},
						.Label = std::string{parent.GetTitle()} + std::string{kBreadcrumbSeparator} + std::string{child->GetTitle()}};

					AppendPageHits(hits, *child, std::move(pageHits), pageTitles, terms);
				}
			}
		}
	}

	std::vector<std::string> SplitQueryWords(std::string_view query)
	{
		constexpr char kWordSeparator = ' ';

		return Core::SplitSkippingEmpty(Core::Lowered(query), kWordSeparator);
	}

	bool IsSearchQuery(std::string_view query)
	{
		return !SplitQueryWords(query).empty();
	}

	bool EveryWordFound(std::span<const std::string> words, std::span<const std::string_view> texts)
	{
		for (const std::string& word : words)
		{
			if (!AnyTextContains(texts, word))
			{
				return false;
			}
		}

		return true;
	}

	std::vector<SettingsPageHits> FindSettings(const SettingsDirectory& directory, std::string_view query)
	{
		const SearchTerms terms{.Words = SplitQueryWords(query), .Query = Core::Trimmed(query)};

		if (terms.Words.empty())
		{
			return {};
		}

		std::vector<SettingsPageHits> hits;

		for (const SettingsGroup& group : directory.GetGroups())
		{
			for (const Core::Scope<SettingsPage>& page : group.Pages)
			{
				const std::array<std::string_view, 1> pageTitles{page->GetTitle()};
				SettingsPageHits pageHits{.Path = {std::string{page->GetId()}}, .Label = std::string{page->GetTitle()}};

				AppendPageHits(hits, *page, std::move(pageHits), pageTitles, terms);

				if (const SettingsDirectory* children = page->GetChildren(); children != nullptr)
				{
					AppendChildHits(hits, *page, *children, terms);
				}
			}
		}

		return hits;
	}

	bool HasHitsUnder(const std::vector<SettingsPageHits>& hits, std::string_view topPageId)
	{
		for (const SettingsPageHits& page : hits)
		{
			if (!page.Path.empty() && page.Path.front() == topPageId)
			{
				return true;
			}
		}

		return false;
	}
}
