#include "Git/GitView.h"

#include "Git/CommitLabels.h"
#include "Git/GitPalette.h"

#include "Core/Command/CommandRegistry.h"

#include <imgui.h>

#include <chrono>
#include <format>

namespace Graphite {

	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path)
	{
		for (std::size_t index = 0; index < diff.Files.size(); ++index)
		{
			const Core::FileDiff& file = diff.Files[index];

			if (file.NewPath == path || file.OldPath == path)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	GitView::GitView(GitModel& model, const ActiveScheme& scheme)
		: Core::Panel("git", "Git", false), m_Model(model), m_Scheme(scheme)
	{
	}

	void GitView::RegisterCommands(Core::CommandRegistry& commands)
	{
		// Bound to nothing by default: there is no established chord for it, and a command
		// with no binding is still reachable from the panels menu and the command palette.
		commands.Add({.Id = "git.show",
					  .Label = "Git",
					  .Run = [this] { SetVisible(true); }});
	}

	void GitView::OnImGuiRender()
	{
		const std::shared_ptr<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		if (ImGui::BeginTable("git", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * 10.0f);
			ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			RenderRefs(branches ? *branches : std::vector<Core::Branch>{});

			ImGui::TableSetColumnIndex(1);
			RenderCommits();

			ImGui::TableSetColumnIndex(2);
			RenderDetails();

			ImGui::EndTable();
		}

		if (const std::optional<Core::Error>& error = m_Model.GetLastError(); error.has_value())
		{
			// A line, not a modal: a failed refresh is worth saying and not worth
			// interrupting for.
			ImGui::TextUnformatted(error->Message.c_str());
		}
	}

	void GitView::RenderRefs(const std::vector<Core::Branch>& branches)
	{
		const auto group = [&](const char* heading, Core::RefKind kind) {
			ImGui::SeparatorText(heading);

			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind != kind)
				{
					continue;
				}

				const bool selected = m_SelectedRef.has_value() && *m_SelectedRef == branch.FullName;

				std::string label = branch.Name;

				if (branch.IsHead)
				{
					label = "* " + label;
				}

				// Ahead and behind are only meaningful when they were measured — zero and
				// zero also spells "in sync", which is a different claim.
				if (branch.DistanceKnown && (branch.Ahead > 0 || branch.Behind > 0))
				{
					label += std::format("  {}/{}", branch.Ahead, branch.Behind);
				}

				if (ImGui::Selectable(label.c_str(), selected))
				{
					m_SelectedRef = branch.FullName;

					Core::LogRequest request;
					request.Ref = branch.FullName;
					m_Model.RequestLog(request);
				}
			}
		};

		group("Local", Core::RefKind::Local);
		group("Remote", Core::RefKind::Remote);
	}

	void GitView::RenderCommits()
	{
		const std::shared_ptr<const Core::LogPage> page = m_Model.GetLog();

		if (!page)
		{
			ImGui::TextUnformatted("Loading history…");
			return;
		}

		const Core::CommitGraph graph = Core::LayoutCommits(page->Commits);
		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		m_Commits.Render(*page, graph, palette);

		if (m_Commits.TakeLoadMoreRequest())
		{
			// Ask for a page twice the size rather than appending: the model holds one
			// snapshot per query, and merging pages would need a second concept of
			// identity for "the same log, more of it".
			Core::LogRequest request;
			request.Ref = m_SelectedRef;
			request.MaxCount = page->Commits.size() * 2;
			m_Model.RequestLog(request);
		}

		RequestDiffForSelection();
	}

	void GitView::RequestDiffForSelection()
	{
		const std::optional<Core::Oid> selected = m_Commits.GetSelected();

		if (!selected.has_value())
		{
			return;
		}

		if (m_DiffFor.has_value() && *m_DiffFor == *selected)
		{
			return;
		}

		m_DiffFor = *selected;
		m_SelectedFile.reset();

		Core::DiffRequest request;
		request.Target = Core::DiffTarget::CommitToParent;
		request.From = *selected;
		m_Model.RequestDiff(request);
	}

	void GitView::RenderDetails()
	{
		const std::shared_ptr<const Core::Diff> diff = m_Model.GetDiff();

		if (!diff)
		{
			ImGui::TextUnformatted("Select a commit.");
			return;
		}

		const float half = ImGui::GetContentRegionAvail().y * 0.4f;

		if (ImGui::BeginChild("files", ImVec2{0.0f, half}))
		{
			for (const Core::FileDiff& file : diff->Files)
			{
				const std::filesystem::path& path =
					file.NewPath.empty() ? file.OldPath : file.NewPath;

				const bool selected =
					m_SelectedFile.has_value() && *m_SelectedFile == path;

				if (ImGui::Selectable(path.string().c_str(), selected))
				{
					m_SelectedFile = path;
				}
			}
		}

		ImGui::EndChild();

		if (!m_SelectedFile.has_value() && !diff->Files.empty())
		{
			const Core::FileDiff& first = diff->Files.front();
			m_SelectedFile = first.NewPath.empty() ? first.OldPath : first.NewPath;
		}

		if (!m_SelectedFile.has_value())
		{
			return;
		}

		const std::optional<std::size_t> index = IndexOfFile(*diff, *m_SelectedFile);

		if (!index.has_value())
		{
			// The refresh that replaced the diff no longer carries the file that was
			// selected. Say so rather than drawing a different file's changes under the
			// old heading.
			ImGui::TextUnformatted("That file is not in this diff.");
			return;
		}

		m_Diff.Render(diff->Files[*index], ReadGitPalette(m_Scheme.Get()));
	}
}
