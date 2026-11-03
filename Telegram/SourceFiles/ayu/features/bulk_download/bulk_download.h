// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/features/bulk_download/bulk_download_logic.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"

#include <deque>
#include <optional>

class ChannelData;
class HistoryItem;
class QPainter;

namespace Main {
class Session;
} // namespace Main

namespace Dialogs {
struct SearchState;
} // namespace Dialogs

namespace style {
struct DialogRow;
} // namespace style

namespace Ui {
class RoundCheckbox;
} // namespace Ui

namespace AyuBulkDownload {

// Where a batch goes by default: the download path setting, resolved
// like upstream (default Downloads/Telegram folder or Temp folder).
[[nodiscard]] QString DefaultDownloadFolder(
	not_null<Main::Session*> session);

// The channel of an in-chat search whose found posts can be selected,
// or nullptr. Channels and supergroups only: album neighbors are found by
// adjacent message ids, which are per-chat only there.
[[nodiscard]] ChannelData *SelectableSearchChannel(
	const Dialogs::SearchState &state);

// Pages through messages.search (photos and videos) for a query.
class SearchPager final {
public:
	struct Handlers {
		Fn<void(std::vector<MatchedPost>)> page;
		Fn<void(bool failed)> done;
	};

	SearchPager(not_null<ChannelData*> channel, QString query);

	// The handlers may be replaced while a page is loading.
	void start(Handlers handlers);
	void setHandlers(Handlers handlers);

	[[nodiscard]] bool loading() const;
	[[nodiscard]] SearchProgress progress() const;

private:
	void requestPage();
	void pageReceived(const MTPmessages_Messages &result);

	const not_null<ChannelData*> _channel;
	const QString _query;
	MTP::Sender _api;
	Handlers _handlers;
	MsgId _offsetId = 0;
	SearchProgress _progress;
	bool _loading = false;

};

// Adds the media of the posts to the Downloads one post at a time, as soon
// as each album is loaded, with one name reservation for the whole batch.
// Lives in the account (Main::Session): it survives the search and is
// destroyed when it finishes, when it is cancelled, or with the session.
class DownloadBatch final : public base::has_weak_ptr {
public:
	struct State {
		int added = 0; // Videos sent to the Downloads.
		float64 progress = 0.;

		friend inline bool operator==(const State &, const State &)
			= default;
	};

	DownloadBatch(
		not_null<ChannelData*> channel,
		PostSelection selection,
		std::unique_ptr<SearchPager> pager,
		bool includePhotos,
		QString folder,
		not_null<NameReservation*> reservation);
	~DownloadBatch();

	// `pager` keeps feeding the batch while it is loading.
	static not_null<DownloadBatch*> Start(
		not_null<ChannelData*> channel,
		PostSelection selection,
		std::unique_ptr<SearchPager> pager,
		bool includePhotos,
		const QString &folder);

	// Stops adding, what is added stays in the Downloads.
	void cancel();

	[[nodiscard]] const QString &folder() const;

	[[nodiscard]] rpl::producer<State> stateValue() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void queue(MatchedPost post);
	void processNext();
	// Returns whether the known items of the post have a video.
	bool addPost(MatchedPost post);
	void checkFinished();
	void finish();
	void pushState();

	[[nodiscard]] HistoryItem *lookup(int64 id) const;

	const not_null<ChannelData*> _channel;
	const bool _includePhotos = false;
	const QString _folder;
	MTP::Sender _api;
	PostSelection _selection;
	std::unique_ptr<SearchPager> _pager;
	const not_null<NameReservation*> _reservation; // Shared per folder.

	std::deque<MatchedPost> _pending;
	std::vector<MatchedPost> _requested;
	std::set<int64> _queued;
	std::set<int64> _fetched; // Album neighbors were requested.
	std::set<int64> _fetchFailed;
	std::set<uint64> _doneGroups;
	mtpRequestId _requestId = 0;
	bool _scheduled = false;
	int _processed = 0;
	int _videos = 0;
	int _photos = 0;
	int _failedPosts = 0;
	bool _pageFailed = false;
	bool _finished = false;

	rpl::variable<State> _state;
	rpl::lifetime _lifetime;

};

// Posts picked in the in-chat search results of a channel. "Select all"
// pages through messages.search for the same query in the background.
class SearchSelection final : public base::has_weak_ptr {
public:
	struct State {
		int count = 0;
		bool loading = false; // "Select all" is paging.
		bool adding = false; // A batch adds the selected posts.
		int added = 0;
		float64 progress = 0.;

		friend inline bool operator==(const State &, const State &)
			= default;
	};

	SearchSelection(
		not_null<ChannelData*> channel,
		QString query,
		Fn<std::vector<not_null<HistoryItem*>>()> shownResults,
		Fn<void()> repaint);
	~SearchSelection();

	// Returns false for messages of other chats and while adding: those
	// rows are never selected. While closing, nothing changes.
	bool toggle(not_null<const HistoryItem*> item);
	void selectAll();
	// Hands the posts (and a running "Select all") to a new batch.
	void startDownload(bool includePhotos, const QString &folder);
	// The owner destroys it after closeRequests(), a running batch is
	// cancelled.
	void close();

	// The number of selected posts, unless "Select all" may add more.
	[[nodiscard]] std::optional<int> finalCount() const;

	void paintCheck(
		QPainter &p,
		not_null<const HistoryItem*> item,
		const style::DialogRow &st,
		int outerWidth);

	[[nodiscard]] rpl::producer<State> stateValue() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void pushState();

	const not_null<ChannelData*> _channel;
	const QString _query;
	const Fn<std::vector<not_null<HistoryItem*>>()> _shownResults;
	const Fn<void()> _repaint;

	PostSelection _selection;
	base::flat_map<MsgId, std::unique_ptr<Ui::RoundCheckbox>> _checks;

	std::unique_ptr<SearchPager> _pager;
	std::vector<MatchedPost> _found;
	bool _foundAll = false;
	bool _closing = false;

	base::weak_ptr<DownloadBatch> _batch;
	DownloadBatch::State _batchState;
	bool _adding = false;

	rpl::variable<State> _state;
	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;

};

} // namespace AyuBulkDownload
