//! Builds the parts of the repository that run on Linux, from Windows, with the pinned zig
//! (scripts/Get-Zig.ps1, ADR 0693): libgit2 with Linux defines, and the headless part of Core
//! with its doctest cases as CoreLinuxTests. Everything targets x86_64-linux-musl and links
//! statically, so the binaries do not depend on the distro's glibc.
//!
//!   vendor\bin\zig\zig.exe build -Dconfiguration=Debug
//!
//! puts the test binary at bin/<Configuration>-linux-x86_64/CoreLinuxTests/CoreLinuxTests.
//! Which Core folders and files are in the Linux set is decided by the lists below; a folder is
//! walked, as premake globs it, minus what is excluded.

const std = @import("std");

const Configuration = enum { Debug, Release };

// The headless part of Core: folders walked for .cpp files, and what is left out of them.
const core_source_folders = [_][]const u8{
    "Core/src/Core/Base",
    "Core/src/Core/Git",
    "Core/src/Core/Remote",
};
// Only FileSystem's paths, listing, reading and atomic writes: the watcher, locks, holds and
// file operations are Windows-only until the WSL helper needs them.
const core_source_files = [_][]const u8{
    "Core/src/Core/FileSystem/AtomicWrite.cpp",
    "Core/src/Core/FileSystem/FileName.cpp",
    "Core/src/Core/FileSystem/FileSystem.cpp",
    "Core/src/Core/FileSystem/RelativePath.cpp",
    "Core/src/Core/FileSystem/WslSharePath.cpp",
};
const core_excluded = [_][]const u8{
    // Starts git.exe through ChildProcess, which has no Linux version until the WSL helper
    // needs one.
    "Core/src/Core/Git/Command/",
    // The host's side of the agent: it starts wsl.exe, so it is Windows-only.
    "Core/src/Core/Remote/AgentConnection.cpp",
    "Core/src/Core/Remote/AgentDeployment.cpp",
};

const core_test_folders = [_][]const u8{
    "Core/tests/src/Base",
    "Core/tests/src/Git",
    "Core/tests/src/Remote",
};
const core_test_files = [_][]const u8{
    "Core/tests/src/Main.cpp",
    "Core/tests/src/FileSystem/AtomicWriteTests.cpp",
    "Core/tests/src/FileSystem/FileNameTests.cpp",
    "Core/tests/src/FileSystem/FileSystemTests.cpp",
    "Core/tests/src/FileSystem/RelativePathTests.cpp",
    "Core/tests/src/FileSystem/WslSharePathTests.cpp",
};
const core_test_excluded = [_][]const u8{
    "Core/tests/src/Git/Command/",
    "Core/tests/src/Remote/AgentConnectionTests.cpp",
    "Core/tests/src/Remote/AgentDeploymentTests.cpp",
    "Core/tests/src/Remote/AgentEndToEndTests.cpp",
};

const core_flags = [_][]const u8{
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-Werror",
    // A designated initializer that leaves out fields with defaults is this codebase's idiom
    // (Core::LogSpec{.ConsoleCapacity = 16}); MSVC never warns about it.
    "-Wno-missing-field-initializers",
    // Sources include "corepch.h" first, as MSVC's precompiled header requires; here it is an
    // ordinary header.
};

pub fn build(b: *std.Build) void {
    const configuration = b.option(Configuration, "configuration", "Debug or Release (default Debug)") orelse .Debug;

    const target = b.resolveTargetQuery(.{ .cpu_arch = .x86_64, .os_tag = .linux, .abi = .musl });
    const optimize: std.builtin.OptimizeMode = switch (configuration) {
        .Debug => .Debug,
        .Release => .ReleaseSafe,
    };

    const libgit2 = addLibgit2(b, target, optimize);

    const tests_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    for ([_][]const u8{ "Core/src", "Core/tests/src", "Core/tests/fixtures", "vendor/doctest", "vendor/libgit2/include", "vendor/spdlog/include", "vendor/json/single_include" }) |include| {
        tests_module.addIncludePath(b.path(include));
    }
    switch (configuration) {
        .Debug => tests_module.addCMacro("CORE_DEBUG", "1"),
        .Release => {
            tests_module.addCMacro("CORE_RELEASE", "1");
            tests_module.addCMacro("NDEBUG", "1");
        },
    }
    tests_module.addCSourceFiles(.{
        .files = collectSources(b, &core_source_folders, &core_source_files, &core_excluded),
        .flags = &core_flags,
    });
    tests_module.addCSourceFiles(.{
        .files = collectSources(b, &core_test_folders, &core_test_files, &core_test_excluded),
        .flags = &core_flags,
    });
    tests_module.linkLibrary(libgit2);

    const tests = b.addExecutable(.{
        .name = "CoreLinuxTests",
        .root_module = tests_module,
        .linkage = .static,
    });

    const output_folder = b.fmt("../bin/{s}-linux-x86_64/CoreLinuxTests", .{@tagName(configuration)});
    const install = b.addInstallFileWithDir(tests.getEmittedBin(), .{ .custom = output_folder }, "CoreLinuxTests");
    b.getInstallStep().dependOn(&install.step);
}

/// Every .cpp under each folder (recursively), plus the listed files, minus the excluded paths.
/// Paths are relative to the repository root, with forward slashes.
fn collectSources(b: *std.Build, folders: []const []const u8, files: []const []const u8, excluded: []const []const u8) []const []const u8 {
    const io = b.graph.io;
    var found: std.ArrayList([]const u8) = .empty;

    for (folders) |folder| {
        var dir = b.root.root_dir.handle.openDir(io, folder, .{ .iterate = true }) catch |err|
            std.debug.panic("cannot open '{s}': {s}", .{ folder, @errorName(err) });
        defer dir.close(io);

        var walker = dir.walk(b.allocator) catch @panic("OOM");
        defer walker.deinit();

        // zig caches what this function found; without these a file added to a folder would
        // be missed until build.zig itself changed.
        b.dependOnDirectoryContents(b.path(folder));

        while (walker.next(io) catch |err| std.debug.panic("cannot walk '{s}': {s}", .{ folder, @errorName(err) })) |entry| {
            const relative = std.mem.replaceOwned(u8, b.allocator, entry.path, "\\", "/") catch @panic("OOM");
            const path = b.fmt("{s}/{s}", .{ folder, relative });

            if (entry.kind == .directory) {
                b.dependOnDirectoryContents(b.path(path));
                continue;
            }
            if (entry.kind != .file or !std.mem.endsWith(u8, entry.basename, ".cpp")) continue;

            if (isExcluded(path, excluded)) continue;
            found.append(b.allocator, path) catch @panic("OOM");
        }
    }

    for (files) |file| found.append(b.allocator, file) catch @panic("OOM");

    // A walk's order is the file system's; a sorted list keeps the build's inputs stable.
    std.mem.sort([]const u8, found.items, {}, lessThan);
    return found.items;
}

/// An excluded entry ending in '/' excludes a whole folder.
fn isExcluded(path: []const u8, excluded: []const []const u8) bool {
    for (excluded) |entry| {
        if (std.mem.endsWith(u8, entry, "/")) {
            if (std.mem.startsWith(u8, path, entry)) return true;
        } else if (std.mem.eql(u8, path, entry)) {
            return true;
        }
    }
    return false;
}

fn lessThan(_: void, left: []const u8, right: []const u8) bool {
    return std.mem.lessThan(u8, left, right);
}

/// Every .c directly in the folder, not below it.
fn cFilesIn(b: *std.Build, folder: []const u8) []const []const u8 {
    const io = b.graph.io;
    var found: std.ArrayList([]const u8) = .empty;

    var dir = b.root.root_dir.handle.openDir(io, folder, .{ .iterate = true }) catch |err|
        std.debug.panic("cannot open '{s}': {s}", .{ folder, @errorName(err) });
    defer dir.close(io);

    b.dependOnDirectoryContents(b.path(folder));

    var iterator = dir.iterate();
    while (iterator.next(io) catch |err| std.debug.panic("cannot list '{s}': {s}", .{ folder, @errorName(err) })) |entry| {
        if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, ".c")) continue;
        found.append(b.allocator, b.fmt("{s}/{s}", .{ folder, entry.name })) catch @panic("OOM");
    }

    std.mem.sort([]const u8, found.items, {}, lessThan);
    return found.items;
}

/// libgit2 for Linux. The file list mirrors vendor/premake/libgit2.lua, which documents why each
/// transport and stream file is there; the Windows-only pieces are swapped for their Unix ones
/// (util/unix, collision-detecting SHA-1, built-in SHA-256, poll). The defines are what CMake's
/// configuration would have written to git2_features.h for this target.
fn addLibgit2(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        // libgit2 is vendor code, built as on Windows: not ours to hold to the sanitizer.
        .sanitize_c = .off,
    });

    for ([_][]const u8{ "src/libgit2", "src/util", "include", "deps/zlib", "deps/pcre", "deps/xdiff", "deps/llhttp" }) |include| {
        module.addIncludePath(b.path(b.fmt("vendor/libgit2/{s}", .{include})));
    }

    const defines = [_][2][]const u8{
        .{ "LIBGIT2_NO_FEATURES_H", "1" },
        .{ "GIT_THREADS", "1" },
        .{ "GIT_ARCH_64", "1" },
        .{ "GIT_USE_NSEC", "1" },
        .{ "GIT_USE_STAT_MTIM", "1" },
        .{ "GIT_USE_FUTIMENS", "1" },
        .{ "GIT_REGEX_BUILTIN", "1" },
        .{ "GIT_IO_POLL", "1" },
        .{ "GIT_SHA1_COLLISIONDETECT", "1" },
        .{ "GIT_SHA256_BUILTIN", "1" },
        .{ "GIT_RAND_GETENTROPY", "1" },
        .{ "GIT_HTTPPARSER_BUILTIN", "1" },
        .{ "SHA1DC_NO_STANDARD_INCLUDES", "1" },
        .{ "SHA1DC_CUSTOM_INCLUDE_SHA1_C", "\"git2_util.h\"" },
        .{ "SHA1DC_CUSTOM_INCLUDE_UBC_CHECK_C", "\"git2_util.h\"" },
        .{ "_GNU_SOURCE", "1" },
        .{ "HAVE_SYS_TYPES_H", "1" },
        .{ "HAVE_SYS_STAT_H", "1" },
        .{ "HAVE_STDINT_H", "1" },
        .{ "HAVE_INTTYPES_H", "1" },
        .{ "HAVE_MEMMOVE", "1" },
        .{ "HAVE_STRERROR", "1" },
        .{ "HAVE_STRTOLL", "1" },
        .{ "HAVE_LONG_LONG", "1" },
        .{ "HAVE_UNSIGNED_LONG_LONG", "1" },
        .{ "SUPPORT_PCRE8", "1" },
        .{ "NO_RECURSE", "1" },
        .{ "NEWLINE", "10" },
        .{ "POSIX_MALLOC_THRESHOLD", "10" },
        .{ "LINK_SIZE", "2" },
        .{ "PARENS_NEST_LIMIT", "250" },
        .{ "MATCH_LIMIT", "10000000" },
        .{ "MATCH_LIMIT_RECURSION", "MATCH_LIMIT" },
        .{ "MAX_NAME_SIZE", "32" },
        .{ "MAX_NAME_COUNT", "10000" },
    };
    for (defines) |define| module.addCMacro(define[0], define[1]);

    const flags = [_][]const u8{ "-std=gnu99", "-w" };

    for ([_][]const u8{
        "vendor/libgit2/src/util",
        "vendor/libgit2/src/util/allocators",
        "vendor/libgit2/src/util/unix",
        "vendor/libgit2/src/util/hash/sha1dc",
        "vendor/libgit2/src/util/hash/rfc6234",
        "vendor/libgit2/src/libgit2",
        "vendor/libgit2/deps/zlib",
        "vendor/libgit2/deps/xdiff",
        "vendor/libgit2/deps/llhttp",
    }) |folder| {
        module.addCSourceFiles(.{ .files = cFilesIn(b, folder), .flags = &flags });
    }

    module.addCSourceFiles(.{
        .root = b.path("vendor/libgit2/src"),
        .files = &.{
            "util/hash/collisiondetect.c",
            "util/hash/builtin.c",
            "libgit2/transports/local.c",
            "libgit2/transports/smart.c",
            "libgit2/transports/smart_pkt.c",
            "libgit2/transports/smart_protocol.c",
            "libgit2/transports/git.c",
            "libgit2/streams/registry.c",
            "libgit2/streams/tls.c",
            "libgit2/streams/socket.c",
            "libgit2/streams/openssl.c",
            "libgit2/streams/mbedtls.c",
            "libgit2/transports/ssh_libssh2.c",
            "libgit2/transports/http.c",
            "libgit2/transports/httpclient.c",
            "libgit2/transports/httpparser.c",
            "libgit2/transports/credential.c",
            "libgit2/transports/auth.c",
            // Compiles to nothing off Windows, but auth.c's table names it.
            "libgit2/transports/auth_sspi.c",
        },
        .flags = &flags,
    });

    module.addCSourceFiles(.{
        .root = b.path("vendor/libgit2/deps/pcre"),
        .files = &.{
            "pcre_byte_order.c",   "pcre_chartables.c", "pcre_compile.c",      "pcre_config.c",
            "pcre_dfa_exec.c",     "pcre_exec.c",       "pcre_fullinfo.c",     "pcre_get.c",
            "pcre_globals.c",      "pcre_jit_compile.c", "pcre_maketables.c",  "pcre_newline.c",
            "pcre_ord2utf8.c",     "pcre_refcount.c",   "pcre_string_utils.c", "pcre_study.c",
            "pcre_tables.c",       "pcre_ucd.c",        "pcre_valid_utf8.c",   "pcre_version.c",
            "pcre_xclass.c",       "pcreposix.c",
        },
        .flags = &flags,
    });

    return b.addLibrary(.{ .name = "git2", .root_module = module, .linkage = .static });
}
