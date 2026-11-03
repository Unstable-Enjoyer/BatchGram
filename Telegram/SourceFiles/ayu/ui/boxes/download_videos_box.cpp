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
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>

#include "styles/style_boxes.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

using namespace AyuBulkDownload;

namespace {

[[nodiscard]] QString CountVideos(int count) {
	return tr::ayu_DownloadVideosCount(tr::now, lt_count_decimal, count);
}

[[nodiscard]] QString CountPhotos(int count) {
	return tr::ayu_DownloadPhotosCount(tr::now, lt_count_decimal, count);
}

[[nodiscard]] QString ConfirmText(
		const DownloadSet &set,
		bool includePhotos) {
	const auto photos = includePhotos ? int(set.photos.size()) : 0;
	const auto items = !photos
		? CountVideos(int(set.videos.size()))
		: tr::ayu_DownloadVideosAndPhotos(
			tr::now,
			lt_videos,
			CountVideos(int(set.videos.size())),
			lt_photos,
			CountPhotos(photos));
	const auto size = set.videosSize + (photos ? set.photosSize : 0);
	const auto question = tr::ayu_DownloadVideosConfirm(
		tr::now,
		lt_items,
		items,
		lt_size,
		Ui::FormatSizeText(size));
	return set.partial
		? (tr::ayu_DownloadVideosStoppedEarly(tr::now) + u"\n\n"_q + question)
		: question;
}

// Modeled on Ui::ConfirmBox (same label style and padding), with an
// extra line for the destination folder like the location label of the
// Export chat history settings, and a link to change it for this batch.
void ResultBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		DownloadSet set,
		Fn<void()> started) {
	const auto folder = box->lifetime().make_state<QString>(
		DefaultDownloadFolder(session));
	const auto picked = box->lifetime().make_state<QString>();
	const auto includePhotos = box->lifetime().make_state<
		rpl::variable<bool>>(false);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			includePhotos->value() | rpl::map([=](bool photos) {
				return ConfirmText(set, photos);
			}),
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
	box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_DownloadVideosIncludePhotos(tr::now),
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			0,
			st::boxPadding.right(),
			st::boxPadding.bottom())
	)->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		*includePhotos = checked;
	}, box->lifetime());

	const auto confirm = box->addButton(tr::lng_media_download(), [=] {
		// The picked folder applies to this batch only.
		StartDownloads(session, set, includePhotos->current(), *picked);
		box->closeBox();
		started();
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
		DownloadSet set,
		Fn<void()> started) {
	if (set.videos.empty()) {
		controller->show(Ui::MakeInformBox(set.partial
			? (tr::ayu_DownloadVideosStoppedEarly(tr::now)
				+ u"\n\n"_q
				+ tr::ayu_DownloadVideosNotFound(tr::now))
			: tr::ayu_DownloadVideosNotFound(tr::now)));
	} else {
		controller->show(Box(
			ResultBox,
			&controller->session(),
			std::move(set),
			std::move(started)));
	}
}
