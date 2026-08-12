#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite
{
    /// Names deliberately mirror microsoft/vs-solutionpersistence — SolutionModel,
    /// SolutionProjectModel, SolutionFolderModel — so the two can be read side by side.
    /// That library is the canonical implementation of both formats but is C# on .NET, so
    /// it serves here as a specification rather than as a dependency.

    /// A file as a project declares it. The path is absolute; the name is what to show.
    struct ContentFile
    {
        std::string name;
        std::filesystem::path path;
    };

    /// A logical folder inside a project. For C++ these come from .vcxproj.filters and
    /// need not match anything on disk — "Panels" is a filter, not a directory.
    struct ContentFolder
    {
        std::string name;
        std::vector<ContentFolder> folders;
        std::vector<ContentFile> files;
    };

    /// What a reader found inside one project file.
    struct ProjectContent
    {
        ContentFolder root;

        /// ProjectReference entries from inside the project file — what Visual Studio
        /// shows under "References". Not the same as Project::dependencies.
        std::vector<std::string> references;
    };

    struct Project
    {
        std::string displayName;
        std::filesystem::path path;
        std::string typeId;

        /// Dependencies declared by the *solution* rather than by the project. Rarely
        /// used, and distinct from ProjectContent::references, which they are easy to
        /// confuse with.
        std::vector<std::string> dependencies;

        ProjectContent content;

        /// False when no reader understood this project type. The project still appears,
        /// as a leaf.
        bool contentRead = false;

        /// False when the solution names a project file that is not on disk. Shown
        /// anyway, and marked: a project the solution expects and cannot find is exactly
        /// the kind of problem this view exists to reveal, so hiding it would defeat it.
        bool exists = true;
    };

    /// A solution folder: a grouping that exists only in the solution file and
    /// corresponds to no directory on disk.
    ///
    /// Folders own their children rather than children pointing at their parent, which is
    /// the other divergence from the reference model. Everything here is a value, so a
    /// parent pointer would dangle the first time the tree was copied or moved, and
    /// rendering only ever walks downwards.
    struct Folder
    {
        std::string name;
        std::vector<Folder> folders;
        std::vector<Project> projects;
    };

    struct Solution
    {
        std::filesystem::path path;
        std::string name;
        Folder root;
        std::size_t projectCount = 0;
    };
}
