#pragma once

#include "Core/Base/Base.h"

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LspClient.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Language/Server/ServerLaunch.h"

#include "Core/FileSystem/FileHold.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <mutex>
#include <optional>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstddef>

namespace Graphite {

	// How long to wait before trying again after the Nth restart of a server that keeps dying —
	// zero for the first attempt, growing by two seconds each time after. Free and pure so the
	// backoff itself is checkable without a process: Poll only ever compares a clock against a
	// point computed from this, it never sleeps, because Poll runs on the UI thread once a frame
	// and a wait implemented by blocking would freeze the application for the length of it.
	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts);

	// How many times a server that keeps dying is restarted before it is left at Failed.
	inline constexpr std::size_t kMaximumRestarts = 3;

	// How long a server must stay up (Ready, Indexing or without a compile database) before its
	// restarts are forgiven. Without this the workspace server, which is never reaped, would
	// spend its budget on crashes spread over a whole session and stay dead until the
	// application restarted.
	inline constexpr std::chrono::minutes kStableAfter{2};

	// Whether a server that has already been restarted `restarts` times may be restarted again.
	bool MayRestart(std::size_t restarts);

	// Whether a server up since `upSince` (empty when it is down) has been stable long
	// enough, at `now`, to have its restart count reset to zero.
	bool RestartBudgetRefilled(std::optional<std::chrono::steady_clock::time_point> upSince,
							   std::chrono::steady_clock::time_point now);

	// How long a server outside the open folder may have nothing to do before it is shut down.
	// Long enough that closing a file to reopen it, or stepping through a run of F12 targets, does
	// not restart clangd each time; short enough that a one-off look at a stray file does not keep
	// a server of about 100 MB alive for the rest of the session.
	inline constexpr std::chrono::minutes kIdleShutdown{2};

	// Owns every language server the application runs: at most one `LspClient` per
	// (language, project root), started the first time a document in that language and root is
	// seen, restarted a bounded number of times if its process dies, and shut down when this
	// object is destroyed.
	class LanguageServices
	{
	public:
		// `fallbackRoot` is the root of a file that lies inside it but under no recognised project
		// layout — ordinarily the workspace root. A file with no marker above it that lies outside
		// the fallback root is rooted at the root of the server that named it, if one did, and at
		// its own directory otherwise (see Resolve).
		explicit LanguageServices(std::filesystem::path fallbackRoot);

		// Follows the open folder. Forgets every root resolved so far, since a directory that fell
		// back to the old root, or to itself, may belong to the new one.
		void SetFallbackRoot(std::filesystem::path root);

		// Records that the server serving `by` named `named`, so `named`, if it has no project of
		// its own, resolves to that server's root. Poll records every answered location this way;
		// public so the rule can be exercised without a live server.
		void NoteNamed(const std::filesystem::path& named, const std::filesystem::path& by, LanguageId language);

		~LanguageServices();

		LanguageServices(const LanguageServices&) = delete;
		LanguageServices& operator=(const LanguageServices&) = delete;

		// Starts a server for this file's language if one is configured and none runs for its
		// root yet. Safe to call every time a document opens — and every frame a document stays
		// active, which is how the editor actually calls it: resolving the config and the root is
		// cheap, and an entry already present makes the rest of the call a single map lookup.
		void EnsureStarted(const std::filesystem::path& path, LanguageId language);

		// Starts a server for every language that has one and that this tree actually contains,
		// without waiting for a document of that language to be opened.
		//
		// Lazy starting is cheap but leaves the index cold at exactly the wrong moment: the first
		// file you open is the first thing the server has to index, so the counts arrive late on
		// the very file being read. Warming on the folder moves that work to the minutes nobody
		// is waiting.
		//
		// The walk runs on a worker, never here: a tree can hold any number of files, and the
		// design's one standing rule is that nothing blocks the UI thread. It stops as soon as it
		// has seen a file of every configured language, so an ordinary source tree costs a
		// fraction of a full walk; a tree with no such file costs one full walk, once, on a
		// thread nobody is waiting for.
		//
		// Servers actually start in `Poll`, on the UI thread, from what the walk found — this
		// only sets the walk going. Calling it again cancels a walk still running, which is what
		// makes switching folders quickly cost one walk rather than a queue of them.
		void WarmUp(std::filesystem::path root, bool respectGitIgnore);

		// Asked once per language and remembered until the provider is replaced or Reconfigure asks again; the recommended server for every language until set.
		// A server already running keeps the choice it started with; Reconfigure applies a new one.
		using ChoiceProvider = std::function<ServerChoice(LanguageId language)>;

		void SetChoiceProvider(ChoiceProvider provider);

		// Shuts down every server of `language` and opens the documents they held on what is chosen now. Call it when the choice, path, arguments, initialization options or settings change.
		void Reconfigure(LanguageId language);

		// Decides whether a server may run for a project root - whether its folder is trusted.
		// A refused root still gets an entry, in ServerState::Restricted, so the documents opened
		// against it are remembered for the moment it is permitted. Permits every root until set.
		using RootPermission = std::function<bool(const std::filesystem::path& root)>;

		void SetRootPermission(RootPermission permission);

		// Where Poll reads the time from; the steady clock until set. A seam so the idle delay can
		// be tested without waiting for it.
		using Clock = std::function<std::chrono::steady_clock::time_point()>;

		void SetClock(Clock clock);

		// Builds the client for a server whose binary has been found, both at first start and at
		// every restart; a real LspClient over that binary until set. A seam so the restart path -
		// a death noticed, the backoff, the replay, the cap - can be driven with clients that have
		// no process behind them (LspClient::WithoutServer) instead of a server that dies on cue.
		using ClientFactory = std::function<Core::Scope<LspClient>(const ServerLaunch& launch, const std::filesystem::path& root)>;

		void SetClientFactory(ClientFactory factory);

		// Where Graphite installs servers; nothing is looked for there until set.
		void SetServersDirectory(std::filesystem::path directory);

		// Asked at most once until Reconfigure, which asks again so a runtime installed since is seen.
		using DotnetLocator = std::function<std::vector<DotnetInstallation>()>;

		void SetDotnetLocator(DotnetLocator locator);

		// Filled into a server argument that names {logs}; the system's temporary directory until set.
		void SetLogDirectory(std::filesystem::path directory);

		// Restarts every server whose adapter follows the selected solution; cheap when nothing changed, so it may be called every frame.
		void SetSelectedSolution(const std::filesystem::path& solution);

		// Whether a server entry exists for the key this file resolves to, whether or not it has a
		// process. What shows that an idle server has been shut down.
		bool IsRunningFor(const std::filesystem::path& path, LanguageId language) const;

		// Brings every server into line with the permission after it has changed: a restricted
		// server whose root is now permitted starts and is told every document it was sent while
		// restricted; a server whose root is now refused is shut down, its outstanding tickets
		// answered Failed, and left restricted. Cheap when nothing changed - call it on a change
		// rather than every frame all the same.
		void Revalidate();

		// The status of some server for this language, Off when its server is turned off, or a
		// default (State == NotConfigured) if none has ever been started. When more than one root is running the same language, this
		// reports whichever the map happens to order first — every caller in this codebase works
		// with one project root at a time, so that ambiguity has never mattered in practice.
		// A caller with a document in hand asks the overload below instead, since a trusted
		// workspace and an untrusted file opened beside it are two roots of the same language.
		ServerStatus StatusFor(LanguageId language) const;

		// Whether a server is chosen for `language`, which is what a command offering go to definition asks. Says
		// nothing about whether one is installed or running.
		bool HasChosenServer(LanguageId language) const;

		const ServerDefinition* ChosenServer(LanguageId language) const;

		bool OffersSwitchSourceHeader(LanguageId language) const;

		// The status of the server for this document's own (language, root), Off when its server
		// is turned off, or a default (State == NotConfigured) when there is none. What a caller with a document in hand
		// asks, rather than the overload above: once two roots run the same language - a trusted
		// workspace and one file opened from an untrusted folder elsewhere - the first in map
		// order is the wrong answer for half the tabs. Not const: resolving the root consults and
		// fills the root cache, as it does for every other per-document method.
		ServerStatus StatusFor(const std::filesystem::path& path, LanguageId language);

		// The state of the workspace's language servers as a whole, for the status bar to fall
		// back on when the active document's own language has nothing to say — which is most of
		// the time, since a folder starts its servers before any document of theirs is opened,
		// and a README is a perfectly ordinary thing to be looking at while one indexes behind it.
		//
		// Busy first (Starting, Indexing), then broken (Failed, NotFound, ProjectIncomplete),
		// then Ready, then Restricted. Ready is included: a server that is up and answering is
		// worth saying so, the slot is empty anyway, and leaving it silent means the bar comes
		// alive only when a file of that language is opened — which reads as the feature not
		// working rather than as a deliberately quiet resting state. Restricted comes last because
		// it may be one file opened from another folder, and it is said in that file's own tabs.
		//
		// Nothing at all when no server has been started, which is the one case with genuinely
		// nothing to report.
		std::optional<ServerStatus> OverallStatus() const;

		// UI thread, once a frame: collects each running client's replies and status, notices a
		// process that has died, and restarts it once its backoff has elapsed. Never blocks. This
		// is the only place any `LspClient::Drain` is called — see `TakeAnswers`.
		void Poll();

		// Unique across every server this owns, unlike `LspClient::RequestId`, which is numbered
		// per client and restarts at 1 for each one — two different servers would otherwise hand
		// out the same number, and a consumer matching on that alone would attribute one server's
		// answer to the other's question. A consumer holds one of these and matches `Answer::For`
		// against it.
		enum class Ticket : std::uint64_t
		{
		};

		// Each returns a ticket immediately; the answer arrives through `TakeAnswers`. A ticket
		// for a language with no configured server, or for a root whose server never got a
		// process, is answered `Failed` on the very next `TakeAnswers` — there is no such thing as
		// a ticket nobody ever hears back about.
		Ticket AskDefinition(const std::filesystem::path& path, LanguageId language,
							 const FilePosition& where);
		Ticket AskReferences(const std::filesystem::path& path, LanguageId language,
							 const FilePosition& where);
		Ticket AskHover(const std::filesystem::path& path, LanguageId language,
						const FilePosition& where);
		Ticket AskSwitchSourceHeader(const std::filesystem::path& path, LanguageId language);
		Ticket AskSymbols(const std::filesystem::path& path, LanguageId language);
		Ticket AskSemanticTokens(const std::filesystem::path& path, LanguageId language);

		struct Answer
		{
			Ticket For{};
			std::vector<Location> Locations;
			std::vector<Declaration> Declarations;
			std::vector<SemanticToken> SemanticTokens;
			// The hover reply's markdown; empty for every other question, and for a hover with
			// nothing to say.
			std::string Text;
			bool Failed = false;
		};

		// Moves everything answered since the last call into `answers`. UI thread only, and the
		// only place answers leave this class.
		//
		// `LspClient::Drain` *consumes* what it returns — a second caller draining the same
		// client would see nothing, because the first caller already took it. `Poll` has to call
		// `Drain` on every client it owns to notice a dead process, so it is also the only place
		// that may collect answers; if a second caller drained a client directly, answers would
		// vanish at random depending on which caller ran first in a frame, which is the worst
		// failure shape there is — indistinguishable from a server that occasionally forgets to
		// answer. `TakeAnswers` is therefore the only way anything outside this class ever sees an
		// answer, and there is deliberately no accessor to a raw `LspClient` for a second drain
		// loop to form around.
		void TakeAnswers(std::vector<Answer>& answers);

		// UI thread only, once a frame, and - like TakeAnswers, and for the same reason - the
		// only place diagnostics leave this class. Poll drains every client because it has to
		// notice a dead process; nothing else may drain one, so nothing else can see this
		// except through here.
		void TakeDiagnostics(DiagnosticsUpdate& update);

		// UI thread only, once a frame, and for the same reason as TakeDiagnostics: the errors and
		// warnings servers have shown since the last call, moved out.
		void TakeMessages(std::vector<ServerMessage>& messages);

		void Cancel(Ticket ticket);

		// Forwarded to the matching client and remembered here, so a restart can tell it again —
		// a fresh `LspClient` knows nothing about what was open, and re-sending is the only way
		// its answers are ever about the file the caller is looking at. A no-op when no server is
		// configured or running for this language and root.
		//
		// The text is taken by value and moved into what this remembers, not copied out of a
		// reference: the caller has just materialised the whole buffer for this call and keeps
		// nothing, so a reference here only bought a second copy of the file on the UI thread —
		// once per keystroke, since a full-text didChange goes out on every edited frame.
		void OpenDocument(const std::filesystem::path& path, LanguageId language,
						  std::string text);
		void ChangeDocument(const std::filesystem::path& path, LanguageId language,
							std::string text);

		// Matters less than the other three, and deliberately so: `ChangeDocument` already keeps
		// the server's copy in step with the buffer, so no answer depends on this. What a
		// didSave tells the server is that the file on disk now matches what it has been sent,
		// which is what lets it re-index from disk instead of from its own in-memory copy.
		// Without this the counts are still right; the server just works harder for them.
		//
		// Nothing is remembered for a restart, unlike the other three: a fresh client is told
		// what is open and what it contains, and whether a save happened before it existed says
		// nothing it can act on.
		void SaveDocument(const std::filesystem::path& path, LanguageId language);

		void CloseDocument(const std::filesystem::path& path, LanguageId language);

		// Whether the server for this document's (language, root) keeps it open - that is, whether
		// a ChangeDocument for it would be kept and forwarded rather than dropped. True from
		// OpenDocument to CloseDocument whether or not the server has a process, since a
		// restricted or binary-less server remembers what it was sent for the moment it has one.
		// Not const for the same reason Resolve is not: it consults and fills the root cache.
		bool Holds(const std::filesystem::path& path, LanguageId language);

	private:
		// Identifies one server: a language and the project root it was started for. Two files of
		// the same language under different roots get different servers, which is the whole
		// reason the root is part of the key rather than an implementation detail of the client.
		struct Key
		{
			LanguageId Language = LanguageId::PlainText;
			std::filesystem::path Root;

			friend bool operator<(const Key& lhs, const Key& rhs)
			{
				if (lhs.Language != rhs.Language)
				{
					return lhs.Language < rhs.Language;
				}

				return lhs.Root < rhs.Root;
			}
		};

		// Everything owned about one server, whether or not it ever got a process.
		struct ManagedServer
		{
			const ServerDefinition* Definition = nullptr;
			ServerOverrides Overrides;
			std::filesystem::path Root;
			ServerCommand Command; // found once; a restart reuses it rather than searching again

			// Before Client so it outlives the client, though the process the client ended still holds its files for tens of milliseconds after both are gone.
			std::optional<Core::FileHold> Hold;
			Core::Scope<LspClient> Client; // null when the binary was never found
			ServerStatus Status;
			std::size_t Restarts = 0;

			// When this server was first seen up since it last crashed or restarted; empty otherwise.
			// Restarts is forgiven once it has been set for kStableAfter.
			std::optional<std::chrono::steady_clock::time_point> UpSince;

			// Set the moment a Poll first sees this server Failed, cleared once it restarts (or
			// once it settles for good at kMaximumRestarts). Comparing against `RestartAt` rather than
			// sleeping is what keeps a frame that finds a dead server cheap.
			bool RestartPending = false;
			std::chrono::steady_clock::time_point RestartAt{};

			// The text last sent for every document this server was told is open, and the version
			// it went out under, so a restarted client can be told the same thing again and a
			// reader can tell a current answer from one about text that has moved on.
			struct SentDocument
			{
				std::string Text;
				DocumentVersion Version;
			};

			std::map<std::filesystem::path, SentDocument> OpenDocuments;

			// What ticket a still-outstanding `RequestId` of *this* client belongs to. RequestId
			// is only meaningful together with the client that issued it — this is the map that
			// makes that pairing explicit, and it is why the ticket, not the RequestId, is what a
			// caller outside this class ever holds.
			std::unordered_map<RequestId, Ticket> Pending;

			// When this server was first seen with nothing to do - no open document, no pending
			// ticket - and not the workspace's own; nullopt while it has work or is the
			// workspace's. See ReapIdle.
			std::optional<std::chrono::steady_clock::time_point> IdleSince;
		};

		// Resolves the server and the root for (path, language); null when the language has no
		// configured server or its server is turned off. Shared by every method that needs to find a server's entry rather
		// than start one — including EnsureStarted, which is called every frame a document stays
		// active, so this is on the hot path. Const, though it consults and fills m_RootCache:
		// the cache changes how fast the answer comes, never what it is.
		const ServerDefinition* Resolve(const std::filesystem::path& path, LanguageId language,
									   Key& key) const;

		const ServerDefinition* ServerFor(LanguageId language) const;

		bool FollowsSelectedSolution(LanguageId language) const;
		bool RunsServersOf(LanguageId language) const;

		const ServerChoice& ChoiceFor(LanguageId language) const;

		struct HeldDocument
		{
			std::filesystem::path Path;
			std::string Text;
		};

		// A document of a language that lists servers but has none chosen, kept so the server chosen
		// later is told about it.
		struct ParkedDocument
		{
			LanguageId Language = LanguageId::PlainText;
			std::string Text;
		};

		std::map<std::filesystem::path, ParkedDocument> m_Parked;

		std::vector<HeldDocument> TakeParkedDocumentsOf(LanguageId language);

		struct ReleasedServers
		{
			std::vector<HeldDocument> Held;
			bool ReleasedWorkspaceServer = false;
		};

		// What WarmUp was last asked, so Reconfigure can start the workspace's server again.
		struct WarmUpRequest
		{
			std::filesystem::path Root;
			bool RespectGitIgnore = false;
		};

		ReleasedServers ReleaseServersOf(LanguageId language);

		// Whether a server rooted here is the open folder's own: at it, inside it, or above it.
		bool IsWorkspaceRoot(const std::filesystem::path& root) const;
		void ForgetRootsOf(LanguageId language);

		ServerStatus OffStatus(LanguageId language) const;

		bool WarmUpNeedsOpen(LanguageId language) const;

		// The root a file with no marker above it falls back to: the fallback root when the file
		// lies inside it, the file's own directory otherwise.
		std::filesystem::path FallbackFor(const std::filesystem::path& path) const;

		// NoteNamed once `by` has been resolved; what Poll calls with the key of the server whose
		// answer it is reading. A map insert and nothing else - Poll runs every frame.
		void NoteNamedByKey(const std::filesystem::path& named, const Key& key);

		// Resolves the binary and either constructs the client or records why it could not.
		void StartServer(ManagedServer& server, const Key& key);

		// Tears down a dead client and stands up a fresh one in its place. Never revives the old
		// object: `LspClient::ReaderStopped`/`WriterStopped` are one-way, set once its threads
		// have ended, so an `LspClient` whose process died is finished for good — the only way
		// back is a brand new instance, which is what this does.
		void RestartServer(const Key& key, ManagedServer& server);

		// Shuts down, and forgets, every server outside the workspace that has held no document
		// for kIdleShutdown, along with the files it named. Called at the end of every Poll.
		void ReapIdle(std::chrono::steady_clock::time_point now);

		Clock m_Clock;

		ClientFactory m_ClientFactory;

		// The one place a client is constructed: through m_ClientFactory when one is set.
		Core::Scope<LspClient> MakeClient(const ManagedServer& server, const Key& key) const;

		bool Permits(const std::filesystem::path& root) const;
		void MarkRestricted(ManagedServer& server);
		void Restrict(const Key& key, ManagedServer& server);
		// Drops every m_NamedBy entry that points at this server.
		void ForgetNamedBy(const Key& key);
		void ReplayOpenDocuments(ManagedServer& server);

		RootPermission m_RootPermission;

		Ticket IssueTicket();

		// Records an immediately-Failed answer for a ticket that will never reach a client —
		// no server configured, or no process ever started for one that was.
		void Fail(Ticket ticket);

		// Shared by AskDefinition/AskReferences/AskSymbols: resolves (path, language) to a
		// server, and either forwards through `ask` and remembers the resulting RequestId against
		// a fresh ticket, or fails the ticket immediately when there is no live client to ask.
		Ticket AskVia(const std::filesystem::path& path, LanguageId language,
					  const std::function<RequestId(LspClient&)>& ask);

		// One representative file per language, found by a warm-up walk and waiting for the next
		// Poll to turn into a started server. Held through a shared_ptr for the same reason
		// SearchService holds its results that way: a walk still winding down after Cancel needs
		// somewhere valid to write, whether or not anything is still reading.
		struct WarmFinding
		{
			LanguageId Language = LanguageId::PlainText;
			std::filesystem::path Path;

			// Read on the walk's own thread, because it is wanted the moment the server starts
			// and reading it in Poll would be a file of unknown size opened inside a frame.
			std::string Text;
		};

		struct WarmFindings
		{
			std::mutex Mutex;
			std::vector<WarmFinding> Found;
		};

		Core::Ref<WarmFindings> m_Warm = Core::CreateRef<WarmFindings>();
		Core::CancellationToken m_WarmToken;

		std::filesystem::path m_FallbackRoot;
		std::optional<WarmUpRequest> m_LastWarmUp;
		ChoiceProvider m_ChoiceProvider;
		mutable std::map<LanguageId, ServerChoice> m_Choices;

		ServerCommandPlaces CommandPlaces() const;
		const std::vector<DotnetInstallation>& DotnetInstallations() const;

		std::filesystem::path m_ServersDirectory;
		DotnetLocator m_DotnetLocator;
		mutable std::optional<std::vector<DotnetInstallation>> m_Dotnet;
		std::filesystem::path m_LogDirectory;
		std::filesystem::path m_SelectedSolution;

		std::map<Key, ManagedServer> m_Servers;

		// A document's directory resolves to the same root every time RootFor would walk it, until
		// SetFallbackRoot clears this — a project's layout does not move mid-session, but the
		// open folder a markerless directory falls back to does. An open document is unaffected
		// by that clearing, because Resolve consults m_DocumentKeys before this. Keyed on
		// (language, parent directory) rather than the full file path, so every document in the
		// same directory shares one entry. Without this, EnsureStarted — called every frame a
		// document is active, per its own contract — would call RootFor, and so
		// std::filesystem::exists, once per marker per directory level, sixty times a second, for
		// a server that is already running and needs nothing. If a root ever did move during a
		// session, the stale entry would point a server at where the project used to live; that
		// is wrong but not silently so — restarting the application re-walks from scratch — and
		// is a better trade than stat-ing the filesystem on every frame forever.
		mutable std::map<std::pair<LanguageId, std::filesystem::path>, std::filesystem::path> m_RootCache;

		// The key each open document was opened under. A document belongs to one server for as
		// long as it is open (see the server-roots spec): every per-document call goes through
		// Resolve, and Resolve answers from here first, so a later change to how a path would
		// resolve - a new fallback root, a newly named file - never moves an open document to a
		// server that was not told about it. Filled by OpenDocument, emptied by CloseDocument.
		std::map<std::filesystem::path, Key> m_DocumentKeys;

		// Files a server has named in an answer - a definition, a reference - with that server's
		// key, last answer winning. A file with no project of its own that is reached this way
		// belongs to the server that pointed at it: a system header opened by F12 is served by
		// the project that includes it, with that project's flags and that project's trust.
		std::map<std::filesystem::path, Key> m_NamedBy;

		std::uint64_t m_NextTicket = 1;
		std::vector<Answer> m_Answers;
		DiagnosticsUpdate m_Diagnostics;
		std::vector<ServerMessage> m_Messages;

		// Last, deliberately: members are destroyed in reverse order of declaration, so this one
		// joins its worker before anything the worker might still be touching goes away.
		Core::JobQueue m_Jobs;
	};
}
