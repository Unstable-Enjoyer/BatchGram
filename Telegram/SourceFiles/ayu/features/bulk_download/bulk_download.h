// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/features/bulk_download/bulk_download_logic.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"

class ChannelData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace AyuBulkDownload {

struct PhotoItem {
	FullMsgId photo;
	FullMsgId video; // Its file name is reused for the photo.
};

// What a finished search found, shown in the confirm box.
struct SearchOutcome {
	ChannelData *channel = nullptr;
	std::vector<FullMsgId> videos;
	std::vector<PhotoItem> photos;
	int64 size = 0; // Videos plus photos with a known byte size.
	bool partial = false; // A search page failed after some results.
	bool failed = false; // The first search page failed.
};

// Where a batch goes by default: the download path setting, resolved
// like upstream (default Downloads/Telegram folder or Temp folder).
[[nodiscard]] QString DefaultDownloadFolder(
	not_null<Main::Session*> session);

// Saves the found media to `folder` (the default one when empty), with a
// per-batch name reservation and no file dialog, even with "Ask download
// path" enabled. Videos are tracked in the Downloads manager while they
// load; photos are added there when saved.
void StartDownloads(
	not_null<Main::Session*> session,
	const SearchOutcome &outcome,
	const QString &folder);

// Finds videos (and optionally photos) of the posts that match the query
// using server-side messages.search, then loads the full albums of matched
// posts. Channels and supergroups only: album neighbors are found by
// adjacent message ids, which are per-chat only there.
//
// One search per account. It is owned by the account (Main::Session), not
// by any box: it survives closing the dialog, and it is destroyed when it
// finishes, when it is stopped, or when the session is destroyed.
// Destroying it cancels all pending requests and ends lifetime().
class VideoSearch final : public base::has_weak_ptr {
public:
	VideoSearch(
		not_null<ChannelData*> channel,
		Query query,
		bool includePhotos);
	~VideoSearch();

	// The running search of the account, if any.
	[[nodiscard]] static VideoSearch *Find(not_null<Main::Session*> session);

	// Starts a search unless one is already running for the account,
	// returns the running one.
	static not_null<VideoSearch*> Start(
		not_null<ChannelData*> channel,
		Query query,
		bool includePhotos);

	// Cancels the search and destroys it. Nothing is shown afterwards.
	void stop();

	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] rpl::producer<SearchProgress> progressValue() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void requestPage();
	void pageReceived(const MTPmessages_Messages &result);
	void pageFailed();
	void requestAlbums();
	void requestNextChunk();
	void updateProgress();
	void finish();

	[[nodiscard]] HistoryItem *lookup(int64 id) const;
	[[nodiscard]] std::vector<std::vector<int64>> albums() const;
	// checkFiles: also skip already downloaded videos (stats files).
	[[nodiscard]] MediaSelection select(bool checkFiles) const;

	const not_null<ChannelData*> _channel;
	const Query _query;
	const bool _includePhotos = false;
	MTP::Sender _api;

	MsgId _offsetId = 0;
	std::vector<MatchedPost> _posts;
	std::vector<std::vector<int64>> _chunks;
	int _chunkIndex = 0;
	bool _failed = false;
	bool _finished = false;

	rpl::variable<SearchProgress> _progress;
	rpl::lifetime _lifetime;

};

} // namespace AyuBulkDownload
