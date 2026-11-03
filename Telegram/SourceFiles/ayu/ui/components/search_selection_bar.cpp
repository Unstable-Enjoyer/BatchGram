// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/search_selection_bar.h"

#include "ayu/features/bulk_download/bulk_download.h"
#include "ayu/ui/boxes/download_videos_box.h"
#include "base/unique_qptr.h"
#include "lang/lang_keys.h"
#include "lang/lang_numbers_animation.h"
#include "ui/effects/numbers_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"

#include "styles/style_ayu_styles.h"
#include "styles/style_info.h"
#include "styles/style_media_player.h"

namespace AyuBulkDownload {
namespace {

[[nodiscard]] Ui::StringWithNumbers SelectedText(int count) {
	return tr::lng_media_selected_message(
		tr::now,
		lt_count,
		count,
		Ui::StringWithNumbers::FromString);
}

} // namespace

void ShowSelectionBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<SearchSelection*> selection) {
	const auto owner = selection->lifetime().make_state<
		base::unique_qptr<Ui::RpWidget>>(
			base::make_unique_q<Ui::RpWidget>(parent.get()));
	const auto bar = owner->get();
	const auto &st = st::infoTopBar;

	// Same controls as Info::TopBar::createSelectionControls().
	const auto cancel = Ui::CreateChild<Ui::IconButton>(
		bar,
		st.mediaCancel);
	cancel->setAccessibleName(tr::lng_context_clear_selection(tr::now));
	const auto label = Ui::CreateChild<Ui::LabelWithNumbers>(
		bar,
		st.title,
		st.titlePosition.y(),
		SelectedText(0));
	label->resize(0, st.height);
	const auto selectAll = Ui::CreateChild<Ui::IconButton>(
		bar,
		st::ayuSearchSelectAll);
	selectAll->setAccessibleName(tr::lng_mac_menu_select_all(tr::now));
	const auto download = Ui::CreateChild<Ui::IconButton>(
		bar,
		st::ayuSearchSelectDownload);
	download->setAccessibleName(tr::lng_media_download(tr::now));

	// Same progress line as the Export top bar.
	const auto progress = Ui::CreateChild<Ui::FilledSlider>(
		bar,
		st::mediaPlayerPlayback);
	progress->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto loading = bar->lifetime().make_state<bool>(false);
	selection->stateValue(
	) | rpl::on_next([=](const SearchSelection::State &state) {
		*loading = state.loading;
		label->setValue(SelectedText(state.count));
		download->setVisible(state.count > 0);
		progress->setVisible(state.loading || state.preparing);
		progress->setValue(state.progress);
	}, bar->lifetime());

	rpl::combine(
		parent->sizeValue(),
		label->naturalWidthValue()
	) | rpl::on_next([=](QSize size, int) {
		const auto width = size.width();
		bar->setGeometry(QRect(QPoint(), size));
		auto right = st.mediaActionsSkip;
		download->moveToRight(right, 0, width);
		right += download->width();
		selectAll->moveToRight(right, 0, width);
		right += selectAll->width();
		cancel->moveToLeft(0, 0, width);
		const auto left = cancel->width();
		label->resizeToNaturalWidth(std::max(width - left - right, 0));
		label->moveToLeft(left, 0, width);
		const auto line = st::mediaPlayerPlayback.fullWidth;
		progress->setGeometry(0, size.height() - line, width, line);
	}, bar->lifetime());

	bar->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(bar).fillRect(clip, st.bg);
	}, bar->lifetime());

	cancel->setClickedCallback([=] {
		selection->close();
	});
	selectAll->setClickedCallback([=] {
		if (*loading) {
			selection->stop();
		} else {
			selection->selectAll();
		}
	});
	download->setClickedCallback([=] {
		const auto started = crl::guard(selection, [=] {
			selection->close();
		});
		selection->prepareDownload(crl::guard(bar, [=](DownloadSet set) {
			ShowDownloadConfirm(controller, std::move(set), started);
		}));
	});

	bar->show();
	bar->raise();
}

} // namespace AyuBulkDownload
