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
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/effects/round_checkbox.h"
#include "window/window_session_controller.h"

#include <map>

#include "styles/style_dialogs.h"
#include "styles/style_overview.h"

namespace AyuBulkDownload {
namespace {

using MessagesList = QVector<MTPMessage>;

constexpr auto kPostsPerSlice = 50;
constexpr auto kSliceDuration = crl::time(8);

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
	std::map<QString, NameReservation> reservations; // By folder.
	std::unique_ptr<PhotoSaver> photos;
	std::vector<std::unique_ptr<DownloadBatch>> batches;
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
	// Logout or session destruction cancels the batches and photo saves.
	session->lifetime().add([=] {
		auto taken = base::take(Accounts()[session]);
		Accounts().erase(session);
	});
	return accounts[session];
}

[[nodiscard]] not_null<PhotoSaver*> PhotoSaverFor(
		not_null<Main::Session*> session) {
	auto &result = Account(session).photos;
	if (!result) {
		result = std::make_unique<PhotoSaver>(session);
	}
	return result.get();
}

void DestroyBatch(
		not_null<DownloadBatch*> batch,
		not_null<Main::Session*> session) {
	auto &accounts = Accounts();
	const auto i = accounts.find(session);
	if (i == accounts.end()) {
		return;
	}
	auto &batches = i->second.batches;
	const auto j = ranges::find_if(batches, [&](const auto &owned) {
		return (owned.get() == batch);
	});
	if (j != end(batches)) {
		auto taken = std::move(*j);
		batches.erase(j);
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

// Unlike Session::tryResolveWindow() it never activates the account.
[[nodiscard]] Window::SessionController *ExistingWindow(
		not_null<Main::Session*> session) {
	auto result = (Window::SessionController*)nullptr;
	for (const auto &window : session->windows()) {
		if (!result || window->isPrimary()) {
			result = window;
		}
	}
	return result;
}

[[nodiscard]] QString AddedText(
		int videos,
		int photos,
		bool includePhotos,
		int failedPosts,
		bool pageFailed) {
	const auto countVideos = tr::ayu_DownloadVideosCount(
		tr::now,
		lt_count_decimal,
		videos);
	const auto items = includePhotos
		? tr::ayu_DownloadVideosAndPhotos(
			tr::now,
			lt_videos,
			countVideos,
			lt_photos,
			tr::ayu_DownloadPhotosCount(tr::now, lt_count_decimal, photos))
		: countVideos;
	auto result = tr::ayu_DownloadAdded(tr::now, lt_items, items);
	if (failedPosts > 0) {
		result += ' ' + tr::ayu_DownloadFailedPosts(
			tr::now,
			lt_count_decimal,
			failedPosts);
	} else if (pageFailed) {
		result += ' ' + tr::ayu_DownloadSomeFailed(tr::now);
	}
	return result;
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

SearchPager::SearchPager(not_null<ChannelData*> channel, QString query)
: _channel(channel)
, _query(std::move(query))
, _api(&channel->session().mtp()) {
}

void SearchPager::start(Handlers handlers) {
	_handlers = std::move(handlers);
	_loading = true;
	requestPage();
}

void SearchPager::setHandlers(Handlers handlers) {
	_handlers = std::move(handlers);
}

bool SearchPager::loading() const {
	return _loading;
}

SearchProgress SearchPager::progress() const {
	return _progress;
}

void SearchPager::requestPage() {
	// Flood waits are delayed and retried by the MTP layer, as usual.
	_api.request(MTPmessages_Search(
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
		pageReceived(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("AyuGram bulk: search page failed: %1").arg(error.type()));
		_loading = false;
		_handlers.done(true);
	}).send();
}

void SearchPager::pageReceived(const MTPmessages_Messages &result) {
	auto &owner = _channel->owner();
	owner.processExistingMessages(_channel, result);

	const auto list = MessagesFrom(result);
	auto minId = MsgId();
	auto received = 0;
	auto posts = std::vector<MatchedPost>();
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
				posts.push_back({
					.msgId = id.bare,
					.groupId = item->groupId().value,
				});
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
	} else {
		_offsetId = minId;
		requestPage();
	}
	// The handlers may destroy neither this pager nor its owner.
	_handlers.page(std::move(posts));
	if (complete) {
		_handlers.done(false);
	}
}

DownloadBatch::DownloadBatch(
	not_null<ChannelData*> channel,
	PostSelection selection,
	std::unique_ptr<SearchPager> pager,
	bool includePhotos,
	QString folder,
	not_null<NameReservation*> reservation)
: _channel(channel)
, _includePhotos(includePhotos)
, _folder(std::move(folder))
, _api(&channel->session().mtp())
, _selection(std::move(selection))
, _pager(std::move(pager))
, _reservation(reservation) {
}

DownloadBatch::~DownloadBatch() = default;

not_null<DownloadBatch*> DownloadBatch::Start(
		not_null<ChannelData*> channel,
		PostSelection selection,
		std::unique_ptr<SearchPager> pager,
		bool includePhotos,
		const QString &folder) {
	const auto session = &channel->session();
	const auto target = ResolveDownloadFolder(
		folder,
		Core::App().settings().downloadPath(),
		FileDialog::Tmp(),
		File::DefaultDownloadPath(session),
		session->local().tempDirectory());
	if (!target.isEmpty() && !QDir().exists(target)) {
		QDir().mkpath(target);
	}
	const auto posts = selection.posts();
	auto &account = Account(session);
	auto &batches = account.batches;
	batches.push_back(std::make_unique<DownloadBatch>(
		channel,
		std::move(selection),
		std::move(pager),
		includePhotos,
		target,
		&account.reservations[target]));
	const auto result = batches.back().get();
	LOG(("AyuGram bulk: batch started: %1 posts, paging: %2"
		).arg(int(posts.size())
		).arg(Logs::b(result->_pager != nullptr)));
	if (const auto raw = result->_pager.get()) {
		raw->setHandlers({
			.page = [=](std::vector<MatchedPost> found) {
				for (const auto &post : found) {
					if (result->_selection.addFound(post)) {
						result->queue(post);
					}
				}
				result->processNext();
			},
			.done = [=](bool failed) {
				if (failed) {
					const auto progress = raw->progress();
					result->_pageFailed = true;
					result->_failedPosts += std::max(
						progress.total - progress.scanned,
						0);
				}
				crl::on_main(result, [=] {
					result->checkFinished();
				});
			},
		});
	}
	for (const auto &post : posts) {
		result->queue(post);
	}
	result->processNext();
	return result;
}

void DownloadBatch::cancel() {
	if (_finished) {
		return;
	}
	LOG(("AyuGram bulk: batch cancelled"));
	_api.request(base::take(_requestId)).cancel();
	_pager = nullptr;
	_pending.clear();
	_requested.clear();
	finish();
}

rpl::producer<DownloadBatch::State> DownloadBatch::stateValue() const {
	return _state.value();
}

rpl::lifetime &DownloadBatch::lifetime() {
	return _lifetime;
}

void DownloadBatch::queue(MatchedPost post) {
	if (_queued.emplace(post.msgId).second) {
		_pending.push_back(post);
	}
}

void DownloadBatch::processNext() {
	if (_requestId || _finished || _scheduled) {
		pushState();
		return;
	}
	// WHY: saving thousands of posts in one turn freezes the window and
	// the progress line, so the work is split into short slices.
	const auto started = crl::now();
	auto ids = std::vector<int64>();
	auto added = 0;
	while (!_pending.empty()) {
		if (added >= kPostsPerSlice
			|| crl::now() - started >= kSliceDuration) {
			_scheduled = true;
			crl::on_main(this, [=] {
				_scheduled = false;
				processNext();
			});
			pushState();
			return;
		}
		const auto post = _pending.front();
		const auto neighbors = (post.groupId && !_fetched.contains(post.msgId))
			? AlbumNeighborIds(
				std::vector<std::vector<int64>>{ { post.msgId } },
				[&](int64 id) { return lookup(id) != nullptr; })
			: std::vector<int64>();
		if (int(ids.size() + neighbors.size()) > kIdsPerRequest) {
			break;
		}
		_pending.pop_front();
		if (neighbors.empty()) {
			const auto hasVideo = addPost(post);
			if (!hasVideo && _fetchFailed.contains(post.msgId)) {
				++_failedPosts;
			}
			++added;
			continue;
		}
		ids.insert(end(ids), begin(neighbors), end(neighbors));
		_requested.push_back(post);
	}
	if (ids.empty()) {
		checkFinished();
		return;
	}
	auto inputs = QVector<MTPInputMessage>();
	inputs.reserve(ids.size());
	for (const auto id : ids) {
		inputs.push_back(MTP_inputMessageID(MTP_int(id)));
	}
	// Fetched posts go back to the queue, to be added in slices.
	const auto fetched = [=](bool failed) {
		_requestId = 0;
		auto requested = base::take(_requested);
		for (auto i = rbegin(requested); i != rend(requested); ++i) {
			_fetched.emplace(i->msgId);
			if (failed) {
				_fetchFailed.emplace(i->msgId);
			}
			_pending.push_front(*i);
		}
		processNext();
	};
	_requestId = _api.request(MTPchannels_GetMessages(
		_channel->inputChannel(),
		MTP_vector<MTPInputMessage>(inputs)
	)).done([=](const MTPmessages_Messages &result) {
		_channel->owner().processExistingMessages(_channel, result);
		fetched(false);
	}).fail([=] {
		LOG(("AyuGram bulk: album chunk failed"));
		fetched(true);
	}).send();
	pushState();
}

bool DownloadBatch::addPost(MatchedPost post) {
	++_processed;
	if (post.groupId && !_doneGroups.emplace(post.groupId).second) {
		return true;
	}
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
	const auto range = post.groupId
		? AlbumIdRange({ post.msgId })
		: IdRange{ post.msgId, post.msgId };
	for (auto id = range.from; id <= range.till; ++id) {
		if (const auto item = lookup(id)) {
			add(item);
		}
	}
	const auto hasVideo = ranges::any_of(candidates, [](const Candidate &c) {
		return c.isVideo;
	});
	const auto selection = SelectMedia({ post }, candidates, _includePhotos);
	if (_folder.isEmpty()) {
		return hasVideo;
	}
	const auto peerId = _channel->id;
	auto videoPaths = std::map<int64, QString>();
	for (const auto id : selection.videos) {
		const auto item = lookup(id);
		const auto video = item ? VideoOf(item) : nullptr;
		if (!video || video->loading()) {
			continue;
		}
		const auto path = SaveVideoAndTrack(
			FullMsgId(peerId, MsgId(id)),
			video,
			_folder,
			*_reservation);
		if (!path.isEmpty()) {
			videoPaths.emplace(id, path);
			++_videos;
		}
	}
	for (const auto &pick : selection.photos) {
		const auto i = videoPaths.find(pick.videoMsgId);
		const auto item = lookup(pick.msgId);
		const auto photo = item ? PhotoOf(item) : nullptr;
		if (i == videoPaths.end() || !photo) {
			continue;
		}
		const auto parts = SplitPath(
			PhotoPathForVideo(i->second, u".jpg"_q));
		PhotoSaverFor(&_channel->session())->add(
			FullMsgId(peerId, MsgId(pick.msgId)),
			photo,
			ReserveName(
				*_reservation,
				parts.base,
				parts.extension,
				FileExists));
		++_photos;
	}
	return hasVideo;
}

void DownloadBatch::checkFinished() {
	if (!_finished
		&& !_requestId
		&& _pending.empty()
		&& !(_pager && _pager->loading())) {
		finish();
	} else {
		pushState();
	}
}

void DownloadBatch::finish() {
	_finished = true;
	pushState();
	LOG(("AyuGram bulk: batch finished: %1 videos, %2 photos, %3 failed"
		).arg(_videos
		).arg(_photos
		).arg(_failedPosts));
	if (const auto window = ExistingWindow(&_channel->session())) {
		window->showToast(AddedText(
			_videos,
			_photos,
			_includePhotos,
			_failedPosts,
			_pageFailed));
	}
	// We may be inside an MTP handler of our own sender, destroy later.
	const auto session = &_channel->session();
	crl::on_main(this, [=] {
		DestroyBatch(this, session);
	});
}

void DownloadBatch::pushState() {
	const auto pagingLeft = (_pager && _pager->loading())
		? std::max(_pager->progress().total - _pager->progress().scanned, 0)
		: 0;
	_state = State{
		.added = _videos + _photos,
		.progress = _finished
			? 1.
			: ProgressRatio({
				.scanned = _processed,
				.total = int(_queued.size()) + pagingLeft,
			}),
	};
}

HistoryItem *DownloadBatch::lookup(int64 id) const {
	return _channel->owner().message(_channel, MsgId(id));
}

SearchSelection::SearchSelection(
	not_null<ChannelData*> channel,
	QString query,
	Fn<std::vector<not_null<HistoryItem*>>()> shownResults,
	Fn<void()> repaint)
: _channel(channel)
, _query(std::move(query))
, _shownResults(std::move(shownResults))
, _repaint(std::move(repaint)) {
}

SearchSelection::~SearchSelection() = default;

bool SearchSelection::toggle(not_null<const HistoryItem*> item) {
	if (item->history()->peer != _channel || _adding) {
		return false;
	} else if (_closing) {
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
	if (_adding || _closing) {
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
	if (!_foundAll && !_pager) {
		LOG(("AyuGram bulk: select all started"));
		_found.clear();
		_pager = std::make_unique<SearchPager>(_channel, _query);
		_pager->start({
			.page = [=](std::vector<MatchedPost> found) {
				for (const auto &post : found) {
					_found.push_back(post);
					_selection.addFound(post);
				}
				pushState();
				_repaint();
			},
			.done = [=](bool failed) {
				_foundAll = !failed;
				// We're inside a handler of the pager, destroy later.
				crl::on_main(this, [=] {
					if (_pager && !_pager->loading()) {
						_pager = nullptr;
						pushState();
					}
				});
			},
		});
	}
	pushState();
	_repaint();
}

void SearchSelection::startDownload(
		bool includePhotos,
		const QString &folder) {
	if (_adding || _closing) {
		return;
	}
	auto pager = (_pager && _pager->loading())
		? std::move(_pager)
		: nullptr;
	_pager = nullptr;
	const auto batch = DownloadBatch::Start(
		_channel,
		_selection,
		std::move(pager),
		includePhotos,
		folder);
	_batch = batch;
	_adding = true;
	_checks.clear();
	batch->stateValue(
	) | rpl::on_next([=](const DownloadBatch::State &state) {
		_batchState = state;
		pushState();
	}, _lifetime);
	batch->lifetime().add(crl::guard(this, [=] {
		_batch = nullptr;
		close();
	}));
	_repaint();
}

void SearchSelection::close() {
	if (_closing) {
		return;
	}
	_closing = true;
	_pager = nullptr;
	if (const auto batch = _batch.get()) {
		batch->cancel();
	}
	_closeRequests.fire({});
}

std::optional<int> SearchSelection::finalCount() const {
	return (_pager && _pager->loading())
		? std::nullopt
		: std::make_optional(_selection.count());
}

void SearchSelection::paintCheck(
		QPainter &p,
		not_null<const HistoryItem*> item,
		const style::DialogRow &st,
		int outerWidth) {
	if (item->history()->peer != _channel || _adding) {
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

void SearchSelection::pushState() {
	const auto loading = _pager && _pager->loading();
	_state = State{
		.count = _selection.count(),
		.loading = loading,
		.adding = _adding,
		.added = _batchState.added,
		.progress = _adding
			? _batchState.progress
			: loading
			? ProgressRatio(_pager->progress())
			: 0.,
	};
}

} // namespace AyuBulkDownload
