// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/bulk_download/bulk_download.h"

#include "ayu/ui/boxes/download_videos_box.h"
#include "base/base_file_utilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_download_manager.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <map>

namespace AyuBulkDownload {
namespace {

using MessagesList = QVector<MTPMessage>;

// Photos load into memory first (as in upstream "Save selected files"),
// then are written to their reserved path and added to the Downloads.
class PhotoSaver final {
public:
	explicit PhotoSaver(not_null<Main::Session*> session);

	void add(
		FullMsgId itemId,
		not_null<PhotoData*> photo,
		const QString &path);

private:
	struct Pending {
		FullMsgId itemId;
		not_null<PhotoData*> photo;
		std::shared_ptr<Data::PhotoMedia> view;
		QString path;
	};

	void check();
	[[nodiscard]] bool done(const Pending &pending) const;

	const not_null<Main::Session*> _session;
	std::vector<Pending> _pending;
	rpl::lifetime _subscription;

};

struct AccountData {
	std::unique_ptr<VideoSearch> search;
	std::unique_ptr<PhotoSaver> photos;
};

[[nodiscard]] auto Accounts()
-> std::map<not_null<Main::Session*>, AccountData> & {
	static auto result = std::map<not_null<Main::Session*>, AccountData>();
	return result;
}

[[nodiscard]] AccountData &Account(not_null<Main::Session*> session) {
	auto &accounts = Accounts();
	const auto i = accounts.find(session);
	if (i != accounts.end()) {
		return i->second;
	}
	// Logout or session destruction cancels the search and photo saves.
	session->lifetime().add([=] {
		auto taken = base::take(Accounts()[session]);
		Accounts().erase(session);
	});
	return accounts[session];
}

void DestroySearch(not_null<Main::Session*> session) {
	auto &accounts = Accounts();
	const auto i = accounts.find(session);
	if (i != accounts.end()) {
		// Take it out first, so Find() is empty during destruction.
		auto taken = base::take(i->second.search);
	}
}

[[nodiscard]] const MessagesList *MessagesFrom(
		const MTPmessages_Messages &result) {
	return result.match([](const MTPDmessages_messagesNotModified &) {
		return static_cast<const MessagesList*>(nullptr);
	}, [](const auto &data) {
		return &data.vmessages().v;
	});
}

// The `count` field of the result, or the list size when it has none.
[[nodiscard]] int TotalFrom(const MTPmessages_Messages &result) {
	return result.match([](const MTPDmessages_messagesNotModified &) {
		return 0;
	}, [](const MTPDmessages_messages &data) {
		return int(data.vmessages().v.size());
	}, [](const MTPDmessages_messagesSlice &data) {
		return data.vcount().v;
	}, [](const MTPDmessages_channelMessages &data) {
		return data.vcount().v;
	});
}

[[nodiscard]] DocumentData *VideoOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document && document->isVideoFile()) ? document : nullptr;
}

[[nodiscard]] PhotoData *PhotoOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return media ? media->photo() : nullptr;
}

// Unsavable videos (forbidsFileSave) are skipped silently.
[[nodiscard]] bool SkipVideo(not_null<DocumentData*> document) {
	return document->loading()
		|| !document->filepath(true).isEmpty()
		|| document->forbidsFileSave();
}

[[nodiscard]] bool FileExists(const QString &path) {
	return QFileInfo::exists(path);
}

// Same name as FileNameUnsafe() picks when it does not ask: the document
// name (or a timestamped "video_..." name), reserved for this batch.
[[nodiscard]] QString VideoFilePath(
		not_null<DocumentData*> document,
		const QString &folder,
		NameReservation &reservation) {
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
	const auto parts = SplitPath(folder + name);
	auto result = ReserveName(
		reservation,
		parts.base,
		parts.extension,
		FileExists);
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
// path: it never asks for a path and never takes Save()'s cache-only
// branch for saveToCache() documents. Returns the path used, or empty.
QString SaveVideoAndTrack(
		FullMsgId itemId,
		not_null<DocumentData*> document,
		const QString &folder,
		NameReservation &reservation) {
	if (document->isNull() || document->forbidsFileSave()) {
		return QString();
	}
	const auto path = VideoFilePath(document, folder, reservation);
	if (path.isEmpty()) {
		return QString();
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
	return path;
}

PhotoSaver::PhotoSaver(not_null<Main::Session*> session)
: _session(session) {
}

void PhotoSaver::add(
		FullMsgId itemId,
		not_null<PhotoData*> photo,
		const QString &path) {
	auto view = photo->createMediaView();
	view->wanted(Data::PhotoSize::Large, Data::FileOrigin(itemId));
	_pending.push_back({
		.itemId = itemId,
		.photo = photo,
		.view = std::move(view),
		.path = path,
	});
	if (!_subscription) {
		_session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			check();
		}, _subscription);
	}
	check();
}

bool PhotoSaver::done(const Pending &pending) const {
	if (!pending.view->loaded()) {
		// Still loading, or failed to load: drop it when it failed.
		return pending.photo->failed(Data::PhotoSize::Large);
	} else if (!pending.view->saveToFile(pending.path)) {
		return true;
	}
	if (const auto item = _session->data().message(pending.itemId)) {
		auto &manager = Core::App().downloadManager();
		manager.addLoaded({
			.item = item,
			.photo = pending.photo,
		}, pending.path, manager.computeNextStartDate());
	}
	return true;
}

void PhotoSaver::check() {
	_pending.erase(
		ranges::remove_if(_pending, [&](const Pending &pending) {
			return done(pending);
		}),
		end(_pending));
	if (_pending.empty()) {
		_subscription.destroy();
	}
}

} // namespace

QString DefaultDownloadFolder(not_null<Main::Session*> session) {
	return ResolveDownloadFolder(
		QString(),
		Core::App().settings().downloadPath(),
		FileDialog::Tmp(),
		File::DefaultDownloadPath(session),
		session->local().tempDirectory());
}

void StartDownloads(
		not_null<Main::Session*> session,
		const SearchOutcome &outcome,
		const QString &folder) {
	const auto target = ResolveDownloadFolder(
		folder,
		Core::App().settings().downloadPath(),
		FileDialog::Tmp(),
		File::DefaultDownloadPath(session),
		session->local().tempDirectory());
	if (target.isEmpty()) {
		return;
	}
	if (!QDir().exists(target)) {
		QDir().mkpath(target);
	}
	auto reservation = NameReservation();
	auto videoPaths = std::map<FullMsgId, QString>();
	for (const auto &id : outcome.videos) {
		const auto item = session->data().message(id);
		const auto video = item ? VideoOf(item) : nullptr;
		if (!video || video->loading()) {
			continue;
		}
		const auto path = SaveVideoAndTrack(id, video, target, reservation);
		if (!path.isEmpty()) {
			videoPaths.emplace(id, path);
		}
	}
	if (outcome.photos.empty()) {
		return;
	}
	auto &saver = Account(session).photos;
	if (!saver) {
		saver = std::make_unique<PhotoSaver>(session);
	}
	for (const auto &[photoId, videoId] : outcome.photos) {
		const auto i = videoPaths.find(videoId);
		const auto item = session->data().message(photoId);
		const auto photo = item ? PhotoOf(item) : nullptr;
		if (i == videoPaths.end() || !photo) {
			continue;
		}
		const auto parts = SplitPath(
			PhotoPathForVideo(i->second, u".jpg"_q));
		saver->add(
			photoId,
			photo,
			ReserveName(
				reservation,
				parts.base,
				parts.extension,
				FileExists));
	}
}

VideoSearch::VideoSearch(
	not_null<ChannelData*> channel,
	Query query,
	bool includePhotos)
: _channel(channel)
, _query(std::move(query))
, _includePhotos(includePhotos)
, _api(&channel->session().mtp()) {
}

VideoSearch::~VideoSearch() = default;

VideoSearch *VideoSearch::Find(not_null<Main::Session*> session) {
	const auto &accounts = Accounts();
	const auto i = accounts.find(session);
	return (i != accounts.end()) ? i->second.search.get() : nullptr;
}

not_null<VideoSearch*> VideoSearch::Start(
		not_null<ChannelData*> channel,
		Query query,
		bool includePhotos) {
	const auto session = &channel->session();
	if (const auto running = Find(session)) {
		return running;
	}
	auto &data = Account(session);
	data.search = std::make_unique<VideoSearch>(
		channel,
		std::move(query),
		includePhotos);
	const auto result = data.search.get();
	result->requestPage();
	return result;
}

void VideoSearch::stop() {
	DestroySearch(&_channel->session());
}

not_null<ChannelData*> VideoSearch::channel() const {
	return _channel;
}

rpl::producer<SearchProgress> VideoSearch::progressValue() const {
	return _progress.value();
}

rpl::lifetime &VideoSearch::lifetime() {
	return _lifetime;
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
		pageFailed();
	}).send();
}

void VideoSearch::pageFailed() {
	// Keep what was collected so far.
	_failed = true;
	requestAlbums();
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
	auto progress = _progress.current();
	progress.scanned += received;
	progress.total = std::max(progress.total, TotalFrom(result));
	_progress = progress;
	updateProgress();

	const auto complete = !list
		|| (result.type() == mtpc_messages_messages)
		|| !received
		|| (_offsetId && minId >= _offsetId);
	if (!complete) {
		_offsetId = minId;
		requestPage();
		return;
	}
	requestAlbums();
}

void VideoSearch::requestAlbums() {
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
		updateProgress();
		requestNextChunk();
	}).fail([=] {
		// A failed chunk only means fewer album items, keep going.
		requestNextChunk();
	}).send();
}

void VideoSearch::updateProgress() {
	const auto selection = select();
	auto progress = _progress.current();
	progress.videos = int(selection.videos.size());
	progress.photos = int(selection.photos.size());
	_progress = progress;
}

MediaSelection VideoSearch::select() const {
	auto candidates = std::vector<Candidate>();
	const auto add = [&](not_null<HistoryItem*> item) {
		const auto video = VideoOf(item);
		const auto photo = video ? nullptr : PhotoOf(item);
		candidates.push_back({
			.msgId = item->id.bare,
			.groupId = item->groupId().value,
			.documentId = video ? uint64(video->id) : uint64(0),
			.isVideo = (video != nullptr),
			.skip = video && SkipVideo(video),
			.photoId = photo ? uint64(photo->id) : uint64(0),
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
	return SelectMedia(_posts, candidates, _includePhotos);
}

void VideoSearch::finish() {
	if (_finished) {
		return;
	}
	_finished = true;

	const auto selection = select();
	const auto peerId = _channel->id;
	auto outcome = SearchOutcome{
		.channel = _channel,
		.partial = IsPartialResult(_failed, _progress.current().scanned),
		.failed = _failed && !_progress.current().scanned,
	};
	for (const auto id : selection.videos) {
		const auto itemId = FullMsgId(peerId, MsgId(id));
		outcome.videos.push_back(itemId);
		if (const auto item = lookup(id)) {
			if (const auto video = VideoOf(item)) {
				outcome.size += video->size;
			}
		}
	}
	for (const auto &pick : selection.photos) {
		outcome.photos.push_back({
			.photo = FullMsgId(peerId, MsgId(pick.msgId)),
			.video = FullMsgId(peerId, MsgId(pick.videoMsgId)),
		});
		if (const auto item = lookup(pick.msgId)) {
			if (const auto photo = PhotoOf(item)) {
				// Unknown sizes (0) are left out of the total.
				outcome.size += std::max(
					photo->imageByteSize(Data::PhotoSize::Large),
					0);
			}
		}
	}

	const auto session = &_channel->session();
	ShowDownloadVideosResult(session, std::move(outcome));

	// We're inside an MTP handler of our own sender, destroy later.
	crl::on_main(this, [=] {
		DestroySearch(session);
	});
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

} // namespace AyuBulkDownload
