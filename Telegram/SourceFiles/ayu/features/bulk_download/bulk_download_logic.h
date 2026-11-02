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
#include <map>
#include <set>
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
	bool skip = false; // Already downloaded or downloading (videos only).
	uint64 photoId = 0; // Non-zero when the message is a photo.
};

// Takes every video of every matched post (the message itself, or every
// candidate of its album), drops skipped ones and duplicates (same message
// or same document), and returns message ids sorted ascending.
[[nodiscard]] std::vector<int64> SelectVideos(
	const std::vector<MatchedPost> &posts,
	const std::vector<Candidate> &candidates);

struct PhotoPick {
	int64 msgId = 0; // The photo message.
	int64 videoMsgId = 0; // First selected video of the same post.

	friend inline bool operator==(PhotoPick, PhotoPick) = default;
};

struct MediaSelection {
	std::vector<int64> videos; // Same as SelectVideos().
	std::vector<PhotoPick> photos; // Sorted by msgId.
};

// SelectVideos() plus, when includePhotos is set, every photo of each
// matched post that has at least one selected video, paired with the
// first (lowest id) selected video of that post. Photos are deduplicated
// by message and by photoId.
[[nodiscard]] MediaSelection SelectMedia(
	const std::vector<MatchedPost> &posts,
	const std::vector<Candidate> &candidates,
	bool includePhotos);

// Splits "/d/name.ext" into { "/d/name", ".ext" }. A file name without
// a dot (or starting with its only dot) gives an empty extension.
struct PathParts {
	QString base;
	QString extension;
};
[[nodiscard]] PathParts SplitPath(const QString &path);

// The photo path for a video saved to videoPath: the same folder and
// base name, with photoExtension (including the dot, e.g. ".jpg").
// "/d/Episode 12.mp4" -> "/d/Episode 12.jpg".
[[nodiscard]] QString PhotoPathForVideo(
	const QString &videoPath,
	const QString &photoExtension);

// Names picked during one download batch. A file may be created only when
// its download finishes, so `exists` alone can't tell that a name is taken.
// `next` keeps the " (N)" search for a name from restarting at 0.
struct NameReservation {
	std::set<QString> taken;
	std::map<QString, int> next;
};

// Returns base + extension, or base + " (N)" + extension with the lowest
// N >= 2 that is neither reserved nor existing, and reserves it.
[[nodiscard]] QString ReserveName(
	NameReservation &reservation,
	const QString &base,
	const QString &extension,
	const std::function<bool(const QString&)> &exists);

// The folder a batch downloads to, ending with '/', or empty if none.
// A non-empty folderOverride (picked for this batch) wins. Otherwise the
// download path setting is resolved as upstream does: empty means
// defaultFolder, tmpMarker (FileDialog::Tmp()) means tempFolder,
// anything else is the folder itself.
[[nodiscard]] QString ResolveDownloadFolder(
	const QString &folderOverride,
	const QString &setting,
	const QString &tmpMarker,
	const QString &defaultFolder,
	const QString &tempFolder);

struct SearchProgress {
	int scanned = 0; // Search results received so far.
	int total = 0; // The server `count`, 0 while unknown.
	int videos = 0; // Videos selected so far.
	int photos = 0; // Photos selected so far.

	friend inline bool operator==(
		const SearchProgress &,
		const SearchProgress &) = default;
};

// The total to show next to `scanned`: never less than scanned.
[[nodiscard]] int ProgressTotal(const SearchProgress &progress);

// Progress in [0, 1] for the bar: scanned / ProgressTotal().
[[nodiscard]] float64 ProgressRatio(const SearchProgress &progress);

// A search page failed: the result is partial (and still shown) when
// anything was scanned before the failure.
[[nodiscard]] bool IsPartialResult(bool failed, int scanned);

} // namespace AyuBulkDownload
