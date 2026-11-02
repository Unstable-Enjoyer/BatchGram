// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/bulk_download/bulk_download_logic.h"

#include <QDateTime>

#include <algorithm>
#include <set>

namespace AyuBulkDownload {

Query PrepareQuery(
		const QString &phrase,
		const QString &hashtag,
		QDate from,
		QDate till) {
	auto result = Query();
	result.phrase = phrase.trimmed().toLower();
	auto tag = hashtag.trimmed().toLower();
	if (!tag.isEmpty() && !tag.startsWith(QChar('#'))) {
		tag = QChar('#') + tag;
	}
	result.hashtag = (tag.size() > 1) ? tag : QString();
	if (from.isValid()) {
		result.minDate = TimeId(from.startOfDay().toSecsSinceEpoch());
	}
	if (till.isValid()) {
		result.maxDate = TimeId(
			till.addDays(1).startOfDay().toSecsSinceEpoch() - 1);
	}
	return result;
}

QString SearchText(const Query &query) {
	if (query.phrase.isEmpty()) {
		return query.hashtag;
	} else if (query.hashtag.isEmpty()) {
		return query.phrase;
	}
	return query.phrase + QChar(' ') + query.hashtag;
}

TimeId ServerMinDate(const Query &query) {
	return query.minDate ? (query.minDate - 1) : 0;
}

TimeId ServerMaxDate(const Query &query) {
	return query.maxDate ? (query.maxDate + 1) : 0;
}

bool Matches(const Query &query, const QString &text, TimeId date) {
	if (query.minDate && date < query.minDate) {
		return false;
	} else if (query.maxDate && date > query.maxDate) {
		return false;
	}
	if (query.phrase.isEmpty() && query.hashtag.isEmpty()) {
		return true;
	}
	const auto lower = text.toLower();
	return (query.phrase.isEmpty() || lower.contains(query.phrase))
		&& (query.hashtag.isEmpty() || lower.contains(query.hashtag));
}

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

std::vector<int64> SelectVideos(
		const std::vector<MatchedPost> &posts,
		const std::vector<Candidate> &candidates) {
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
	auto result = std::vector<int64>();
	auto messages = std::set<int64>();
	auto documents = std::set<uint64>();
	for (const auto &candidate : sorted) {
		if (!candidate.isVideo || candidate.skip || !candidate.documentId) {
			continue;
		}
		const auto inPost = candidate.groupId
			? groups.contains(candidate.groupId)
			: singles.contains(candidate.msgId);
		if (!inPost) {
			continue;
		} else if (!messages.insert(candidate.msgId).second) {
			continue;
		} else if (!documents.insert(candidate.documentId).second) {
			continue;
		}
		result.push_back(candidate.msgId);
	}
	return result;
}

} // namespace AyuBulkDownload
