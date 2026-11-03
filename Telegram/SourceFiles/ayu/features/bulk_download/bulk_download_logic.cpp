// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/bulk_download/bulk_download_logic.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace AyuBulkDownload {

IdRange AlbumIdRange(const std::vector<int64> &knownMemberIds) {
	if (knownMemberIds.empty()) {
		return { 0, -1 };
	}
	const auto [min, max] = std::minmax_element(
		knownMemberIds.begin(),
		knownMemberIds.end());
	const auto span = int64(kMaxAlbumSize - 1);
	return {
		std::max(*max - span, int64(1)),
		*min + span,
	};
}

std::vector<int64> AlbumNeighborIds(
		const std::vector<std::vector<int64>> &albums,
		const std::function<bool(int64)> &isKnown) {
	auto result = std::set<int64>();
	for (const auto &members : albums) {
		const auto range = AlbumIdRange(members);
		for (auto id = range.from; id <= range.till; ++id) {
			if (!isKnown || !isKnown(id)) {
				result.insert(id);
			}
		}
	}
	return { result.begin(), result.end() };
}

std::vector<std::vector<int64>> ChunkIds(
		const std::vector<int64> &ids,
		int chunkSize) {
	auto result = std::vector<std::vector<int64>>();
	if (chunkSize <= 0) {
		return result;
	}
	for (auto i = std::size_t(0); i < ids.size(); i += chunkSize) {
		const auto till = std::min(ids.size(), i + std::size_t(chunkSize));
		result.emplace_back(ids.begin() + i, ids.begin() + till);
	}
	return result;
}

bool PostSelection::contains(int64 msgId) const {
	return _selected.contains(msgId);
}

int PostSelection::count() const {
	return int(_selected.size());
}

std::vector<MatchedPost> PostSelection::posts() const {
	auto result = std::vector<MatchedPost>();
	result.reserve(_selected.size());
	for (const auto &[msgId, groupId] : _selected) {
		result.push_back({ .msgId = msgId, .groupId = groupId });
	}
	return result;
}

bool PostSelection::toggle(MatchedPost post) {
	if (_selected.erase(post.msgId)) {
		_unticked.emplace(post.msgId);
		return false;
	}
	_unticked.erase(post.msgId);
	_selected.emplace(post.msgId, post.groupId);
	return true;
}

bool PostSelection::addFound(MatchedPost post) {
	return !_unticked.contains(post.msgId)
		&& _selected.emplace(post.msgId, post.groupId).second;
}

void PostSelection::forgetUnticked() {
	_unticked.clear();
}

std::vector<int64> SelectVideos(
		const std::vector<MatchedPost> &posts,
		const std::vector<Candidate> &candidates) {
	return SelectMedia(posts, candidates, false).videos;
}

MediaSelection SelectMedia(
		const std::vector<MatchedPost> &posts,
		const std::vector<Candidate> &candidates,
		bool includePhotos) {
	auto groups = std::set<uint64>();
	auto singles = std::set<int64>();
	for (const auto &post : posts) {
		if (post.groupId) {
			groups.insert(post.groupId);
		} else {
			singles.insert(post.msgId);
		}
	}
	auto sorted = candidates;
	std::sort(sorted.begin(), sorted.end(), [](
			const Candidate &a,
			const Candidate &b) {
		return a.msgId < b.msgId;
	});
	const auto inPost = [&](const Candidate &candidate) {
		return candidate.groupId
			? groups.contains(candidate.groupId)
			: singles.contains(candidate.msgId);
	};
	// Post key: the album group, or the single message id.
	using PostKey = std::pair<uint64, int64>;
	const auto key = [](const Candidate &candidate) {
		return candidate.groupId
			? PostKey(candidate.groupId, 0)
			: PostKey(0, candidate.msgId);
	};
	auto result = MediaSelection();
	auto messages = std::set<int64>();
	auto documents = std::set<uint64>();
	auto firstVideo = std::map<PostKey, int64>();
	for (const auto &candidate : sorted) {
		if (!candidate.isVideo || candidate.skip || !candidate.documentId) {
			continue;
		} else if (!inPost(candidate)) {
			continue;
		} else if (!messages.insert(candidate.msgId).second) {
			continue;
		} else if (!documents.insert(candidate.documentId).second) {
			continue;
		}
		result.videos.push_back(candidate.msgId);
		firstVideo.emplace(key(candidate), candidate.msgId);
	}
	if (!includePhotos) {
		return result;
	}
	auto photos = std::set<uint64>();
	for (const auto &candidate : sorted) {
		if (!candidate.photoId || !inPost(candidate)) {
			continue;
		}
		const auto i = firstVideo.find(key(candidate));
		if (i == firstVideo.end()) {
			continue;
		} else if (!messages.insert(candidate.msgId).second) {
			continue;
		} else if (!photos.insert(candidate.photoId).second) {
			continue;
		}
		result.photos.push_back({
			.msgId = candidate.msgId,
			.videoMsgId = i->second,
		});
	}
	return result;
}

PathParts SplitPath(const QString &path) {
	const auto slash = std::max(
		path.lastIndexOf(QChar('/')),
		path.lastIndexOf(QChar('\\')));
	const auto dot = path.lastIndexOf(QChar('.'));
	if (dot <= slash + 1) {
		return { path, QString() };
	}
	return { path.mid(0, dot), path.mid(dot) };
}

QString PhotoPathForVideo(
		const QString &videoPath,
		const QString &photoExtension) {
	return SplitPath(videoPath).base + photoExtension;
}

QString ReserveName(
		NameReservation &reservation,
		const QString &base,
		const QString &extension,
		const std::function<bool(const QString&)> &exists) {
	const auto make = [&](int index) {
		return index
			? (base + QString(" (%1)").arg(index + 1) + extension)
			: (base + extension);
	};
	const auto key = base + extension;
	auto index = reservation.next[key];
	auto result = make(index);
	while (reservation.taken.contains(result)
		|| (exists && exists(result))) {
		result = make(++index);
	}
	reservation.next[key] = index + 1;
	reservation.taken.insert(result);
	return result;
}

QString ResolveDownloadFolder(
		const QString &folderOverride,
		const QString &setting,
		const QString &tmpMarker,
		const QString &defaultFolder,
		const QString &tempFolder) {
	auto result = !folderOverride.isEmpty()
		? folderOverride
		: setting.isEmpty()
		? defaultFolder
		: (setting == tmpMarker)
		? tempFolder
		: setting;
	if (!result.isEmpty() && !result.endsWith(QChar('/'))) {
		result += QChar('/');
	}
	return result;
}

float64 ProgressRatio(const SearchProgress &progress) {
	const auto total = std::max(progress.total, progress.scanned);
	return (total > 0)
		? std::clamp(float64(progress.scanned) / total, 0., 1.)
		: 0.;
}

bool SearchPagingDone(int received, int scanned, int total) {
	return !received
		|| (total > 0 && scanned >= total && received < kSearchPerPage);
}

} // namespace AyuBulkDownload
