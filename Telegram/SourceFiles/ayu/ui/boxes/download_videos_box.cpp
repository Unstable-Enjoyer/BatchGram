// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/download_videos_box.h"

#include "ayu/features/bulk_download/bulk_download.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>

#include "styles/style_boxes.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace {

// Modeled on Ui::ConfirmBox (same label style and padding), with an
// extra line for the destination folder like the location label of the
// Export chat history settings, and a link to change it for this batch.
void DownloadConfirmBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::optional<int> posts,
		Fn<void(bool includePhotos, QString folder)> confirmed) {
	const auto folder = box->lifetime().make_state<QString>(
		AyuBulkDownload::DefaultDownloadFolder(session));
	const auto picked = box->lifetime().make_state<QString>();

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(posts
				? tr::ayu_DownloadPostsConfirm(
					tr::now,
					lt_count_decimal,
					*posts)
				: tr::ayu_DownloadSelectedConfirm(tr::now)),
			st::boxLabel),
		st::boxPadding);

	const auto location = box->addRow(
		object_ptr<Ui::FlatLabel>(box, st::exportLocationLabel),
		style::margins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	const auto refresh = [=] {
		const auto change = tr::ayu_DownloadVideosChangeFolder(tr::now);
		const auto prefix = tr::ayu_DownloadVideosFolder(
			tr::now,
			lt_path,
			QString());
		const auto &font = st::exportLocationLabel.style.font;
		// Elide the path natively, as DownloadPathBox::setPathText does.
		const auto available = st::boxWidth
			- st::boxPadding.left()
			- st::boxPadding.right()
			- font->width(prefix)
			- font->spacew
			- font->width(change);
		const auto path = font->elided(
			QDir::toNativeSeparators(*folder),
			std::max(available, 0));
		location->setMarkedText(tr::ayu_DownloadVideosFolder(
			tr::now,
			lt_path,
			TextWithEntities{ path },
			tr::marked
		).append(' ').append(tr::link(change, u"internal:change"_q)));
	};
	refresh();
	location->overrideLinkClickHandler([=](const QString &url) {
		const auto initial = folder->endsWith('/')
			? folder->left(folder->size() - 1)
			: *folder;
		const auto handleFolder = [=](const QString &result) {
			if (result.isEmpty()) {
				return;
			}
			*picked = result.endsWith('/') ? result : (result + '/');
			*folder = *picked;
			refresh();
		};
		FileDialog::GetFolder(
			box.get(),
			tr::lng_download_path_choose(tr::now),
			initial,
			crl::guard(box, handleFolder));
	});

	// Same as the options of the Export chat history settings.
	const auto includePhotos = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_DownloadVideosIncludePhotos(tr::now),
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom()));

	const auto confirm = box->addButton(tr::lng_media_download(), [=] {
		// The picked folder applies to this batch only.
		const auto photos = includePhotos->checked();
		const auto folder = *picked;
		box->closeBox();
		confirmed(photos, folder);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	// Enter / Return confirms, as in Ui::ConfirmBox.
	box->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if ((e->type() != QEvent::KeyPress) || !confirm) {
			return;
		}
		const auto k = static_cast<QKeyEvent*>(e.get());
		if (k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) {
			confirm->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}
	}, box->lifetime());
}

} // namespace

void ShowDownloadConfirm(
		not_null<Window::SessionController*> controller,
		std::optional<int> posts,
		Fn<void(bool includePhotos, QString folder)> confirmed) {
	controller->show(Box(
		DownloadConfirmBox,
		&controller->session(),
		posts,
		std::move(confirmed)));
}
