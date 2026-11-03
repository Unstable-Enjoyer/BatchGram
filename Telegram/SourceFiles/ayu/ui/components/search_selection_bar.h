// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace AyuBulkDownload {

class SearchSelection;

// Covers `parent` (the dialogs search controls) while found posts are
// selected, laid out like the selection mode of the shared media top bar.
// The bar lives as long as the selection.
void ShowSelectionBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<SearchSelection*> selection);

} // namespace AyuBulkDownload
