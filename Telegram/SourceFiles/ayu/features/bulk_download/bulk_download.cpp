// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/bulk_download/bulk_download.h"

#include "base/base_file_utilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_download_manager.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <map>
#include <set>

namespace AyuBulkDownload {
namespace {

using MessagesList = QVector<MTPMessage>;

[[nodiscard]] const MessagesList *MessagesFrom(
		const MTPmessages_Messages &result) {
	return result.match([](const MTPDmessages_messagesNotModified &) {
		return static_cast<const MessagesList*>(nullptr);
	}, [](const auto &data) {
		return &data.vmessages().v;
	});
}

[[nodiscard]] DocumentData *VideoOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document && document->isVideoFile()) ? document : nullptr;
}

[[nodiscard]] bool AlreadyHave(not_null<DocumentData*> document) {
	return document->loading() || !document->filepath(true).isEmpty();
}

// Paths picked during one StartDownloads() batch. A file may be created
// only when its download finishes, so `exists()` alone can't tell that
// a name is taken. `next` keeps the " (N)" search from restarting at 0.
struct Reservation {
	std::set<QString> taken;
	std::map<QString, int> next;
};

// Same target as FileNameUnsafe() picks when it does not ask:
// the default download folder, the document name (or a timestamped
// "video_..." name) and a " (N)" suffix when the name is taken.
[[nodiscard]] QString DefaultDownloadFilePath(
		not_null<DocumentData*> document,
		Reservation &reservation) {
	const auto session = &document->session();
	const auto setting = Core::App().settings().downloadPath();
	auto folder = setting.isEmpty()
		? File::DefaultDownloadPath(session)
		: (setting == FileDialog::Tmp())
		? session->local().tempDirectory()
		: setting;
	if (folder.isEmpty()) {
		return QString();
	} else if (!folder.endsWith('/')) {
		folder += '/';
	}
	if (!QDir().exists(folder)) {
		QDir().mkpath(folder);
	}
	auto name = base::FileNameFromUserString(document->filename());
	if (name.isEmpty() || name.at(0) == QChar::fromLatin1('.')) {
		const auto patterns = Core::MimeTypeForName(
			document->mimeString()).globPatterns();
		const auto extension = !name.isEmpty()
			? name
			: patterns.isEmpty()
			? u".mov"_q
			: QString(patterns.front()).replace('*', QString());
		// skipExistance: only the timestamped file name, checked below.
		name = filedialogDefaultName(u"video"_q, extension, folder, true);
	}
	const auto dot = name.lastIndexOf('.');
	const auto base = folder + ((dot >= 0) ? name.mid(0, dot) : name);
	const auto extension = (dot >= 0) ? name.mid(dot) : QString();
	const auto make = [&](int index) {
		return index
			? (base + u" (%1)"_q.arg(index + 1) + extension)
			: (base + extension);
	};
	auto index = reservation.next[base + extension];
	auto result = make(index);
	while (reservation.taken.contains(result) || QFileInfo::exists(result)) {
		result = make(++index);
	}
	reservation.next[base + extension] = index + 1;
	reservation.taken.insert(result);
#ifdef Q_OS_WIN
	// Same masking as FileNameForSave().
	const auto lower = result.trimmed().toLower();
	for (const auto &bad : { u".lnk"_q, u".scf"_q }) {
		if (lower.endsWith(bad)) {
			return result + u".download"_q;
		}
	}
#endif // Q_OS_WIN
	return result;
}

// What DocumentSaveClickHandler::SaveAndTrack() does, with an explicit
// path: it never asks for a path (with "Ask download path" on it would
// open a dialog per file) and never takes Save()'s cache-only branch for
// saveToCache() documents, so every started download goes to a file and
// is tracked in the Downloads manager.
void SaveToDefaultFolderAndTrack(
		FullMsgId itemId,
		not_null<DocumentData*> document,
		Reservation &reservation) {
	if (document->isNull() || document->forbidsFileSave()) {
		return;
	}
	const auto path = DefaultDownloadFilePath(document, reservation);
	if (path.isEmpty()) {
		return;
	}
	document->save(Data::FileOrigin(itemId), path);
	if (document->loading() && !document->loadingFilePath().isEmpty()) {
		if (const auto item = document->owner().message(itemId)) {
			Core::App().downloadManager().addLoading({
				.item = item,
				.document = document,
			});
		}
	}
}

} // namespace

VideoSearch::VideoSearch(
	not_null<ChannelData*> channel,
	Query query,
	Fn<void(SearchResult)> done,
	Fn<void()> fail)
: _channel(channel)
, _query(std::move(query))
, _done(std::move(done))
, _fail(std::move(fail))
, _api(&channel->session().mtp()) {
}

void VideoSearch::start() {
	requestPage();
}

void VideoSearch::requestPage() {
	// Flood waits are delayed and retried by the MTP layer, as usual.
	_api.request(MTPmessages_Search(
		MTP_flags(0),
		_channel->input(),
		MTP_string(SearchText(_query)),
		MTP_inputPeerEmpty(), // from_id
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTP_int(0), // top_msg_id
		MTP_inputMessagesFilterPhotoVideo(),
		MTP_int(ServerMinDate(_query)),
		MTP_int(ServerMaxDate(_query)),
		MTP_int(_offsetId.bare),
		MTP_int(0), // add_offset
		MTP_int(kSearchPerPage),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		pageReceived(result);
	}).fail([=] {
		_fail();
	}).send();
}

void VideoSearch::pageReceived(const MTPmessages_Messages &result) {
	auto &owner = _channel->owner();
	owner.processExistingMessages(_channel, result);

	const auto list = MessagesFrom(result);
	auto minId = MsgId();
	auto received = 0;
	if (list) {
		for (const auto &message : *list) {
			const auto id = IdFromMessage(message);
			if (!id) {
				continue;
			}
			++received;
			if (!minId || id < minId) {
				minId = id;
			}
			const auto item = owner.message(_channel, id);
			if (!item
				|| !Matches(_query, item->originalText().text, item->date())) {
				continue;
			}
			_posts.push_back({
				.msgId = id.bare,
				.groupId = item->groupId().value,
			});
		}
	}
	const auto complete = !list
		|| (result.type() == mtpc_messages_messages)
		|| !received
		|| (_offsetId && minId >= _offsetId);
	if (!complete) {
		_offsetId = minId;
		requestPage();
		return;
	}

	_chunks = ChunkIds(AlbumNeighborIds(albums(), [&](int64 id) {
		return lookup(id) != nullptr;
	}));
	_chunkIndex = 0;
	requestNextChunk();
}

void VideoSearch::requestNextChunk() {
	if (_chunkIndex >= int(_chunks.size())) {
		finish();
		return;
	}
	auto ids = QVector<MTPInputMessage>();
	for (const auto id : _chunks[_chunkIndex++]) {
		ids.push_back(MTP_inputMessageID(MTP_int(id)));
	}
	_api.request(MTPchannels_GetMessages(
		_channel->inputChannel(),
		MTP_vector<MTPInputMessage>(ids)
	)).done([=](const MTPmessages_Messages &result) {
		_channel->owner().processExistingMessages(_channel, result);
		requestNextChunk();
	}).fail([=] {
		// A failed chunk only means fewer album items, keep going.
		requestNextChunk();
	}).send();
}

void VideoSearch::finish() {
	// `allowed` also skips videos that can't be saved, `any` does not,
	// to tell "nothing found" from "found, but saving is restricted".
	auto allowed = std::vector<Candidate>();
	auto any = std::vector<Candidate>();
	const auto add = [&](not_null<HistoryItem*> item) {
		const auto video = VideoOf(item);
		const auto candidate = Candidate{
			.msgId = item->id.bare,
			.groupId = item->groupId().value,
			.documentId = video ? uint64(video->id) : uint64(0),
			.isVideo = (video != nullptr),
			.skip = video && AlreadyHave(video),
		};
		any.push_back(candidate);
		allowed.push_back(candidate);
		if (video && video->forbidsFileSave()) {
			allowed.back().skip = true;
		}
	};
	for (const auto &post : _posts) {
		if (!post.groupId) {
			if (const auto item = lookup(post.msgId)) {
				add(item);
			}
		}
	}
	for (const auto &members : albums()) {
		const auto range = AlbumIdRange(members);
		for (auto id = range.from; id <= range.till; ++id) {
			if (const auto item = lookup(id)) {
				add(item);
			}
		}
	}
	auto result = SearchResult();
	for (const auto id : SelectVideos(_posts, allowed)) {
		result.ids.push_back(FullMsgId(_channel->id, MsgId(id)));
	}
	result.restricted = result.ids.empty()
		&& !SelectVideos(_posts, any).empty();
	_done(std::move(result));
}

HistoryItem *VideoSearch::lookup(int64 id) const {
	return _channel->owner().message(_channel, MsgId(id));
}

std::vector<std::vector<int64>> VideoSearch::albums() const {
	auto byGroup = std::map<uint64, std::vector<int64>>();
	for (const auto &post : _posts) {
		if (post.groupId) {
			byGroup[post.groupId].push_back(post.msgId);
		}
	}
	auto result = std::vector<std::vector<int64>>();
	result.reserve(byGroup.size());
	for (auto &[groupId, members] : byGroup) {
		result.push_back(std::move(members));
	}
	return result;
}

void StartDownloads(
		not_null<Main::Session*> session,
		const std::vector<FullMsgId> &ids) {
	auto reservation = Reservation();
	for (const auto &id : ids) {
		const auto item = session->data().message(id);
		const auto video = item ? VideoOf(item) : nullptr;
		if (!video || video->loading()) {
			continue;
		}
		SaveToDefaultFolderAndTrack(id, video, reservation);
	}
}

} // namespace AyuBulkDownload
