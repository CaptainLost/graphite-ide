-- libgit2 is vendored without running its CMake configuration (see
-- vendor/libgit2-config/README-equivalent comments in git2_features.h).
-- File lists mirror src/util/CMakeLists.txt and src/libgit2/CMakeLists.txt
-- for a Windows x64 build with threads, bundled regex, bundled zlib, and
-- the bundled HTTP parser. Note this is *not* a network-free build: the
-- http:// and git:// smart transports are referenced unconditionally by
-- src/libgit2/transport.c's static transport table (only ssh:// is behind
-- an #ifdef), so they always compile in; there is simply no TLS backend
-- configured, so https:// fetches fail at runtime by design. See
-- task-1-report.md for the full derivation and every adjustment made
-- while getting this to link.

project "libgit2"
    kind "StaticLib"
    language "C"
    cdialect "C99"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        -- src/util (src/util/CMakeLists.txt: UTIL_SRC glob)
        "%{wks.location}/vendor/libgit2/src/util/*.c",
        "%{wks.location}/vendor/libgit2/src/util/*.h",
        "%{wks.location}/vendor/libgit2/src/util/allocators/*.c",
        "%{wks.location}/vendor/libgit2/src/util/allocators/*.h",

        -- src/libgit2 (src/libgit2/CMakeLists.txt: SRC_GIT2 glob), minus
        -- streams/ and transports/, which CMake's glob would sweep in
        -- wholesale. Both directories are added back file-by-file below,
        -- each only because the linker demanded that specific symbol --
        -- see the comments by each entry and task-1-report.md for the
        -- full trail.
        "%{wks.location}/vendor/libgit2/src/libgit2/*.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/*.h",

        -- transports/local.c: the built-in "file://" transport is wired
        -- unconditionally into transport.c's static transport table, so
        -- the linker needs git_transport_local even with no network
        -- transports enabled.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/local.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/local.h",

        -- transports/smart*.c: git_transport_smart (used by transport.c's
        -- table for git://, http:// and https://) lives here, unconditionally.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/smart.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/smart.h",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/smart_pkt.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/smart_protocol.c",

        -- transports/git.c: git_smart_subtransport_git, the git:// smart
        -- subtransport referenced unconditionally by transport.c's table.
        -- Needs only sockets, no HTTP parser.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/git.c",

        -- streams/registry.c + streams/tls.c: git_stream_registry_global_init
        -- is called unconditionally from git_libgit2_init()'s static init
        -- table (src/libgit2/libgit2.c), so this is required even though we
        -- never open a TLS stream.
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/registry.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/registry.h",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/tls.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/tls.h",

        -- streams/socket.c: git_socket_stream_global_init is likewise called
        -- unconditionally from git_libgit2_init(); it is also the transport
        -- transports/git.c uses to open its plain TCP connection.
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/socket.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/socket.h",

        -- streams/openssl.c, streams/mbedtls.c: git_openssl_stream_global_init
        -- and git_mbedtls_stream_global_init are called unconditionally from
        -- git_libgit2_init(). With GIT_OPENSSL/GIT_MBEDTLS undefined, both
        -- files compile down to a trivial stub (`#else` branch) that needs
        -- no TLS library.
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/openssl.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/openssl.h",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/mbedtls.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/streams/mbedtls.h",

        -- transports/ssh_libssh2.c: git_transport_ssh_libssh2_global_init is
        -- called unconditionally from git_libgit2_init(). With GIT_SSH
        -- undefined it compiles down to a trivial stub, needing no libssh2.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/ssh_libssh2.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/ssh_libssh2.h",

        -- transports/http.c: git_smart_subtransport_http and
        -- git_http__expect_continue, referenced unconditionally by
        -- transport.c's table (http_subtransport_definition is not behind
        -- any #ifdef). Needs the bundled llhttp parser (deps/llhttp, below)
        -- since no system HTTP parser is vendored.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/http.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/http.h",

        -- transports/httpclient.c: git_http_client_* — http.c's HTTP request
        -- state machine, wired to the bundled llhttp parser via httpparser.c.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/httpclient.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/httpclient.h",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/httpparser.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/httpparser.h",

        -- transports/credential.c: git_credential_free/_userpass_plaintext_new/
        -- _default_new — http.c applies URL-embedded credentials and needs
        -- the credential object's own lifecycle functions.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/credential.c",

        -- transports/auth.c: git_http_auth_basic, the Basic auth scheme
        -- dispatcher httpclient.c calls unconditionally.
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/auth.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/auth.h",

        -- transports/auth_sspi.c: git_http_auth_negotiate and
        -- git_http_auth_ntlm. auth_negotiate.h/auth_ntlm.h resolve both to
        -- real externs (not the git_http_auth_dummy stub) whenever GIT_WIN32
        -- is defined -- regardless of GIT_GSSAPI/GIT_NTLM -- because Windows
        -- always has a native implementation available: SSPI
        -- (security.h/secur32.lib). This is why deps/ntlmclient is not
        -- needed: NTLM auth on Windows goes through auth_sspi.c, not
        -- auth_ntlmclient.c (which stays inert, gated behind GIT_NTLM, left
        -- undefined).
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/auth_sspi.c",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/auth_negotiate.h",
        "%{wks.location}/vendor/libgit2/src/libgit2/transports/auth_ntlm.h",

        -- deps/zlib (deps/zlib/CMakeLists.txt: SRC_ZLIB glob)
        "%{wks.location}/vendor/libgit2/deps/zlib/*.c",
        "%{wks.location}/vendor/libgit2/deps/zlib/*.h",

        -- deps/xdiff (deps/xdiff/CMakeLists.txt: SRC_XDIFF glob)
        "%{wks.location}/vendor/libgit2/deps/xdiff/*.c",
        "%{wks.location}/vendor/libgit2/deps/xdiff/*.h",

        -- deps/llhttp (deps/llhttp/CMakeLists.txt: SRC_LLHTTP glob). This is
        -- libgit2's own bundled HTTP parser: three self-contained .c files
        -- with no includes beyond <stdio.h>/<stdlib.h>/<string.h> and no
        -- system library, used because transports/http.c is unconditionally
        -- required (see above) and GIT_HTTPPARSER_BUILTIN is the CMake
        -- default when no system parser is requested.
        "%{wks.location}/vendor/libgit2/deps/llhttp/*.c",
        "%{wks.location}/vendor/libgit2/deps/llhttp/*.h",

        -- deps/pcre: explicit file list, matching PCRE_SOURCES +
        -- PCREPOSIX_SOURCES in deps/pcre/CMakeLists.txt. Deliberately NOT a
        -- wildcard: pcre_printint.c exists in the directory but is not part
        -- of PCRE_SOURCES (it's a debug pretty-printer, excluded from the
        -- library build upstream too).
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_byte_order.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_chartables.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_compile.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_config.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_dfa_exec.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_exec.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_fullinfo.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_get.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_globals.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_jit_compile.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_maketables.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_newline.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_ord2utf8.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_refcount.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_string_utils.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_study.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_tables.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_ucd.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_valid_utf8.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_version.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcre_xclass.c",
        "%{wks.location}/vendor/libgit2/deps/pcre/pcreposix.c"
    }

    includedirs
    {
        -- pcre-config must come first: deps/pcre's sources do
        -- #include "config.h" (quoted), and vendor/libgit2/src/libgit2 also
        -- happens to contain an unrelated config.h (the git-config-file
        -- API header) that would otherwise shadow it if searched first.
        "%{wks.location}/vendor/libgit2-config/pcre-config",
        "%{wks.location}/vendor/libgit2/src/libgit2",
        "%{wks.location}/vendor/libgit2/src/util",
        "%{wks.location}/vendor/libgit2/include",
        "%{wks.location}/vendor/libgit2/deps/zlib",
        "%{wks.location}/vendor/libgit2/deps/pcre",
        "%{wks.location}/vendor/libgit2/deps/xdiff",
        "%{wks.location}/vendor/libgit2/deps/llhttp",
        "%{IncludeDir.libgit2Config}"
    }

    defines
    {
        -- Bundled PCRE reads its generated config.h when this is set
        -- (deps/pcre/CMakeLists.txt: add_definitions(-DHAVE_CONFIG_H)).
        "HAVE_CONFIG_H"
    }

    filter "system:windows"
        systemversion "latest"
        files
        {
            "%{wks.location}/vendor/libgit2/src/util/win32/*.c",
            "%{wks.location}/vendor/libgit2/src/util/win32/*.h",
            "%{wks.location}/vendor/libgit2/src/util/hash/win32.c",
            "%{wks.location}/vendor/libgit2/src/util/hash/win32.h"
        }
        defines
        {
            "_CRT_SECURE_NO_WARNINGS",
            "_CRT_SECURE_NO_DEPRECATE",
            "WIN32"
        }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        runtime "Release"
        optimize "on"
        symbols "off"
