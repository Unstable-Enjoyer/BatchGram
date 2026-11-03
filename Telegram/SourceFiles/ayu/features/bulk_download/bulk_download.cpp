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
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/effects/round_checkbox.h"

#include <map>

#include "styles/style_dialogs.h"
#include "styles/style_overview.h"

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

[[nodiscard]] auto PhotoSavers()
-> std::map<not_null<Main::Session*>, std::unique_ptr<PhotoSaver>> & {
	static auto result = std::map<
		not_null<Main::Session*>,
		std::unique_ptr<PhotoSaver>>();
	return result;
}

[[nodiscard]] not_null<PhotoSaver*> PhotoSaverFor(
		not_null<Main::Session*> session) {
	auto &savers = PhotoSavers();
	auto &result = savers[session];
	if (!result) {
		result = std::make_unique<PhotoSaver>(session);
		// Logout or session destruction cancels the photo saves.
		session->lifetime().add([=] {
			auto taken = base::take(PhotoSavers()[session]);
			PhotoSavers().erase(session);
		});
	}
	return result.get();
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
		|| document->forbidsFileSave()
		|| !document->filepath(true).isEmpty();
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
		const DownloadSet &set,
		bool includePhotos,
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
	for (const auto &id : set.videos) {
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
	if (!includePhotos || set.photos.empty()) {
		return;
	}
	const auto saver = PhotoSaverFor(session);
	for (const auto &[photoId, videoId] : set.photos) {
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

ChannelData *SelectableSearchChannel(const Dialogs::SearchState &state) {
	if (state.tab != Dialogs::ChatSearchTab::ThisPeer
		|| state.fromPeer
		|| !state.tags.empty()
		|| state.query.trimmed().isEmpty()) {
		return nullptr;
	}
	const auto history = state.inChat.history();
	const auto channel = history ? history->peer->asChannel() : nullptr;
	return (channel && !channel->isMonoforum()) ? channel : nullptr;
}

SearchSelection::SearchSelection(
	not_null<ChannelData*> channel,
	QString query,
	Fn<std::vector<not_null<HistoryItem*>>()> shownResults,
	Fn<void()> repaint)
: _channel(channel)
, _query(std::move(query))
, _shownResults(std::move(shownResults))
, _repaint(std::move(repaint))
, _api(&channel->session().mtp()) {
}

SearchSelection::~SearchSelection() = default;

bool SearchSelection::toggle(not_null<const HistoryItem*> item) {
	if (item->history()->peer != _channel) {
		return false;
	} else if (_ready) {
		return true;
	}
	_selection.toggle({
		.msgId = item->id.bare,
		.groupId = item->groupId().value,
	});
	pushState();
	_repaint();
	return true;
}

void SearchSelection::selectAll() {
	if (_ready) {
		return;
	}
	_selection.forgetUnticked();
	for (const auto &item : _shownResults()) {
		if (item->history()->peer == _channel
			&& (item->groupId() || VideoOf(item) || PhotoOf(item))) {
			_selection.addFound({
				.msgId = item->id.bare,
				.groupId = item->groupId().value,
			});
		}
	}
	for (const auto &post : _found) {
		_selection.addFound(post);
	}
	if (!_foundAll && !_loading) {
		LOG(("AyuGram bulk: select all started"));
		_found.clear();
		_offsetId = 0;
		_progress = SearchProgress();
		_loading = true;
		requestPage();
	}
	pushState();
	_repaint();
}

void SearchSelection::stop() {
	if (_loading) {
		_api.request(base::take(_requestId)).cancel();
		_loading = false;
		_partial = true;
	} else if (_ready) {
		_api.request(base::take(_requestId)).cancel();
		_ready = nullptr;
	}
	pushState();
}

void SearchSelection::prepareDownload(Fn<void(DownloadSet)> ready) {
	if (_ready) {
		return;
	}
	stop();
	_ready = std::move(ready);

	auto byGroup = std::map<uint64, std::vector<int64>>();
	for (const auto &post : _selection.posts()) {
		if (post.groupId) {
			byGroup[post.groupId].push_back(post.msgId);
		}
	}
	auto albums = std::vector<std::vector<int64>>();
	albums.reserve(byGroup.size());
	for (auto &[groupId, members] : byGroup) {
		albums.push_back(std::move(members));
	}
	const auto neighbors = AlbumNeighborIds(albums, [&](int64 id) {
		return lookup(id) != nullptr;
	});
	_chunks = ChunkIds(neighbors);
	_chunkIndex = 0;
	LOG(("AyuGram bulk: album phase: %1 ids, %2 chunks"
		).arg(int(neighbors.size())
		).arg(int(_chunks.size())));
	pushState();
	requestNextChunk();
}

void SearchSelection::close() {
	stop();
	_closeRequests.fire({});
}

void SearchSelection::paintCheck(
		QPainter &p,
		not_null<const HistoryItem*> item,
		const style::DialogRow &st,
		int outerWidth) {
	if (item->history()->peer != _channel) {
		return;
	}
	const auto checked = _selection.contains(item->id.bare);
	auto &check = _checks[item->id];
	if (!check) {
		check = std::make_unique<Ui::RoundCheckbox>(
			st::overviewSmallCheck,
			_repaint);
		check->setDisplayInactive(true);
		check->setChecked(checked, anim::type::instant);
	} else {
		check->setChecked(checked);
	}
	// Same corner of the thumbnail as the shared files list selection.
	const auto shift = st.photoSize
		+ st::overviewCheckSkip
		- st::overviewSmallCheck.size;
	check->paint(
		p,
		st.padding.left() + shift,
		st.padding.top() + shift,
		outerWidth);
}

rpl::producer<SearchSelection::State> SearchSelection::stateValue() const {
	return _state.value();
}

rpl::producer<> SearchSelection::closeRequests() const {
	return _closeRequests.events();
}

rpl::lifetime &SearchSelection::lifetime() {
	return _lifetime;
}

void SearchSelection::requestPage() {
	// Flood waits are delayed and retried by the MTP layer, as usual.
	_requestId = _api.request(MTPmessages_Search(
		MTP_flags(0),
		_channel->input(),
		MTP_string(_query),
		MTP_inputPeerEmpty(), // from_id
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTP_int(0), // top_msg_id
		MTP_inputMessagesFilterPhotoVideo(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(_offsetId.bare),
		MTP_int(0), // add_offset
		MTP_int(kSearchPerPage),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		_requestId = 0;
		pageReceived(result);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		LOG(("AyuGram bulk: search page failed: %1").arg(error.type()));
		pageFailed();
	}).send();
}

void SearchSelection::pageFailed() {
	// Keep what was found so far.
	_loading = false;
	_partial = true;
	pushState();
}

void SearchSelection::pageReceived(const MTPmessages_Messages &result) {
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
			if (const auto item = owner.message(_channel, id)) {
				const auto post = MatchedPost{
					.msgId = id.bare,
					.groupId = item->groupId().value,
				};
				_found.push_back(post);
				_selection.addFound(post);
			}
		}
	}
	_progress.scanned += received;
	_progress.total = std::max(_progress.total, TotalFrom(result));
	LOG(("AyuGram bulk: page received: %1 received, %2 scanned, %3 total"
		).arg(received
		).arg(_progress.scanned
		).arg(_progress.total));

	const auto complete = !list
		|| (result.type() == mtpc_messages_messages)
		|| SearchPagingDone(received, _progress.scanned, _progress.total)
		|| (_offsetId && minId >= _offsetId);
	if (complete) {
		LOG(("AyuGram bulk: paging done"));
		_loading = false;
		_foundAll = true;
		_partial = false;
	} else {
		_offsetId = minId;
		requestPage();
	}
	pushState();
	_repaint();
}

void SearchSelection::requestNextChunk() {
	if (_chunkIndex >= int(_chunks.size())) {
		finishPreparing();
		return;
	}
	const auto index = _chunkIndex++;
	auto ids = QVector<MTPInputMessage>();
	for (const auto id : _chunks[index]) {
		ids.push_back(MTP_inputMessageID(MTP_int(id)));
	}
	_requestId = _api.request(MTPchannels_GetMessages(
		_channel->inputChannel(),
		MTP_vector<MTPInputMessage>(ids)
	)).done([=](const MTPmessages_Messages &result) {
		_requestId = 0;
		_channel->owner().processExistingMessages(_channel, result);
		pushState();
		requestNextChunk();
	}).fail([=] {
		_requestId = 0;
		LOG(("AyuGram bulk: chunk %1 failed").arg(index));
		// A failed chunk only means fewer album items, keep going.
		requestNextChunk();
	}).send();
}

void SearchSelection::finishPreparing() {
	auto set = collect();
	LOG(("AyuGram bulk: prepared: %1 videos, %2 photos, partial: %3"
		).arg(int(set.videos.size())
		).arg(int(set.photos.size())
		).arg(Logs::b(set.partial)));
	const auto ready = base::take(_ready);
	pushState();
	ready(std::move(set));
}

void SearchSelection::pushState() {
	const auto preparing = (_ready != nullptr);
	_state = State{
		.count = _selection.count(),
		.loading = _loading,
		.preparing = preparing,
		.progress = _loading
			? ProgressRatio(_progress)
			: preparing
			? ProgressRatio({
				.scanned = _chunkIndex,
				.total = int(_chunks.size()),
			})
			: 0.,
	};
}

HistoryItem *SearchSelection::lookup(int64 id) const {
	return _channel->owner().message(_channel, MsgId(id));
}

DownloadSet SearchSelection::collect() const {
	const auto posts = _selection.posts();
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
	auto byGroup = std::map<uint64, std::vector<int64>>();
	for (const auto &post : posts) {
		if (post.groupId) {
			byGroup[post.groupId].push_back(post.msgId);
		} else if (const auto item = lookup(post.msgId)) {
			add(item);
		}
	}
	for (const auto &[groupId, members] : byGroup) {
		const auto range = AlbumIdRange(members);
		for (auto id = range.from; id <= range.till; ++id) {
			if (const auto item = lookup(id)) {
				add(item);
			}
		}
	}
	const auto selection = SelectMedia(posts, candidates, true);

	const auto peerId = _channel->id;
	auto result = DownloadSet{
		.channel = _channel,
		.partial = _partial,
	};
	for (const auto id : selection.videos) {
		result.videos.push_back(FullMsgId(peerId, MsgId(id)));
		if (const auto item = lookup(id)) {
			if (const auto video = VideoOf(item)) {
				result.videosSize += video->size;
			}
		}
	}
	for (const auto &pick : selection.photos) {
		result.photos.push_back({
			.photo = FullMsgId(peerId, MsgId(pick.msgId)),
			.video = FullMsgId(peerId, MsgId(pick.videoMsgId)),
		});
		if (const auto item = lookup(pick.msgId)) {
			if (const auto photo = PhotoOf(item)) {
				// Unknown sizes (0) are left out of the total.
				result.photosSize += std::max(
					photo->imageByteSize(Data::PhotoSize::Large),
					0);
			}
		}
	}
	return result;
}

} // namespace AyuBulkDownload
