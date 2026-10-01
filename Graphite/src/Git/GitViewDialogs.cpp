// The Git panel's modal questions: checking out over changes, naming a branch, deleting a
// branch and dropping a stash. Members of GitView, kept apart from GitView.cpp so the panel's
// drawing of refs and history does not have to be read past four dialogs.

#include "Git/GitView.h"

#include "Editor/Palettes.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		ImVec4 Scaled(const ImVec4& colour, float factor)
		{
			return ImVec4(std::min(colour.x * factor, 1.0f), std::min(colour.y * factor, 1.0f),
						  std::min(colour.z * factor, 1.0f), colour.w);
		}

		// The rows of one group in the checkout dialog, each marked with what makes it matter.
		void CheckoutRows(const char* heading, const std::vector<CheckoutQuestionRow>& rows)
		{
			if (rows.empty())
			{
				return;
			}

			ImGui::TextUnformatted(heading);
			ImGui::Indent();

			for (const CheckoutQuestionRow& row : rows)
			{
				std::string line = row.Path.generic_string();

				if (row.Unsaved)
				{
					line += " (unsaved in the editor)";
				}

				if (row.Blocks)
				{
					line += " (blocks checkout)";
				}

				ImGui::TextUnformatted(line.c_str());
			}

			ImGui::Unindent();
		}
	}

	void GitView::RenderCheckoutDialog()
	{
		if (!m_CheckoutQuestion.Begin())
		{
			m_CheckoutDialogOpen = false;
			return;
		}

		// Null for the one frame after a question was withdrawn while on screen: the popup is
		// still open, with nothing left to ask.
		const CheckoutQuestionPayload* current = m_CheckoutQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_CheckoutQuestion.End();
			m_CheckoutDialogOpen = false;
			return;
		}

		if (!m_CheckoutDialogOpen)
		{
			m_CheckoutDialogOpen = true;
			m_IncludeUntracked = current->IncludeUntracked;
			m_StashSaveFailed = false;
		}

		const CheckoutQuestion& question = current->Question;

		ImGui::TextUnformatted(question.Title.c_str());
		ImGui::Separator();

		// Every change, not only the blocking ones, since both answers act on every change -
		// so the list can be long, and scrolls rather than pushing the buttons off screen.
		const std::size_t lines = question.Changed.size() + question.Untracked.size() +
								  (question.Changed.empty() ? 0 : 1) + (question.Untracked.empty() ? 0 : 1);

		const float listHeight =
			static_cast<float>(std::min<std::size_t>(lines, 14)) * ImGui::GetTextLineHeightWithSpacing();

		const float listWidth =
			std::max(ImGui::CalcTextSize(question.Title.c_str()).x, ImGui::GetFontSize() * 28.0f);

		if (ImGui::BeginChild("checkout.files", ImVec2(listWidth, listHeight), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			CheckoutRows("Changed:", question.Changed);
			CheckoutRows("Untracked:", question.Untracked);

			// BuildCheckoutQuestion is only asked when there is something to list. Should both
			// lists ever be empty, the child still ends on an item, as every window here does.
			if (lines == 0)
			{
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}

		ImGui::EndChild();

		// Locked on when an untracked file blocks: leaving it out would leave the checkout
		// refused over the very file it was asked about.
		if (question.UntrackedForced)
		{
			bool forced = true;
			ImGui::BeginDisabled();
			ImGui::Checkbox("Include untracked files in Discard and Stash", &forced);
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::Checkbox("Include untracked files in Discard and Stash", &m_IncludeUntracked);
		}

		ImGui::Separator();

		// Copied out before Answer(), which drops the payload `current` points at.
		const Core::CheckoutTarget target = current->Target;
		const std::vector<std::filesystem::path> unsaved = current->UnsavedAbsolute;
		const bool includeUntracked = question.UntrackedForced || m_IncludeUntracked;

		const auto answer = [this] {
			m_CheckoutQuestion.Answer();

			// Let go here too, not only when Begin fails: a question queued behind this one
			// opens on the very next Begin, with no frame between them to reset on.
			m_CheckoutDialogOpen = false;
		};

		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		// Red, from the scheme's own error colour: this is the one button here that loses
		// work for good.
		const ImVec4 danger = ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_Scheme.Get()).DiagnosticError);

		ImGui::PushStyleColor(ImGuiCol_Button, danger);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Scaled(danger, 1.15f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, Scaled(danger, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

		const bool discard = ImGui::Button("Discard");

		ImGui::PopStyleColor(4);

		ImGui::SameLine();
		const bool stash = ImGui::Button("Stash");

		ImGui::EndDisabled();

		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		ImGui::TextDisabled("Discard cannot be undone.");

		if (m_StashSaveFailed)
		{
			// Which file failed is already in the status bar - see EditorBuffers::Save.
			ImGui::TextUnformatted("Could not save every file; nothing was stashed.");
		}

		if (discard)
		{
			// The editor's copies first: the checkout is about to write over the files, and a
			// buffer still holding the old edits would then disagree with the disk.
			m_Buffers.Revert(unsaved);
			RequestCheckout(CheckoutRequest{.Target = target,
											.Preparation = CheckoutPreparation::Discard,
											.IncludeUntracked = includeUntracked},
							unsaved);
			answer();
		}
		else if (stash)
		{
			// A stash takes what is on disk, so the unsaved edits have to get there first - and
			// if any cannot, stashing the rest would leave those edits behind on this branch.
			if (!m_Buffers.Save(unsaved))
			{
				m_StashSaveFailed = true;
			}
			else
			{
				RequestCheckout(CheckoutRequest{.Target = target,
												.Preparation = CheckoutPreparation::Stash,
												.IncludeUntracked = includeUntracked,
												.StashMessage = "Before checking out " + CheckoutTargetLabel(target)},
								unsaved);
				answer();
			}
		}
		else if (cancel)
		{
			answer();
		}

		m_CheckoutQuestion.End();
	}

	void GitView::RenderBranchNameDialog(ModalQuestion<BranchNamePayload>& question)
	{
		if (!question.Begin())
		{
			if (m_BranchNameFor == &question)
			{
				m_BranchNameFor = nullptr;
			}

			return;
		}

		const BranchNamePayload* current = question.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			question.End();
			m_BranchNameFor = nullptr;
			return;
		}

		const bool first = m_BranchNameFor != &question;

		if (first)
		{
			m_BranchNameFor = &question;
			m_BranchNameBuffer = current->Buffer;
			m_CheckoutAfter = current->CheckoutAfter;
		}

		// Copied out before Answer(), which drops the payload `current` points at.
		const BranchNameMode mode = current->Mode;
		const std::string from = current->From;
		const Core::Oid at = current->At;

		ImGui::TextUnformatted((mode == BranchNameMode::New ? "New branch from " + from : "Rename " + from).c_str());

		// The field is what the dialog is for, so it takes the keyboard as it opens.
		if (first)
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
		const bool entered = ImGui::InputText("##branchName", m_BranchNameBuffer.data(), m_BranchNameBuffer.size(),
											  ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string name(m_BranchNameBuffer.data());

		const std::shared_ptr<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::optional<std::string> problem =
			BranchNameProblem(name, branches ? *branches : std::vector<Core::Branch>{});

		// Drawn even when empty, so the dialog does not change height under the reader's typing.
		ImGui::TextDisabled("%s", problem.has_value() ? problem->c_str() : "");

		if (mode == BranchNameMode::New)
		{
			ImGui::Checkbox("Checkout after creating", &m_CheckoutAfter);
		}

		ImGui::Separator();

		const bool acceptable = !problem.has_value() && !m_Model.IsWriteInFlight();

		ImGui::BeginDisabled(!acceptable);
		const bool ok = ImGui::Button("OK");
		ImGui::EndDisabled();

		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		if (acceptable && (ok || entered))
		{
			if (mode == BranchNameMode::New)
			{
				m_Model.RequestCreateBranch(name, at);

				// The same way as any other checkout - planned, and asked about when it would
				// overwrite something. The plan queues behind the create, so it finds the branch.
				if (m_CheckoutAfter)
				{
					BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = name});
				}
			}
			else
			{
				m_Model.RequestRenameBranch(from, name);
			}

			question.Answer();
			m_BranchNameFor = nullptr;
		}
		else if (cancel)
		{
			question.Answer();
			m_BranchNameFor = nullptr;
		}

		question.End();
	}

	void GitView::RenderDeleteBranchDialog()
	{
		if (!m_DeleteBranchQuestion.Begin())
		{
			return;
		}

		const DeleteBranchPayload* current = m_DeleteBranchQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_DeleteBranchQuestion.End();
			return;
		}

		const std::string branch = current->Branch;
		const std::size_t unmerged = current->Unmerged;

		ImGui::TextUnformatted(("Delete branch " + branch + "?").c_str());

		if (unmerged == 0)
		{
			ImGui::TextDisabled("This branch has no commits that other branches lack.");
		}
		else
		{
			ImGui::TextUnformatted(
				std::format("{} commit(s) on this branch are on no other branch and will be lost.", unmerged).c_str());
		}

		ImGui::Separator();

		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		// Worded for what it costs: past this button, those commits have no name left.
		if (ImGui::Button(unmerged == 0 ? "Delete" : "Delete anyway"))
		{
			m_Model.RequestDeleteBranch(branch);
			m_DeleteBranchQuestion.Answer();
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DeleteBranchQuestion.Answer();
		}

		m_DeleteBranchQuestion.End();
	}

	void GitView::RenderDropStashDialog()
	{
		if (!m_DropStashQuestion.Begin())
		{
			return;
		}

		const DropStashPayload* current = m_DropStashQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_DropStashQuestion.End();
			return;
		}

		const Core::Oid id = current->Id;

		ImGui::TextUnformatted(std::format("Drop stash@{{{}}}?", current->Index).c_str());
		ImGui::TextDisabled("A dropped stash cannot be recovered.");

		ImGui::Separator();

		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (ImGui::Button("Drop"))
		{
			m_Model.RequestStashDrop(id);
			m_DropStashQuestion.Answer();
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DropStashQuestion.Answer();
		}

		m_DropStashQuestion.End();
	}
}
