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

class PeerData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace AyuBulkDownload {

// Finds videos of the posts that match the query using server-side
// messages.search, then loads the full albums of matched posts.
// Destroying the object cancels all pending requests.
class VideoSearch final {
public:
	VideoSearch(
		not_null<PeerData*> peer,
		Query query,
		Fn<void(std::vector<FullMsgId>)> done,
		Fn<void(QString)> fail);

	void start();

private:
	void requestPage();
	void pageReceived(const MTPmessages_Messages &result);
	void requestNextChunk();
	void finish();

	[[nodiscard]] HistoryItem *lookup(int64 id) const;
	[[nodiscard]] std::vector<std::vector<int64>> albums() const;

	const not_null<PeerData*> _peer;
	const Query _query;
	const Fn<void(std::vector<FullMsgId>)> _done;
	const Fn<void(QString)> _fail;
	MTP::Sender _api;

	MsgId _offsetId = 0;
	std::vector<MatchedPost> _posts;
	std::vector<std::vector<int64>> _chunks;
	int _chunkIndex = 0;

};

// Queues the videos into the Downloads manager through the regular
// tracked save path (DocumentSaveClickHandler::SaveAndTrack), always into
// the default download folder, even with "Ask download path" enabled.
void StartDownloads(
	not_null<Main::Session*> session,
	const std::vector<FullMsgId> &ids);

} // namespace AyuBulkDownload
