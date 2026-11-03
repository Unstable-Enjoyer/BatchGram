// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace AyuBulkDownload {
struct SearchOutcome;
} // namespace AyuBulkDownload

// Shows the result of a finished search in the account's window: the
// download confirm, "No videos found." or the failure message.
void ShowDownloadVideosResult(
	not_null<Main::Session*> session,
	AyuBulkDownload::SearchOutcome outcome);
