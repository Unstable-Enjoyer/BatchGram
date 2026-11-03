// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <optional>

namespace Window {
class SessionController;
} // namespace Window

// The download confirm of the selected posts: Include photos and the
// folder. `posts` is the number of selected posts, when it is final.
// `confirmed` gets the checkbox and the folder picked for this batch only
// (empty for the default one).
void ShowDownloadConfirm(
	not_null<Window::SessionController*> controller,
	std::optional<int> posts,
	Fn<void(bool includePhotos, QString folder)> confirmed);
