#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Base/Base.h"
#include "Core/Base/Timestep.h"

namespace Graphite {

	class ContentModel;
	class GitModel;
	class LinesModel;
	class NetworkModel;
	class WorktreesModel;
	class WriteConsequences;

	// The open folder's repository: the git model and the models that follow it, brought up to date every frame in the
	// order each depends on, whether or not the Git window is on screen.
	class RepositorySession
	{
	public:
		RepositorySession(GitModel& git, WriteConsequences& writes, NetworkModel& network, LinesModel& lines,
						  WorktreesModel& worktrees, ContentModel& content);

		// In this order: the git model applies what its workers finished; what a landed write changes is applied, before
		// the watcher reports the write; the network model takes Pull's fast-forward, which the git model's update lands;
		// the lines follow the branch listing and the worktrees the working directory; a new read sets the base tips the
		// authors leave out; the content checks follow the read.
		// WARNING: Before any panel reads a snapshot, so a panel never draws a model that has not landed this frame.
		void Update(Core::Timestep timestep);

	private:
		GitModel& m_Git;
		WriteConsequences& m_Writes;
		NetworkModel& m_Network;
		LinesModel& m_Lines;
		WorktreesModel& m_Worktrees;
		ContentModel& m_Content;

		// The read whose bases the authors leave out.
		Core::Ref<const LineRead> m_AuthorsHiddenFor;
	};
}
