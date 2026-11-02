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
#include "data/data_file_click_handler.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <map>

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
	return document->loading()
		|| !document->filepath(true).isEmpty()
		|| document->forbidsFileSave();
}

// Same target as FileNameUnsafe() picks when it does not ask:
// the default download folder, the document name (or a timestamped
// "video_..." name) and a " (N)" suffix when the file already exists.
[[nodiscard]] QString DefaultDownloadFilePath(
		not_null<DocumentData*> document) {
	const auto session = &document->session();
	const auto setting = Core::App().settings().downloadPath();
	const auto folder = setting.isEmpty()
		? File::DefaultDownloadPath(session)
		: (setting == FileDialog::Tmp())
		? session->local().tempDirectory()
		: setting;
	if (folder.isEmpty()) {
		return QString();
	}
	if (!QDir().exists(folder)) {
		QDir().mkpath(folder);
	}
	const auto name = base::FileNameFromUserString(document->filename());
	auto result = QString();
	if (name.isEmpty() || name.at(0) == QChar::fromLatin1('.')) {
		const auto patterns = Core::MimeTypeForName(
			document->mimeString()).globPatterns();
		auto extension = name.isEmpty()
			? (patterns.isEmpty()
				? u".mov"_q
				: QString(patterns.front()).replace('*', QString()))
			: name;
		result = filedialogDefaultName(u"video"_q, extension, folder);
	} else {
		const auto dot = name.lastIndexOf('.');
		const auto start = (dot >= 0) ? name.mid(0, dot) : name;
		const auto extension = (dot >= 0) ? name.mid(dot) : QString();
		const auto base = folder + start;
		result = base + extension;
		for (auto i = 0; QFileInfo::exists(result); ++i) {
			result = base + u" (%1)"_q.arg(i + 2) + extension;
		}
	}
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

// DocumentSaveClickHandler::SaveAndTrack(), but never asks for a path:
// with "Ask download path" on, it would open a dialog for every file.
void SaveAndTrackToDefaultFolder(
		FullMsgId itemId,
		not_null<DocumentData*> document) {
	if (!Core::App().settings().askDownloadPath()) {
		DocumentSaveClickHandler::SaveAndTrack(itemId, document);
		return;
	} else if (document->isNull()) {
		return;
	}
	const auto path = DefaultDownloadFilePath(document);
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
	not_null<PeerData*> peer,
	Query query,
	Fn<void(std::vector<FullMsgId>)> done,
	Fn<void(QString)> fail)
: _peer(peer)
, _query(std::move(query))
, _done(std::move(done))
, _fail(std::move(fail))
, _api(&peer->session().mtp()) {
}

void VideoSearch::start() {
	requestPage();
}

void VideoSearch::requestPage() {
	// Flood waits are delayed and retried by the MTP layer, as usual.
	_api.request(MTPmessages_Search(
		MTP_flags(0),
		_peer->input(),
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
	}).fail([=](const MTP::Error &error) {
		_fail(error.type());
	}).send();
}

void VideoSearch::pageReceived(const MTPmessages_Messages &result) {
	auto &owner = _peer->owner();
	owner.processExistingMessages(_peer->asChannel(), result);

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
			const auto item = owner.message(_peer, id);
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
	const auto channel = _peer->asChannel();
	const auto done = [=](const MTPmessages_Messages &result) {
		_peer->owner().processExistingMessages(channel, result);
		requestNextChunk();
	};
	// A failed chunk only means fewer album items, keep going.
	const auto fail = [=] {
		requestNextChunk();
	};
	if (channel) {
		_api.request(MTPchannels_GetMessages(
			channel->inputChannel(),
			MTP_vector<MTPInputMessage>(ids)
		)).done(done).fail(fail).send();
	} else {
		_api.request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(ids)
		)).done(done).fail(fail).send();
	}
}

void VideoSearch::finish() {
	auto candidates = std::vector<Candidate>();
	const auto add = [&](not_null<HistoryItem*> item) {
		const auto video = VideoOf(item);
		candidates.push_back({
			.msgId = item->id.bare,
			.groupId = item->groupId().value,
			.documentId = video ? uint64(video->id) : uint64(0),
			.isVideo = (video != nullptr),
			.skip = video && AlreadyHave(video),
		});
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
	auto result = std::vector<FullMsgId>();
	for (const auto id : SelectVideos(_posts, candidates)) {
		result.push_back(FullMsgId(_peer->id, MsgId(id)));
	}
	_done(std::move(result));
}

HistoryItem *VideoSearch::lookup(int64 id) const {
	return _peer->owner().message(_peer, MsgId(id));
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
	for (const auto &id : ids) {
		const auto item = session->data().message(id);
		const auto video = item ? VideoOf(item) : nullptr;
		if (!video || AlreadyHave(video)) {
			continue;
		}
		SaveAndTrackToDefaultFolder(id, video);
	}
}

} // namespace AyuBulkDownload
