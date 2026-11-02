// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

// Pure logic of the bulk video download feature.
// No UI, no session, no MTP: only Qt containers and lib_base types,
// so it can be unit-tested in isolation.

#include "base/basic_types.h"

#include <QDate>
#include <QString>

#include <functional>
#include <vector>

namespace AyuBulkDownload {

inline constexpr auto kMaxAlbumSize = 10;
inline constexpr auto kIdsPerRequest = 100;
inline constexpr auto kSearchPerPage = 100;

struct Query {
	QString phrase; // Trimmed and lowercased, empty = no constraint.
	QString hashtag; // Trimmed, lowercased, '#'-prefixed, empty = none.
	TimeId minDate = 0; // Inclusive, 0 = no constraint.
	TimeId maxDate = 0; // Inclusive, 0 = no constraint.
};

// from / till may be invalid (QDate()), meaning no constraint.
// minDate = start of `from` day, maxDate = last second of `till` day,
// both in local time.
[[nodiscard]] Query PrepareQuery(
	const QString &phrase,
	const QString &hashtag,
	QDate from,
	QDate till);

// The `q` for messages.search: phrase and hashtag joined by a space.
[[nodiscard]] QString SearchText(const Query &query);

// messages.search treats min_date / max_date as exclusive bounds,
// so widen by one second. Returns 0 when there is no constraint.
[[nodiscard]] TimeId ServerMinDate(const Query &query);
[[nodiscard]] TimeId ServerMaxDate(const Query &query);

// Lowercase containment of phrase and hashtag, plus inclusive date range.
[[nodiscard]] bool Matches(
	const Query &query,
	const QString &text,
	TimeId date);

struct IdRange {
	int64 from = 0; // Inclusive.
	int64 till = 0; // Inclusive.
};

// Album items have adjacent ids and there are at most kMaxAlbumSize of
// them, so the whole album lies within [max - 9, min + 9] of any known
// subset of its member ids. Ids below 1 are clamped away.
// Returns an empty range ({0, -1}) when knownMemberIds is empty.
[[nodiscard]] IdRange AlbumIdRange(const std::vector<int64> &knownMemberIds);

// Each element of `albums` is the known member ids of one matched album.
// Returns sorted unique ids inside the albums' ranges that are not known
// yet (isKnown returns false), to be fetched from the server.
[[nodiscard]] std::vector<int64> AlbumNeighborIds(
	const std::vector<std::vector<int64>> &albums,
	const std::function<bool(int64)> &isKnown);

// Splits ids into consecutive chunks of at most chunkSize.
[[nodiscard]] std::vector<std::vector<int64>> ChunkIds(
	const std::vector<int64> &ids,
	int chunkSize = kIdsPerRequest);

struct MatchedPost {
	int64 msgId = 0;
	uint64 groupId = 0; // 0 when the post is a single message.
};

struct Candidate {
	int64 msgId = 0;
	uint64 groupId = 0; // 0 when not part of an album.
	uint64 documentId = 0; // 0 when there is no document.
	bool isVideo = false; // A normal video file (not GIF / round / image).
	bool skip = false; // Already downloaded or downloading.
};

// Takes every video of every matched post (the message itself, or every
// candidate of its album), drops skipped ones and duplicates (same message
// or same document), and returns message ids sorted ascending.
[[nodiscard]] std::vector<int64> SelectVideos(
	const std::vector<MatchedPost> &posts,
	const std::vector<Candidate> &candidates);

} // namespace AyuBulkDownload
