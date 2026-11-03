// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/download_videos_box.h"

#include "ayu/features/bulk_download/bulk_download.h"
#include "core/file_utilities.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
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

[[nodiscard]] QString ConfirmText(const SearchOutcome &outcome) {
	const auto items = outcome.photos.empty()
		? CountVideos(int(outcome.videos.size()))
		: tr::ayu_DownloadVideosAndPhotos(
			tr::now,
			lt_videos,
			CountVideos(int(outcome.videos.size())),
			lt_photos,
			CountPhotos(int(outcome.photos.size())));
	const auto question = tr::ayu_DownloadVideosConfirm(
		tr::now,
		lt_items,
		items,
		lt_size,
		Ui::FormatSizeText(outcome.size));
	return outcome.partial
		? (tr::ayu_DownloadVideosStoppedEarly(tr::now) + u"\n\n"_q + question)
		: question;
}

// Modeled on Ui::ConfirmBox (same label style and padding), with an
// extra line for the destination folder like the location label of the
// Export chat history settings, and a link to change it for this batch.
void ResultBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		SearchOutcome outcome) {
	const auto folder = box->lifetime().make_state<QString>(
		DefaultDownloadFolder(session));
	const auto picked = box->lifetime().make_state<QString>();

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			ConfirmText(outcome),
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

	const auto confirm = box->addButton(tr::lng_media_download(), [=] {
		// The picked folder applies to this batch only.
		StartDownloads(session, outcome, *picked);
		box->closeBox();
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

void ShowDownloadVideosResult(
		not_null<Main::Session*> session,
		SearchOutcome outcome) {
	const auto resolved = session->tryResolveWindow(outcome.channel);
	if (!resolved) {
		return;
	}
	const auto window = &resolved->window();
	if (outcome.failed) {
		window->show(Ui::MakeInformBox(tr::ayu_DownloadVideosFailed()));
	} else if (outcome.videos.empty()) {
		window->show(Ui::MakeInformBox(outcome.partial
			? (tr::ayu_DownloadVideosStoppedEarly(tr::now)
				+ u"\n\n"_q
				+ tr::ayu_DownloadVideosNotFound(tr::now))
			: tr::ayu_DownloadVideosNotFound(tr::now)));
	} else {
		window->show(Box(ResultBox, session, std::move(outcome)));
	}
}
