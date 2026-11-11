#include <doctest/doctest.h>

#include "Sessions/PricingPage.h"

#include <string>

using namespace Graphite;

namespace {

	// Cut from the page as it stood on 2026-11-11, keeping the shapes a row takes: a footnote mark, a note in brackets
	// with a link, two rows for one model, and a later table that is not the price list.
	const std::string kPage = R"(---
title: Pricing
---

This page provides detailed pricing information for Anthropic's models and features. All prices are in USD.

## Model pricing

The following table shows pricing for all Claude models:

| Model                                                       | Base input tokens     | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens          |
| :---------------------------------------------------------- | :-------------------- | :-------------- | :-------------- | :----------------------- | :--------------------- |
| Claude Fable 5.2                                            | $8 / MTok             | $10 / MTok      | $16 / MTok      | $0.20 / MTok<sup>1</sup> | $40 / MTok             |
| Claude Mythos 5.2 ([limited availability](https://support.claude.com/en/articles/14604842)) | $8 / MTok | $10 / MTok | $16 / MTok | $0.20 / MTok<sup>1</sup> | $40 / MTok |
| Claude Opus 5.5                                             | $4 / MTok             | $5 / MTok       | $8 / MTok       | $0.20 / MTok<sup>2</sup> | $20 / MTok             |
| Claude Opus 4.1 ([retired, except on Bedrock](https://platform.claude.com/docs/en/about-claude/model-deprecations)) | $15 / MTok | $18.75 / MTok | $30 / MTok | $1.50 / MTok | $75 / MTok |
| Claude Sonnet 5                                             | $2 / MTok<sup>3</sup> | $2.50 / MTok    | $4 / MTok       | $0.20 / MTok             | $10 / MTok<sup>3</sup> |
| Claude Haiku 5.5 (for prompts up to 100,000 tokens)         | $0.10 / MTok          | $0.125 / MTok   | $0.20 / MTok    | $0.01 / MTok             | $0.50 / MTok           |
| Claude Haiku 5.5 (for prompts over 100,000 tokens)          | $0.50 / MTok          | $0.625 / MTok   | $1 / MTok       | $0.05 / MTok             | $2.50 / MTok           |

*<sup>1 Cache hits and refreshes on Claude Fable 5.2 are priced at 0.025x the base input price.</sup>*

### Batch processing

| Model                 | Batch input  | Batch output |
| :-------------------- | :----------- | :----------- |
| Claude Fable 5.2      | $4 / MTok    | $20 / MTok   |
)";

	std::string WithTableHeader(const std::string& header)
	{
		return "## Model pricing\n\n" + header +
			   "\n| :--- | :--- | :--- | :--- | :--- | :--- |\n| Claude Opus 5.5 | $4 / MTok | $5 / MTok | $8 / MTok | $0.20 / MTok | $20 / MTok |\n";
	}
}

TEST_CASE("The pricing page gives each model its five prices under its model id")
{
	const Core::Result<PriceList> read = ReadPricingPage(kPage);

	REQUIRE(read.HasValue());
	const PriceList& prices = read.GetValue();

	REQUIRE(prices.contains("claude-opus-5-5"));
	CHECK(prices.at("claude-opus-5-5") == Price{.Input = 4.0, .Output = 20.0, .CacheWrite5m = 5.0, .CacheWrite1h = 8.0, .CacheRead = 0.2});

	REQUIRE(prices.contains("claude-fable-5-2"));
	CHECK(prices.at("claude-fable-5-2").CacheRead == doctest::Approx(0.2));
}

TEST_CASE("A note in brackets after a model's name is not part of its id")
{
	const Core::Result<PriceList> read = ReadPricingPage(kPage);

	REQUIRE(read.HasValue());
	CHECK(read.GetValue().contains("claude-mythos-5-2"));
	CHECK(read.GetValue().contains("claude-opus-4-1"));
}

TEST_CASE("A footnote mark after a price does not change the price")
{
	const Core::Result<PriceList> read = ReadPricingPage(kPage);

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().contains("claude-sonnet-5"));
	CHECK(read.GetValue().at("claude-sonnet-5").Input == doctest::Approx(2.0));
	CHECK(read.GetValue().at("claude-sonnet-5").Output == doctest::Approx(10.0));
}

TEST_CASE("A model with a dearer tier for long prompts keeps the price of its first row")
{
	const Core::Result<PriceList> read = ReadPricingPage(kPage);

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().contains("claude-haiku-5-5"));
	CHECK(read.GetValue().at("claude-haiku-5-5").Input == doctest::Approx(0.10));
	CHECK(read.GetValue().at("claude-haiku-5-5").CacheWrite5m == doctest::Approx(0.125));
}

TEST_CASE("Only the model price table is read, not the tables after it")
{
	const Core::Result<PriceList> read = ReadPricingPage(kPage);

	REQUIRE(read.HasValue());
	CHECK(read.GetValue().size() == 6);
	CHECK(read.GetValue().at("claude-fable-5-2").Input == doctest::Approx(8.0));
}

TEST_CASE("A page without the model price section is an error, not an empty list")
{
	CHECK_FALSE(ReadPricingPage("").HasValue());
	CHECK_FALSE(ReadPricingPage("# Pricing\n\nNothing here.\n").HasValue());
}

TEST_CASE("A table whose columns are not the five prices in their order is an error")
{
	CHECK(ReadPricingPage(WithTableHeader("| Model | Base input tokens | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens |")).HasValue());

	CHECK_FALSE(ReadPricingPage(WithTableHeader("| Model | Base input tokens | 1h cache writes | 5m cache writes | Cache hits and refreshes | Output tokens |")).HasValue());
	CHECK_FALSE(ReadPricingPage(WithTableHeader("| Model | Input | Output |")).HasValue());
}

TEST_CASE("A price that does not read as dollars per million tokens is an error, not a skipped row")
{
	const std::string page = "## Model pricing\n\n"
							 "| Model | Base input tokens | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens |\n"
							 "| :--- | :--- | :--- | :--- | :--- | :--- |\n"
							 "| Claude Opus 5.5 | $4 / MTok | $5 / MTok | $8 / MTok | $0.20 / MTok | $20 / MTok |\n"
							 "| Claude Opus 5 | $5 per thousand | $6.25 / MTok | $10 / MTok | $0.50 / MTok | $25 / MTok |\n";

	CHECK_FALSE(ReadPricingPage(page).HasValue());
}

TEST_CASE("A row whose name does not make a model id is passed over")
{
	const std::string page = "## Model pricing\n\n"
							 "| Model | Base input tokens | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens |\n"
							 "| :--- | :--- | :--- | :--- | :--- | :--- |\n"
							 "| All models, on average | $4 / MTok | $5 / MTok | $8 / MTok | $0.20 / MTok | $20 / MTok |\n"
							 "| Claude Opus 5.5 | $4 / MTok | $5 / MTok | $8 / MTok | $0.20 / MTok | $20 / MTok |\n";

	const Core::Result<PriceList> read = ReadPricingPage(page);

	REQUIRE(read.HasValue());
	CHECK(read.GetValue().size() == 1);
	CHECK(read.GetValue().contains("claude-opus-5-5"));
}

TEST_CASE("A table with no model row is an error")
{
	const std::string page = "## Model pricing\n\n"
							 "| Model | Base input tokens | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens |\n"
							 "| :--- | :--- | :--- | :--- | :--- | :--- |\n";

	CHECK_FALSE(ReadPricingPage(page).HasValue());
}

TEST_CASE("A page with Windows line ends reads as one with plain ones")
{
	std::string page;
	for (const char character : kPage)
	{
		if (character == '\n')
		{
			page += '\r';
		}
		page += character;
	}

	const Core::Result<PriceList> read = ReadPricingPage(page);

	REQUIRE(read.HasValue());
	CHECK(read.GetValue().size() == 6);
}
