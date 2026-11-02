// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class ChannelData;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace AyuBulkDownload {
struct SearchOutcome;
} // namespace AyuBulkDownload

// The "Download videos" chat menu action: opens the progress box of the
// account's running search, or the filter box when nothing runs.
void ShowDownloadVideos(
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel);

// Filter box. Layout follows EditLinkBox (two stacked single-line fields),
// the date limits label and option checkboxes of the Export chat history
// settings.
void DownloadVideosBox(
	not_null<Ui::GenericBox*> box,
	not_null<ChannelData*> channel);

// Progress of the account's running search, modeled on the Export chat
// history progress: a label / info row with a progress line, an about
// text, and Hide / Stop buttons. Closes when the search ends.
void DownloadVideosProgressBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

// Shows the result of a finished search in the account's window: the
// download confirm, "No videos found." or the failure message.
void ShowDownloadVideosResult(
	not_null<Main::Session*> session,
	AyuBulkDownload::SearchOutcome outcome);
