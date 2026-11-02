// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/features/bulk_download/bulk_download_logic.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"

class ChannelData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace AyuBulkDownload {

struct SearchResult {
	std::vector<FullMsgId> ids;
	// No ids, but matched posts had videos that can't be saved.
	bool restricted = false;
};

// Finds videos of the posts that match the query using server-side
// messages.search, then loads the full albums of matched posts.
// Channels and supergroups only: album neighbors are found by adjacent
// message ids, which are per-chat only there.
// Destroying the object cancels all pending requests.
class VideoSearch final {
public:
	VideoSearch(
		not_null<ChannelData*> channel,
		Query query,
		Fn<void(SearchResult)> done,
		Fn<void()> fail);

	void start();

private:
	void requestPage();
	void pageReceived(const MTPmessages_Messages &result);
	void requestNextChunk();
	void finish();

	[[nodiscard]] HistoryItem *lookup(int64 id) const;
	[[nodiscard]] std::vector<std::vector<int64>> albums() const;

	const not_null<ChannelData*> _channel;
	const Query _query;
	const Fn<void(SearchResult)> _done;
	const Fn<void()> _fail;
	MTP::Sender _api;

	MsgId _offsetId = 0;
	std::vector<MatchedPost> _posts;
	std::vector<std::vector<int64>> _chunks;
	int _chunkIndex = 0;

};

// Saves the videos to the default download folder and tracks them in the
// Downloads manager, like DocumentSaveClickHandler::SaveAndTrack, but
// without a dialog even with "Ask download path" enabled.
void StartDownloads(
	not_null<Main::Session*> session,
	const std::vector<FullMsgId> &ids);

} // namespace AyuBulkDownload
