// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
//
// Plain console checks for bulk_download_logic.
// Built only with -DAYU_BULK_DOWNLOAD_TESTS=ON (target ayu_bulk_download_tests).
#include "ayu/features/bulk_download/bulk_download_logic.h"

#include <QString>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using namespace AyuBulkDownload;

int gFailed = 0;

#define CHECK(...) do { \
	if (!(__VA_ARGS__)) { \
		++gFailed; \
		std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
	} \
} while (false)

void TestAlbumIdRange() {
	const auto empty = AlbumIdRange(std::vector<int64>());
	CHECK(empty.from == 0);
	CHECK(empty.till == -1);

	// Members 100 and 103: range is [103 - 9, 100 + 9] = [94, 109].
	const auto normal = AlbumIdRange(std::vector<int64>{ 100, 103 });
	CHECK(normal.from == 94);
	CHECK(normal.till == 109);

	// Members 3 and 5: lower bound 5 - 9 clamps to 1, upper is 3 + 9 = 12.
	const auto clamped = AlbumIdRange(std::vector<int64>{ 3, 5 });
	CHECK(clamped.from == 1);
	CHECK(clamped.till == 12);
}

void TestAlbumNeighborIds() {
	const auto albums = std::vector<std::vector<int64>>{
		{ 100, 102 },
		{ 105, 106 },
	};
	// Ranges are [93, 109] and [97, 114], union [93, 114].
	const auto known = std::vector<int64>{ 100, 101, 102, 105, 106 };
	const auto isKnown = [&](int64 id) {
		return std::find(known.begin(), known.end(), id) != known.end();
	};
	auto expected = std::vector<int64>();
	for (auto id = int64(93); id <= 114; ++id) {
		if (!isKnown(id)) {
			expected.push_back(id);
		}
	}
	CHECK(AlbumNeighborIds(albums, isKnown) == expected);
}

void TestChunkIds() {
	auto ids100 = std::vector<int64>();
	for (auto id = int64(1); id <= 100; ++id) {
		ids100.push_back(id);
	}
	const auto chunks100 = ChunkIds(ids100);
	CHECK(chunks100.size() == 1 && chunks100[0].size() == 100);

	auto ids101 = ids100;
	ids101.push_back(101);
	const auto chunks101 = ChunkIds(ids101);
	CHECK(chunks101.size() == 2);
	CHECK(chunks101.size() == 2 && chunks101[0].size() == 100);
	CHECK(chunks101.size() == 2 && chunks101[1].size() == 1);
	CHECK(chunks101.size() == 2 && chunks101[1][0] == 101);

	CHECK(ChunkIds(std::vector<int64>()).empty());
}

Candidate Video(int64 msgId, uint64 groupId, uint64 documentId, bool skip) {
	return Candidate{ msgId, groupId, documentId, true, skip };
}

void TestSelectVideosSinglePost() {
	const auto posts = std::vector<MatchedPost>{ { 10, 0 } };
	const auto candidates = std::vector<Candidate>{ Video(10, 0, 500, false) };
	const auto expected = std::vector<int64>{ 10 };
	CHECK(SelectVideos(posts, candidates) == expected);
}

void TestSelectVideosNonVideoExcluded() {
	const auto posts = std::vector<MatchedPost>{ { 20, 0 } };
	const auto candidates = std::vector<Candidate>{
		{ 20, 0, 600, false, false },
	};
	CHECK(SelectVideos(posts, candidates).empty());
}

void TestSelectVideosAlbumCaptionOnPhoto() {
	// The caption sits on photo 31 of album 7; videos 32 and 33 must be taken.
	const auto posts = std::vector<MatchedPost>{ { 31, 7 } };
	const auto candidates = std::vector<Candidate>{
		{ 31, 7, 0, false, false },
		Video(32, 7, 601, false),
		Video(33, 7, 602, false),
	};
	const auto expected = std::vector<int64>{ 32, 33 };
	CHECK(SelectVideos(posts, candidates) == expected);
}

void TestSelectVideosSkippedExcluded() {
	const auto posts = std::vector<MatchedPost>{ { 40, 0 } };
	const auto candidates = std::vector<Candidate>{ Video(40, 0, 700, true) };
	CHECK(SelectVideos(posts, candidates).empty());
}

void TestSelectVideosDuplicatesRemoved() {
	const auto posts = std::vector<MatchedPost>{ { 50, 0 }, { 51, 0 } };
	// 50 appears twice (same message); 51 shares 50's document.
	const auto candidates = std::vector<Candidate>{
		Video(50, 0, 800, false),
		Video(50, 0, 800, false),
		Video(51, 0, 800, false),
	};
	const auto expected = std::vector<int64>{ 50 };
	CHECK(SelectVideos(posts, candidates) == expected);
}

void TestSelectVideosAscendingOrder() {
	const auto posts = std::vector<MatchedPost>{ { 70, 0 }, { 60, 0 }, { 65, 0 } };
	const auto candidates = std::vector<Candidate>{
		Video(70, 0, 1001, false),
		Video(60, 0, 1002, false),
		Video(65, 0, 1003, false),
	};
	const auto expected = std::vector<int64>{ 60, 65, 70 };
	CHECK(SelectVideos(posts, candidates) == expected);
}

Candidate Photo(int64 msgId, uint64 groupId, uint64 photoId) {
	return Candidate{ msgId, groupId, 0, false, false, photoId };
}

// Album 6: photo 30 and videos 31, 32, all in one post.
std::vector<Candidate> AlbumPhotoAndTwoVideos() {
	return std::vector<Candidate>{
		Photo(30, 6, 910),
		Video(32, 6, 701, false),
		Video(31, 6, 702, false),
	};
}

void TestSelectMediaPhotosExcludedByDefault() {
	// Album 5: photo 20 and video 21.
	const auto posts = std::vector<MatchedPost>{ { 20, 5 } };
	const auto candidates = std::vector<Candidate>{
		Photo(20, 5, 900),
		Video(21, 5, 600, false),
	};
	CHECK(SelectMedia(posts, candidates, false).photos.empty());
}

void TestSelectMediaPairsPhotoWithVideo() {
	const auto posts = std::vector<MatchedPost>{ { 20, 5 } };
	const auto candidates = std::vector<Candidate>{
		Photo(20, 5, 900),
		Video(21, 5, 600, false),
	};
	const auto media = SelectMedia(posts, candidates, true);
	const auto expectedVideos = std::vector<int64>{ 21 };
	const auto expectedPhotos = std::vector<PhotoPick>{ { 20, 21 } };
	CHECK(media.videos == expectedVideos);
	CHECK(media.photos == expectedPhotos);
}

void TestSelectMediaPairsPhotoWithFirstVideo() {
	const auto posts = std::vector<MatchedPost>{ { 30, 6 } };
	const auto media = SelectMedia(posts, AlbumPhotoAndTwoVideos(), true);
	const auto expectedVideos = std::vector<int64>{ 31, 32 };
	const auto expectedPhotos = std::vector<PhotoPick>{ { 30, 31 } };
	CHECK(media.videos == expectedVideos);
	CHECK(media.photos == expectedPhotos);
}

void TestSelectMediaPhotoWithoutSelectableVideo() {
	// Album 7: photo 40 and video 41, but the video is skipped (already downloaded).
	const auto posts = std::vector<MatchedPost>{ { 40, 7 } };
	const auto candidates = std::vector<Candidate>{
		Photo(40, 7, 920),
		Video(41, 7, 703, true),
	};
	const auto media = SelectMedia(posts, candidates, true);
	CHECK(media.videos.empty());
	CHECK(media.photos.empty());
}

void TestSplitPath() {
	const auto withExt = SplitPath(QStringLiteral("C:/d/Episode 12.mp4"));
	CHECK(withExt.base == QStringLiteral("C:/d/Episode 12"));
	CHECK(withExt.extension == QStringLiteral(".mp4"));

	const auto noExt = SplitPath(QStringLiteral("C:/d/name"));
	CHECK(noExt.base == QStringLiteral("C:/d/name"));
	CHECK(noExt.extension.isEmpty());

	// The only dot is in the folder name, so the file has no extension.
	const auto dotInFolder = SplitPath(QStringLiteral("C:/a.b/file"));
	CHECK(dotInFolder.base == QStringLiteral("C:/a.b/file"));
	CHECK(dotInFolder.extension.isEmpty());
}

void TestPhotoPathForVideo() {
	CHECK(PhotoPathForVideo(QStringLiteral("C:/d/Episode 12.mp4"), QStringLiteral(".jpg"))
		== QStringLiteral("C:/d/Episode 12.jpg"));
	CHECK(PhotoPathForVideo(QStringLiteral("C:/d/Episode 12 (2).mp4"), QStringLiteral(".jpg"))
		== QStringLiteral("C:/d/Episode 12 (2).jpg"));
}

void TestReserveNameBumpsSameName() {
	auto reservation = NameReservation();
	const auto none = [](const QString &) { return false; };
	CHECK(ReserveName(reservation, QStringLiteral("Episode"), QStringLiteral(".mp4"), none)
		== QStringLiteral("Episode.mp4"));
	CHECK(ReserveName(reservation, QStringLiteral("Episode"), QStringLiteral(".mp4"), none)
		== QStringLiteral("Episode (2).mp4"));
	// Same base, different extension: not bumped.
	CHECK(ReserveName(reservation, QStringLiteral("Episode"), QStringLiteral(".jpg"), none)
		== QStringLiteral("Episode.jpg"));
}

void TestReserveNameSkipsExistingFile() {
	auto reservation = NameReservation();
	const auto exists = [](const QString &name) {
		return name == QStringLiteral("Episode.mp4");
	};
	CHECK(ReserveName(reservation, QStringLiteral("Episode"), QStringLiteral(".mp4"), exists)
		== QStringLiteral("Episode (2).mp4"));
}

void TestResolveDownloadFolder() {
	const auto tmp = QStringLiteral("#tmp");
	const auto def = QStringLiteral("C:/Downloads");
	const auto temp = QStringLiteral("C:/Temp");
	// The override wins and gets a trailing '/'.
	CHECK(ResolveDownloadFolder(QStringLiteral("D:/Picked"), QStringLiteral("E:/Set"), tmp, def, temp)
		== QStringLiteral("D:/Picked/"));
	// Empty setting falls back to the default folder.
	CHECK(ResolveDownloadFolder(QString(), QString(), tmp, def, temp)
		== QStringLiteral("C:/Downloads/"));
	// The tmp marker gives the temp folder.
	CHECK(ResolveDownloadFolder(QString(), tmp, tmp, def, temp)
		== QStringLiteral("C:/Temp/"));
	// A setting that already ends with '/' is not given a second one.
	CHECK(ResolveDownloadFolder(QString(), QStringLiteral("E:/Set/"), tmp, def, temp)
		== QStringLiteral("E:/Set/"));
}

void TestProgressRatio() {
	// Nothing scanned and total unknown: ratio 0.
	auto progress = SearchProgress();
	CHECK(ProgressRatio(progress) == 0.);

	// Scanned more than the server count: ratio 1.
	progress.scanned = 5;
	progress.total = 3;
	CHECK(ProgressRatio(progress) == 1.);

	progress.scanned = 50;
	progress.total = 200;
	CHECK(ProgressRatio(progress) == 0.25);
}

void TestPostSelectionToggle() {
	auto selection = PostSelection();
	CHECK(selection.toggle({ .msgId = 7, .groupId = 3 }));
	CHECK(selection.toggle({ .msgId = 5 }));
	CHECK(selection.contains(7));
	CHECK(selection.count() == 2);

	const auto posts = selection.posts();
	CHECK(posts.size() == 2);
	CHECK(posts[0].msgId == 5 && posts[0].groupId == 0);
	CHECK(posts[1].msgId == 7 && posts[1].groupId == 3);

	CHECK(!selection.toggle({ .msgId = 7, .groupId = 3 }));
	CHECK(!selection.contains(7));
	CHECK(selection.count() == 1);
}

void TestPostSelectionUntickedStaysOut() {
	auto selection = PostSelection();
	selection.toggle({ .msgId = 9 });
	selection.toggle({ .msgId = 9 });
	CHECK(!selection.addFound({ .msgId = 9 }));
	CHECK(selection.addFound({ .msgId = 4 }));
	CHECK(!selection.contains(9));
	CHECK(selection.contains(4));

	// Ticked again by hand: found again later, no duplicate.
	CHECK(selection.toggle({ .msgId = 9 }));
	CHECK(!selection.addFound({ .msgId = 9 }));
	CHECK(selection.count() == 2);

	selection.toggle({ .msgId = 4 });
	selection.forgetUnticked();
	CHECK(selection.addFound({ .msgId = 4 }));
	CHECK(selection.contains(4));
}

void TestSearchPagingDone() {
	CHECK(SearchPagingDone(0, 0, 0));
	CHECK(SearchPagingDone(0, 50, 200));
	CHECK(!SearchPagingDone(5, 50, 200));
	CHECK(SearchPagingDone(25, 225, 225));
	CHECK(!SearchPagingDone(kSearchPerPage, 200, 200));
	CHECK(SearchPagingDone(5, 225, 200));
	CHECK(!SearchPagingDone(5, 5, 0));
}

} // namespace

int main() {
	TestAlbumIdRange();
	TestAlbumNeighborIds();
	TestChunkIds();
	TestSelectVideosSinglePost();
	TestSelectVideosNonVideoExcluded();
	TestSelectVideosAlbumCaptionOnPhoto();
	TestSelectVideosSkippedExcluded();
	TestSelectVideosDuplicatesRemoved();
	TestSelectVideosAscendingOrder();
	TestSelectMediaPhotosExcludedByDefault();
	TestSelectMediaPairsPhotoWithVideo();
	TestSelectMediaPairsPhotoWithFirstVideo();
	TestSelectMediaPhotoWithoutSelectableVideo();
	TestSplitPath();
	TestPhotoPathForVideo();
	TestReserveNameBumpsSameName();
	TestReserveNameSkipsExistingFile();
	TestResolveDownloadFolder();
	TestProgressRatio();
	TestPostSelectionToggle();
	TestPostSelectionUntickedStaysOut();
	TestSearchPagingDone();

	if (gFailed) {
		std::fprintf(stderr, "bulk download logic tests: %d check(s) failed\n", gFailed);
		return 1;
	}
	std::printf("bulk download logic tests: all checks passed\n");
	return 0;
}
