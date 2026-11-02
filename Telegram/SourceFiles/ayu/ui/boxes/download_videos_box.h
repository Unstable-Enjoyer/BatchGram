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

// Filter box of the "Download videos" chat menu action.
// Layout follows EditLinkBox (two stacked single-line fields) and the
// date limits label of the Export chat history settings.
void DownloadVideosBox(
	not_null<Ui::GenericBox*> box,
	not_null<ChannelData*> channel);
