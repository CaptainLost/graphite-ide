#include "Sessions/Overview/ListTiles.h"

#include "Sessions/Overview/ChartTiles.h"
#include "Sessions/Overview/DashboardFigures.h"
#include "Sessions/Overview/TipText.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/Lists/ListRow.h"
#include "UI/TypographicMarks.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kHalf = 0.5f;

		// WARNING: Lengths are in font sizes, from the mockup; each is multiplied by the unit before it is drawn.
		constexpr float kRowPitch = 1.45f;
		constexpr float kBarInset = 0.12f;
		constexpr float kBarRounding = 0.15f;
		constexpr float kNameInset = 0.35f;
		constexpr float kColumnGap = 0.6f;
		constexpr float kSubGap = 0.4f;
		constexpr float kRowSpark = 4.5f;
		constexpr float kRowSparkHeight = 1.0f;
		constexpr float kSparkNeedsWidth = 16.0f;
		constexpr std::chrono::hours kOneDay{24};

		constexpr float kStackHeight = 0.83f;
		constexpr float kStackRounding = 0.15f;
		constexpr float kStackGap = 0.5f;
		constexpr float kStatsGap = 0.5f;
		constexpr std::size_t kModelSlots = kModelFamilyCount;
		constexpr std::size_t kModelStats = 3;
		constexpr std::size_t kCacheParts = 3;
		constexpr double kThinnestPart = 0.004;
		constexpr double kNothing = 1e-9;
		constexpr std::string_view kWidestMoney = "$000.00";
		constexpr bool kSaved = false;
		constexpr bool kLost = true;

		float BodyTop(float rowTop, float pitch, float size)
		{
			return rowTop + (pitch - size) * kHalf;
		}

		void DrawNameAndNote(DashboardCanvas& canvas, ImVec2 at, float pitch, float room, std::string_view name,
							 std::string_view note)
		{
			const float body = canvas.Units(kBodySize);
			const float small = canvas.Units(kSmallSize);
			const std::string shown = FitAt(name, room, body);
			DrawTextAt(canvas.Drawing, ImVec2(at.x, BodyTop(at.y, pitch, body)), body, canvas.Colours.Ink, shown);

			if (note.empty() || shown != name)
			{
				return;
			}

			const float used = TextWidthAt(shown, body) + canvas.Units(kSubGap);
			const std::string fitted = FitAt(note, std::max(0.0f, room - used), small);
			DrawTextAt(canvas.Drawing, ImVec2(at.x + used, BodyTop(at.y, pitch, small)), small, Faint(canvas.Colours), fitted);
		}

		void DrawRightAligned(DashboardCanvas& canvas, float right, float rowTop, float pitch, float size, ImU32 colour,
							  std::string_view text, TextFace face = TextFace::Regular)
		{
			DrawTextAt(canvas.Drawing, ImVec2(right - TextWidthAt(text, size, face), BodyTop(rowTop, pitch, size)), size,
					   colour, text, face);
		}

		void DrawRowBar(DashboardCanvas& canvas, const ImRect& row, float width)
		{
			if (width <= 0.0f)
			{
				return;
			}

			const float inset = canvas.Units(kBarInset);
			canvas.Drawing.AddRectFilled(ImVec2(row.Min.x, row.Min.y + inset), ImVec2(row.Min.x + width, row.Max.y - inset),
										 canvas.Colours.Bar, canvas.Units(kBarRounding));
		}

		float WidestOf(const std::vector<std::string>& texts, float size)
		{
			float widest = 0.0f;

			for (const std::string& text : texts)
			{
				widest = std::max(widest, TextWidthAt(text, size));
			}

			return widest;
		}

		std::string SessionSpan(const SessionSummary& summary, const LocalCalendar& calendar)
		{
			const auto started = ParseTimestamp(summary.StartedAt);
			const auto ended = ParseTimestamp(summary.EndedAt);

			if (!started || !ended || *ended < *started)
			{
				return {};
			}

			if (*ended - *started < kOneDay)
			{
				return FormatDuration(*ended - *started);
			}

			const auto first = calendar.DayOf(summary.StartedAt);
			const auto last = calendar.DayOf(summary.EndedAt);
			return first && last ? ShortDate(*first) + std::string{kEnDash} + ShortDate(*last) : std::string{};
		}

		std::string WithoutPluginPrefix(const std::string& skill)
		{
			const std::size_t colon = skill.find(':');
			return colon == std::string::npos ? skill : skill.substr(colon + 1);
		}

		struct CallRow
		{
			std::string Name;
			std::size_t Calls = 0;
		};

		template <typename TipOf>
		void DrawCallRows(DashboardCanvas& canvas, const ImRect& tile, const ImRect& content, const std::vector<CallRow>& rows,
						  TipOf tipOf)
		{
			const float body = canvas.Units(kBodySize);
			const float pitch = canvas.Units(kRowPitch);
			std::vector<std::string> counts;

			for (const CallRow& row : rows)
			{
				counts.push_back(TipCount(row.Calls));
			}

			const float countWidth = WidestOf(counts, body);
			const float nameRoom = std::max(0.0f, content.GetWidth() - countWidth - canvas.Units(kColumnGap));
			const auto first = rows.empty() ? 1.0 : static_cast<double>(std::max<std::size_t>(1, rows.front().Calls));

			for (std::size_t index = 0; index < rows.size(); ++index)
			{
				const ImRect row = ListRowRect(tile, index);
				DrawRowBar(canvas, row, static_cast<float>(static_cast<double>(rows[index].Calls) / first) * nameRoom);
				DrawNameAndNote(canvas, ImVec2(row.Min.x + canvas.Units(kNameInset), row.Min.y), pitch,
								nameRoom - canvas.Units(kNameInset), rows[index].Name, {});
				DrawRightAligned(canvas, row.Max.x, row.Min.y, pitch, body, canvas.Colours.Dim, counts[index]);
				canvas.Add({.Rect = row, .Tip = [tipOf, index] { return tipOf(index); }});
			}
		}

		struct CachePiece
		{
			CachePart Part;
			std::string_view Label;
			std::uint64_t Tokens = 0;
			ImU32 Colour = 0;
		};

		std::array<CachePiece, kCacheParts> CachePieces(const DashboardModel& model, const DashboardColours& colours)
		{
			return {{{CachePart::Read, "Read from cache", model.Tokens.CacheRead, colours.Heat[kHeatLevels - 2]},
					 {CachePart::Written, "Written to cache", model.Tokens.CacheWrite, colours.Accent},
					 {CachePart::Uncached, "Uncached", model.Tokens.Input, colours.Bad}}};
		}

		bool CacheSentenceFits(const CacheSentence& sentence, float room, float size)
		{
			return TextWidthAt(sentence.Before, size) + TextWidthAt(sentence.Without, size, TextFace::Strong) +
					   TextWidthAt(sentence.After, size) <=
				   room;
		}

		// WARNING: Measured on the widest sentence either wording can make, never on the one shown, so a figure that
		// grows cannot wrap it onto another line and reflow the row.
		float CacheSentenceHeight(float room, float unit)
		{
			const float size = kSmallSize * unit;
			float height = 0.0f;

			for (const CacheSentence& sentence : {CacheSentenceOf(kWidestMoney, kWidestMoney, kSaved),
												  CacheSentenceOf(kWidestMoney, kWidestMoney, kLost)})
			{
				height = std::max(height, CacheSentenceFits(sentence, room, size) ? size * kLinePitch
																				  : WrappedHeightAt(sentence.Whole(), size, room));
			}

			return height;
		}

		template <typename TipOf>
		void DrawStack(DashboardCanvas& canvas, const ImRect& bar, const std::vector<std::pair<double, ImU32>>& parts,
					   double thinnest, TipOf tipOf)
		{
			double total = 0.0;
			for (const auto& [amount, colour] : parts)
			{
				total += amount;
			}

			if (total <= 0.0)
			{
				canvas.Drawing.AddRectFilled(bar.Min, bar.Max, canvas.Colours.Heat[0], canvas.Units(kStackRounding));
				return;
			}

			std::vector<double> shares;
			double shareTotal = 0.0;
			for (const auto& [amount, colour] : parts)
			{
				shares.push_back(amount > 0.0 ? std::max(thinnest, amount / total) : 0.0);
				shareTotal += shares.back();
			}

			float x = bar.Min.x;
			for (std::size_t index = 0; index < parts.size(); ++index)
			{
				if (shares[index] <= 0.0)
				{
					continue;
				}

				const float width = static_cast<float>(shares[index] / shareTotal) * bar.GetWidth();
				const ImRect piece(ImVec2(x, bar.Min.y), ImVec2(x + width, bar.Max.y));
				const ImU32 colour = parts[index].second;
				canvas.Drawing.AddRectFilled(piece.Min, piece.Max, canvas.IsHot(piece) ? Brighter(colour) : colour);
				canvas.Add({.Rect = piece, .Tip = [tipOf, index] { return tipOf(index); }});
				x += width;
			}
		}

		float ModelRowsTop(const ImRect& content, float unit)
		{
			return content.Min.y + (kStackHeight + kStackGap) * unit;
		}
	}

	CacheSentence CacheSentenceOf(std::string_view without, std::string_view actual, bool lost)
	{
		if (lost)
		{
			return {.Before = "These requests would have cost ",
					.Without = std::string{without},
					.After = " without the cache, not " + std::string{actual} + "."};
		}

		return {.Before = "Without the cache this would have cost ",
				.Without = std::string{without},
				.After = ", not " + std::string{actual} + "."};
	}

	CacheSentence CacheSentenceOf(const DashboardModel& model)
	{
		return CacheSentenceOf(CompactMoney(model.Cost + model.Saved), CompactMoney(model.Cost), model.Saved < 0.0);
	}

	ImRect ListRowRect(const ImRect& tile, std::size_t row)
	{
		const float unit = ImGui::GetFontSize();
		const float pitch = kRowPitch * unit;
		const float top = tile.Min.y + TileHeaderHeight(unit) + static_cast<float>(row) * pitch;
		const float side = TileSidePadding(unit);
		return ImRect(ImVec2(tile.Min.x + side, top), ImVec2(tile.Max.x - side, top + pitch));
	}

	float ListTileHeight(std::size_t slots)
	{
		const float unit = ImGui::GetFontSize();
		return TileHeaderHeight(unit) + static_cast<float>(slots) * kRowPitch * unit + TileBottomPadding(unit);
	}

	float ModelsHeight(float /*width*/)
	{
		const float unit = ImGui::GetFontSize();
		return TileHeaderHeight(unit) +
			   (kStackHeight + kStackGap + static_cast<float>(kModelSlots + kModelStats) * kLinePitch + kStatsGap) * unit +
			   TileBottomPadding(unit);
	}

	void DrawModels(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const ImRect content = DrawTileFrame(canvas, tile, "Models");
		const float body = canvas.Units(kBodySize);
		const float small = canvas.Units(kSmallSize);
		const float pitch = canvas.Units(kLinePitch);

		std::vector<ModelFamily> present;
		std::vector<std::pair<double, ImU32>> parts;
		for (std::size_t family = 0; family < kModelFamilyCount; ++family)
		{
			if (model.Families[family].Requests > 0 || model.Families[family].Cost > 0.0)
			{
				present.push_back(static_cast<ModelFamily>(family));
				parts.emplace_back(model.Families[family].Cost, canvas.Colours.Families[family]);
			}
		}

		const auto modelTip = [&model, present](std::size_t index) { return ModelTip(model, present[index]); };
		DrawStack(canvas, ImRect(content.Min, ImVec2(content.Max.x, content.Min.y + canvas.Units(kStackHeight))), parts, 0.0,
				  modelTip);

		std::vector<std::string> costs;
		std::vector<std::string> shares;
		for (const ModelFamily family : present)
		{
			const FamilyTotals& totals = model.Families[static_cast<std::size_t>(family)];
			const bool unpriced = totals.UnpricedRequests > 0;
			costs.push_back(unpriced ? std::string{kUnknownAmount} : TipMoney(totals.Cost));
			shares.push_back(unpriced ? std::string{} : TipShare(totals.Cost, model.Cost));
		}

		const float gap = canvas.Units(kColumnGap);
		const float shareWidth = WidestOf(shares, small);
		const float costRight = content.Max.x - shareWidth - gap;
		const float nameRoom = std::max(0.0f, costRight - WidestOf(costs, body) - gap - content.Min.x - SwatchRoom(canvas.Unit));
		float top = ModelRowsTop(content, canvas.Unit);

		for (std::size_t index = 0; index < present.size(); ++index)
		{
			const ImRect row(ImVec2(content.Min.x, top), ImVec2(content.Max.x, top + pitch));
			DrawSwatch(canvas, row.Min.x, top, pitch, canvas.Colours.Families[static_cast<std::size_t>(present[index])]);
			DrawTextAt(canvas.Drawing, ImVec2(row.Min.x + SwatchRoom(canvas.Unit), BodyTop(top, pitch, body)), body,
					   canvas.Colours.Ink, FitAt(FamilyLabel(model, present[index]), nameRoom, body));
			DrawRightAligned(canvas, costRight, top, pitch, body, canvas.Colours.Ink, costs[index]);
			DrawRightAligned(canvas, content.Max.x, top, pitch, small, Faint(canvas.Colours), shares[index]);
			canvas.Add({.Rect = row, .Tip = [modelTip, index] { return modelTip(index); }});
			top += pitch;
		}

		std::vector<double> sessionCosts;
		for (const SessionFacts& session : model.Sessions)
		{
			sessionCosts.push_back(session.Cost);
		}

		const std::string dash{kEnDash};
		const std::array<std::pair<std::string_view, std::string>, kModelStats> stats = {{
			{"Per session, average",
			 model.Sessions.empty() ? dash : TipMoney(model.Cost / static_cast<double>(model.Sessions.size()))},
			{"Per session, median", model.Sessions.empty() ? dash : TipMoney(Median(std::move(sessionCosts)))},
			{"Per request", model.Requests == 0 ? dash : TipMoney(model.Cost / static_cast<double>(model.Requests))},
		}};

		top = ModelRowsTop(content, canvas.Unit) + static_cast<float>(kModelSlots) * pitch + canvas.Units(kStatsGap);

		for (const auto& [label, value] : stats)
		{
			const float valueWidth = TextWidthAt(value, body);
			DrawTextAt(canvas.Drawing, ImVec2(content.Min.x, BodyTop(top, pitch, small)), small, canvas.Colours.Dim,
					   FitAt(label, std::max(0.0f, content.GetWidth() - valueWidth - gap), small));
			DrawRightAligned(canvas, content.Max.x, top, pitch, body, canvas.Colours.Ink, value);
			top += pitch;
		}
	}

	void DrawProjects(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const auto& projects = model.Projects;
		const ImRect content = DrawTileFrame(canvas, tile, "Where it went", TipCounted(projects.size(), "folder", "folders"));
		const std::size_t shown = std::min(kTopRows, projects.size());
		const std::size_t rest = projects.size() - shown;
		const float body = canvas.Units(kBodySize);
		const float pitch = canvas.Units(kRowPitch);
		const float gap = canvas.Units(kColumnGap);

		std::vector<std::string> costs;
		for (std::size_t index = 0; index < shown; ++index)
		{
			costs.push_back(TipMoney(projects[index].Cost));
		}

		double restCost = 0.0;
		for (std::size_t index = shown; index < projects.size(); ++index)
		{
			restCost += projects[index].Cost;
		}

		const std::string restText = TipMoney(restCost);
		const float costWidth = std::max(WidestOf(costs, body), rest > 0 ? TextWidthAt(restText, body) : 0.0f);
		const float spark = content.GetWidth() >= canvas.Units(kSparkNeedsWidth) ? canvas.Units(kRowSpark) : 0.0f;
		const float valueWidth = costWidth + (spark > 0.0f ? spark + gap : 0.0f);
		const float nameRoom = std::max(0.0f, content.GetWidth() - valueWidth - gap);
		const double first = shown > 0 ? std::max(projects.front().Cost, kNothing) : 1.0;
		const ImU32 sparkColour = canvas.Colours.Heat[kHeatLevels - 2];

		for (std::size_t index = 0; index < shown; ++index)
		{
			const ProjectTotals& project = projects[index];
			const ImRect row = ListRowRect(tile, index);
			DrawRowBar(canvas, row, static_cast<float>(project.Cost / first) * nameRoom);

			std::string note = TipCount(project.Sessions);
			if (!project.Worktrees.empty())
			{
				note += std::format("{}{} wt", kMiddleDotSeparator, project.Worktrees.size());
			}

			DrawNameAndNote(canvas, ImVec2(row.Min.x + canvas.Units(kNameInset), row.Min.y), pitch,
							nameRoom - canvas.Units(kNameInset), ProjectName(project.Repository), note);

			if (spark > 0.0f)
			{
				const float sparkRight = row.Max.x - costWidth - gap;
				const float sparkTop = row.Min.y + (pitch - canvas.Units(kRowSparkHeight)) * kHalf;
				DrawSparkline(canvas.Drawing,
							  ImRect(ImVec2(sparkRight - spark, sparkTop),
									 ImVec2(sparkRight, sparkTop + canvas.Units(kRowSparkHeight))),
							  project.DailyCost, sparkColour);
			}

			DrawRightAligned(canvas, row.Max.x, row.Min.y, pitch, body, canvas.Colours.Dim, costs[index]);
			canvas.Add({.Rect = row, .Tip = [&model, index] { return ProjectTip(model, index); }});
		}

		if (rest > 0)
		{
			const ImRect row = ListRowRect(tile, kTopRows);
			DrawTextAt(canvas.Drawing, ImVec2(row.Min.x + canvas.Units(kNameInset), BodyTop(row.Min.y, pitch, body)), body,
					   Faint(canvas.Colours), std::format("{} more", rest));
			DrawRightAligned(canvas, row.Max.x, row.Min.y, pitch, body, canvas.Colours.Dim, restText);
		}
	}

	void DrawSessions(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const LocalCalendar& calendar = canvas.Calendar;
		const ImRect content = DrawTileFrame(canvas, tile, "Most expensive sessions");
		const std::size_t shown = std::min(kTopRows, model.Sessions.size());
		const float body = canvas.Units(kBodySize);
		const float pitch = canvas.Units(kRowPitch);
		const float gap = canvas.Units(kColumnGap);

		std::vector<std::string> costs;
		std::vector<std::string> spans;
		for (std::size_t index = 0; index < shown; ++index)
		{
			const SessionFacts& session = model.Sessions[index];
			costs.push_back(SessionMoney(session));
			spans.push_back(session.Summary == nullptr ? std::string{} : SessionSpan(*session.Summary, calendar));
		}

		const float costWidth = WidestOf(costs, body);
		const float spanWidth = WidestOf(spans, body);
		const float spanRight = content.Max.x - costWidth - gap;
		const float titleRoom = std::max(0.0f, spanRight - spanWidth - gap - content.Min.x - canvas.Units(kNameInset));
		const ImU32 hover = ImGui::GetColorU32(CurrentListRowColours().Hovered);

		for (std::size_t index = 0; index < shown; ++index)
		{
			const SessionFacts& session = model.Sessions[index];
			const ImRect row = ListRowRect(tile, index);

			if (canvas.IsHot(row))
			{
				canvas.Drawing.AddRectFilled(row.Min, row.Max, hover, canvas.Units(kBarRounding));
			}

			if (index > 0)
			{
				canvas.Drawing.AddLine(row.Min, ImVec2(row.Max.x, row.Min.y), canvas.Colours.Frame);
			}

			DrawNameAndNote(canvas, ImVec2(row.Min.x + canvas.Units(kNameInset), row.Min.y), pitch, titleRoom,
							SessionTitle(session), PlaceLabel(session.Place));
			DrawRightAligned(canvas, spanRight, row.Min.y, pitch, body, canvas.Colours.Ink, spans[index]);
			DrawRightAligned(canvas, row.Max.x, row.Min.y, pitch, body, canvas.Colours.Ink, costs[index]);
			canvas.Add({.Rect = row,
						.Tip = [&model, &calendar, index] { return SessionTip(model, index, calendar); },
						.OpenSession = index});
		}
	}

	void DrawTools(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const ImRect content = DrawTileFrame(canvas, tile, "Tools", "calls");
		std::vector<CallRow> rows;

		for (std::size_t index = 0; index < std::min(kListRows, model.Tools.size()); ++index)
		{
			rows.push_back({model.Tools[index].Name, model.Tools[index].Tally.Calls});
		}

		DrawCallRows(canvas, tile, content, rows, [&model](std::size_t index) { return ToolTip(model, index); });
	}

	void DrawSkills(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const LocalCalendar& calendar = canvas.Calendar;
		const ImRect content = DrawTileFrame(canvas, tile, "Skills", "calls");
		std::vector<CallRow> rows;

		for (std::size_t index = 0; index < std::min(kListRows, model.Skills.size()); ++index)
		{
			rows.push_back({WithoutPluginPrefix(model.Skills[index].Name), model.Skills[index].Tally.Calls});
		}

		DrawCallRows(canvas, tile, content, rows,
					 [&model, &calendar](std::size_t index) { return SkillTip(model, index, calendar); });
	}

	float CacheHeight(float width)
	{
		const float unit = ImGui::GetFontSize();
		const float room = std::max(0.0f, width - 2.0f * TileSidePadding(unit));
		return TileHeaderHeight(unit) +
			   (kStackHeight + kStackGap + static_cast<float>(kCacheParts) * kLinePitch + kStatsGap) * unit +
			   CacheSentenceHeight(room, unit) + TileBottomPadding(unit);
	}

	void DrawCache(DashboardCanvas& canvas, const ImRect& tile)
	{
		const DashboardModel& model = canvas.Model;
		const std::array<CachePiece, kCacheParts> pieces = CachePieces(model, canvas.Colours);

		std::uint64_t all = 0;
		std::vector<std::pair<double, ImU32>> parts;
		for (const CachePiece& piece : pieces)
		{
			all += piece.Tokens;
			parts.emplace_back(static_cast<double>(piece.Tokens), piece.Colour);
		}

		const std::string aside =
			FormatTokens(all) + " in" + std::string{kMiddleDotSeparator} + FormatTokens(model.Tokens.Output) + " out";
		const ImRect content = DrawTileFrame(canvas, tile, "Input tokens and cache", aside);
		const float body = canvas.Units(kBodySize);
		const float small = canvas.Units(kSmallSize);
		const float pitch = canvas.Units(kLinePitch);
		const float gap = canvas.Units(kColumnGap);

		const auto cacheTip = [&model, pieces](std::size_t index) { return CacheTip(model, pieces[index].Part); };
		DrawStack(canvas, ImRect(content.Min, ImVec2(content.Max.x, content.Min.y + canvas.Units(kStackHeight))), parts,
				  kThinnestPart, cacheTip);

		std::vector<std::string> tokens;
		std::vector<std::string> shares;
		for (const CachePiece& piece : pieces)
		{
			tokens.push_back(FormatTokens(piece.Tokens));
			shares.push_back(all == 0 ? std::string{kEnDash}
									  : std::format("{:.1f}%", 100.0 * static_cast<double>(piece.Tokens) / static_cast<double>(all)));
		}

		const float shareWidth = WidestOf(shares, small);
		const float tokensRight = content.Max.x - shareWidth - gap;
		const float labelRoom = std::max(0.0f, tokensRight - WidestOf(tokens, body) - gap - content.Min.x - SwatchRoom(canvas.Unit));
		float top = ModelRowsTop(content, canvas.Unit);

		for (std::size_t index = 0; index < kCacheParts; ++index)
		{
			const ImRect row(ImVec2(content.Min.x, top), ImVec2(content.Max.x, top + pitch));
			DrawSwatch(canvas, row.Min.x, top, pitch, pieces[index].Colour);
			DrawTextAt(canvas.Drawing, ImVec2(row.Min.x + SwatchRoom(canvas.Unit), BodyTop(top, pitch, body)), body,
					   canvas.Colours.Ink, FitAt(pieces[index].Label, labelRoom, body));
			DrawRightAligned(canvas, tokensRight, top, pitch, body, canvas.Colours.Ink, tokens[index]);
			DrawRightAligned(canvas, content.Max.x, top, pitch, small, Faint(canvas.Colours), shares[index]);
			canvas.Add({.Rect = row, .Tip = [cacheTip, index] { return cacheTip(index); }});
			top += pitch;
		}

		top += canvas.Units(kStatsGap);
		const CacheSentence sentence = CacheSentenceOf(model);

		if (!CacheSentenceFits(sentence, content.GetWidth(), small))
		{
			DrawWrappedAt(canvas.Drawing, ImVec2(content.Min.x, top), small, canvas.Colours.Dim, sentence.Whole(),
						  content.GetWidth());
			return;
		}

		float x = content.Min.x;
		const float textTop = BodyTop(top, small * kLinePitch, small);
		DrawTextAt(canvas.Drawing, ImVec2(x, textTop), small, canvas.Colours.Dim, sentence.Before);
		x += TextWidthAt(sentence.Before, small);
		DrawTextAt(canvas.Drawing, ImVec2(x, textTop), small, canvas.Colours.Ink, sentence.Without, TextFace::Strong);
		x += TextWidthAt(sentence.Without, small, TextFace::Strong);
		DrawTextAt(canvas.Drawing, ImVec2(x, textTop), small, canvas.Colours.Dim, sentence.After);
	}
}
