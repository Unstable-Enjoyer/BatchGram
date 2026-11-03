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

class ChannelData;
class HistoryItem;

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

struct PhotoItem {
	FullMsgId photo;
	FullMsgId video; // Its file name is reused for the photo.
};

// The media of the selected posts, shown in the download confirm.
struct DownloadSet {
	ChannelData *channel = nullptr;
	std::vector<FullMsgId> videos;
	std::vector<PhotoItem> photos; // Photos of the posts with a video.
	int64 videosSize = 0;
	int64 photosSize = 0; // Photos with a known byte size.
	bool partial = false; // "Select all" stopped before the end.
};

// Where a batch goes by default: the download path setting, resolved
// like upstream (default Downloads/Telegram folder or Temp folder).
[[nodiscard]] QString DefaultDownloadFolder(
	not_null<Main::Session*> session);

// Saves the videos (and the photos, with includePhotos) to `folder` (the
// default one when empty), with a per-batch name reservation and no file
// dialog, even with "Ask download path" enabled. Videos are tracked in the
// Downloads manager while they load; photos are added there when saved.
void StartDownloads(
	not_null<Main::Session*> session,
	const DownloadSet &set,
	bool includePhotos,
	const QString &folder);

// The channel of an in-chat search whose found posts can be selected,
// or nullptr. Channels and supergroups only: album neighbors are found by
// adjacent message ids, which are per-chat only there.
[[nodiscard]] ChannelData *SelectableSearchChannel(
	const Dialogs::SearchState &state);

// Posts picked in the in-chat search results of a channel. "Select all"
// pages through messages.search for the same query in the background.
class SearchSelection final : public base::has_weak_ptr {
public:
	struct State {
		int count = 0;
		bool loading = false; // "Select all" is paging.
		bool preparing = false; // Albums are loading for the download.
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

	[[nodiscard]] bool contains(not_null<const HistoryItem*> item) const;
	// Returns false for messages of other chats, they are never selected.
	bool toggle(not_null<const HistoryItem*> item);
	void selectAll();
	// Stops "Select all" (keeping what it found) or the download preparing.
	void stop();
	void prepareDownload(Fn<void(DownloadSet)> ready);
	void close();

	void paintCheck(
		QPainter &p,
		not_null<const HistoryItem*> item,
		const style::DialogRow &st,
		int outerWidth);

	[[nodiscard]] rpl::producer<State> stateValue() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void requestPage();
	void pageReceived(const MTPmessages_Messages &result);
	void pageFailed();
	void requestNextChunk();
	void finishPreparing();
	void pushState();

	[[nodiscard]] HistoryItem *lookup(int64 id) const;
	[[nodiscard]] DownloadSet collect() const;

	const not_null<ChannelData*> _channel;
	const QString _query;
	const Fn<std::vector<not_null<HistoryItem*>>()> _shownResults;
	const Fn<void()> _repaint;
	MTP::Sender _api;

	PostSelection _selection;
	base::flat_map<MsgId, std::unique_ptr<Ui::RoundCheckbox>> _checks;

	mtpRequestId _requestId = 0;
	MsgId _offsetId = 0;
	SearchProgress _progress;
	std::vector<MatchedPost> _found;
	bool _foundAll = false;
	bool _loading = false;
	bool _partial = false;

	std::vector<std::vector<int64>> _chunks;
	int _chunkIndex = 0;
	Fn<void(DownloadSet)> _ready;

	rpl::variable<State> _state;
	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;

};

} // namespace AyuBulkDownload
