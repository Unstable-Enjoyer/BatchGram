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

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

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

// Expected local-time bounds, computed with QDateTime, not hardcoded epochs.
TimeId LocalStartOfDay(QDate day) {
	return TimeId(QDateTime(day, QTime(0, 0)).toSecsSinceEpoch());
}

TimeId LocalLastSecondOfDay(QDate day) {
	return TimeId(QDateTime(day, QTime(23, 59, 59)).toSecsSinceEpoch());
}

void TestPrepareQueryTrimsAndLowercases() {
	const auto q = PrepareQuery(
		QStringLiteral("  Hello World  "),
		QString(),
		QDate(),
		QDate());
	CHECK(q.phrase == QStringLiteral("hello world"));
	CHECK(q.hashtag.isEmpty());
}

void TestPrepareQueryHashtag() {
	const auto add = [](const QString &tag) {
		return PrepareQuery(QString(), tag, QDate(), QDate()).hashtag;
	};
	CHECK(add(QStringLiteral("TagX")) == QStringLiteral("#tagx"));
	CHECK(add(QStringLiteral("  #Tag  ")) == QStringLiteral("#tag"));
	CHECK(add(QStringLiteral("#")).isEmpty());
	CHECK(add(QStringLiteral("   ")).isEmpty());
}

void TestPrepareQueryDateBounds() {
	const auto from = QDate(2024, 1, 10);
	const auto till = QDate(2024, 1, 12);
	const auto q = PrepareQuery(QString(), QString(), from, till);
	CHECK(q.minDate == LocalStartOfDay(from));
	CHECK(q.maxDate == LocalLastSecondOfDay(till));

	const auto open = PrepareQuery(QString(), QString(), QDate(), QDate());
	CHECK(open.minDate == 0);
	CHECK(open.maxDate == 0);
}

void TestSearchText() {
	auto q = Query();
	q.phrase = QStringLiteral("hello");
	q.hashtag = QStringLiteral("#tag");
	CHECK(SearchText(q) == QStringLiteral("hello #tag"));
}

void TestServerMinDate() {
	auto q = Query();
	q.minDate = 1000;
	CHECK(ServerMinDate(q) == 999);
	CHECK(ServerMinDate(Query()) == 0);
}

void TestServerMaxDate() {
	auto q = Query();
	q.maxDate = 2000;
	CHECK(ServerMaxDate(q) == 2001);
	CHECK(ServerMaxDate(Query()) == 0);
}

void TestMatchesPhraseOnly() {
	const auto q = PrepareQuery(
		QStringLiteral("hello"),
		QString(),
		QDate(),
		QDate());
	CHECK(Matches(q, QStringLiteral("say hello there"), 1000));
	CHECK(!Matches(q, QStringLiteral("bye"), 1000));
}

void TestMatchesHashtagOnly() {
	const auto q = PrepareQuery(
		QString(),
		QStringLiteral("tag"),
		QDate(),
		QDate());
	CHECK(Matches(q, QStringLiteral("a #tag b"), 1000));
	CHECK(!Matches(q, QStringLiteral("no tags here"), 1000));
}

void TestMatchesPhraseAndHashtag() {
	const auto q = PrepareQuery(
		QStringLiteral("hello"),
		QStringLiteral("#tag"),
		QDate(),
		QDate());
	CHECK(Matches(q, QStringLiteral("hello world #tag"), 1000));
	CHECK(!Matches(q, QStringLiteral("hello only"), 1000));
	CHECK(!Matches(q, QStringLiteral("#tag only"), 1000));
}

void TestMatchesEmptyQuery() {
	const auto q = PrepareQuery(QString(), QString(), QDate(), QDate());
	CHECK(Matches(q, QStringLiteral("anything"), 1000));
	CHECK(Matches(q, QString(), 1000));
}

void TestMatchesCaseInsensitive() {
	const auto q = PrepareQuery(
		QStringLiteral("HeLLo"),
		QStringLiteral("#TaG"),
		QDate(),
		QDate());
	CHECK(Matches(q, QStringLiteral("SAY HELLO #tag"), 1000));
}

void TestMatchesDateBounds() {
	const auto from = QDate(2024, 1, 10);
	const auto till = QDate(2024, 1, 12);
	const auto minDate = LocalStartOfDay(from);
	const auto maxDate = LocalLastSecondOfDay(till);
	const auto q = PrepareQuery(
		QStringLiteral("hello"),
		QString(),
		from,
		till);
	const auto text = QStringLiteral("hello");
	CHECK(Matches(q, text, minDate));
	CHECK(Matches(q, text, maxDate));
	CHECK(!Matches(q, text, minDate - 1));
	CHECK(!Matches(q, text, maxDate + 1));
}

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

} // namespace

int main() {
	TestPrepareQueryTrimsAndLowercases();
	TestPrepareQueryHashtag();
	TestPrepareQueryDateBounds();
	TestSearchText();
	TestServerMinDate();
	TestServerMaxDate();
	TestMatchesPhraseOnly();
	TestMatchesHashtagOnly();
	TestMatchesPhraseAndHashtag();
	TestMatchesEmptyQuery();
	TestMatchesCaseInsensitive();
	TestMatchesDateBounds();
	TestAlbumIdRange();
	TestAlbumNeighborIds();
	TestChunkIds();
	TestSelectVideosSinglePost();
	TestSelectVideosNonVideoExcluded();
	TestSelectVideosAlbumCaptionOnPhoto();
	TestSelectVideosSkippedExcluded();
	TestSelectVideosDuplicatesRemoved();
	TestSelectVideosAscendingOrder();

	if (gFailed) {
		std::fprintf(stderr, "bulk download logic tests: %d check(s) failed\n", gFailed);
		return 1;
	}
	std::printf("bulk download logic tests: all checks passed\n");
	return 0;
}
