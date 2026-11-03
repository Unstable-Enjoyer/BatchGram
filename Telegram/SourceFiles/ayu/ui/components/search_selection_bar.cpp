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
#include "ui/wrap/fade_wrap.h"
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
		base::unique_qptr<Ui::FadeWrap<Ui::RpWidget>>>(
			base::make_unique_q<Ui::FadeWrap<Ui::RpWidget>>(
				parent.get(),
				object_ptr<Ui::RpWidget>(parent.get())));
	const auto outer = owner->get();
	outer->setDuration(st::infoTopBarDuration);
	const auto bar = outer->entity();
	const auto &st = st::infoTopBar;

	// Same controls as Info::TopBar::createSelectionControls().
	const auto wrap = [&](auto created) {
		created->setDuration(st::infoTopBarDuration);
		return created;
	};
	const auto cancel = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		bar,
		object_ptr<Ui::IconButton>(bar, st.mediaCancel),
		st::infoTopBarScale));
	cancel->entity()->setAccessibleName(
		tr::lng_context_clear_selection(tr::now));
	const auto label = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::LabelWithNumbers>>(
			bar,
			object_ptr<Ui::LabelWithNumbers>(
				bar,
				st.title,
				st.titlePosition.y(),
				SelectedText(0)),
			st::infoTopBarScale));
	label->entity()->resize(0, st.height);
	const auto selectAll = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
			bar,
			object_ptr<Ui::IconButton>(bar, st::ayuSearchSelectAll),
			st::infoTopBarScale));
	selectAll->entity()->setAccessibleName(
		tr::lng_mac_menu_select_all(tr::now));
	const auto download = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
			bar,
			object_ptr<Ui::IconButton>(bar, st::ayuSearchSelectDownload),
			st::infoTopBarScale));
	download->entity()->setAccessibleName(tr::lng_media_download(tr::now));

	// Same progress line as the Export top bar.
	const auto progress = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::FilledSlider>>(
			bar,
			object_ptr<Ui::FilledSlider>(bar, st::mediaPlayerPlayback)));
	progress->setAttribute(Qt::WA_TransparentForMouseEvents);
	progress->entity()->setAttribute(Qt::WA_TransparentForMouseEvents);

	struct Flags {
		bool preparing = false;
		bool shown = false;
	};
	const auto flags = bar->lifetime().make_state<Flags>();
	selection->stateValue(
	) | rpl::on_next([=](const SearchSelection::State &state) {
		const auto animated = flags->shown
			? anim::type::normal
			: anim::type::instant;
		flags->preparing = state.preparing;
		label->entity()->setValue(SelectedText(state.count));
		download->toggle(state.count > 0, animated);
		progress->toggle(state.loading || state.preparing, animated);
		progress->entity()->setValue(state.progress);
		selectAll->toggle(
			!state.loading && !state.preparing,
			animated);
	}, bar->lifetime());

	rpl::combine(
		parent->sizeValue(),
		label->entity()->naturalWidthValue()
	) | rpl::on_next([=](QSize size, int) {
		const auto width = size.width();
		bar->resize(size);
		outer->moveToLeft(0, 0);
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
		progress->entity()->resize(width, line);
		progress->moveToLeft(0, size.height() - line, width);
	}, bar->lifetime());

	bar->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(bar).fillRect(clip, st.bg);
	}, bar->lifetime());

	cancel->entity()->setClickedCallback([=] {
		selection->close();
	});
	selectAll->entity()->setClickedCallback([=] {
		selection->selectAll();
	});
	download->entity()->setClickedCallback([=] {
		if (flags->preparing) {
			return;
		}
		const auto started = crl::guard(selection, [=] {
			selection->close();
		});
		selection->prepareDownload(crl::guard(bar, [=](DownloadSet set) {
			ShowDownloadConfirm(controller, std::move(set), started);
		}));
	});

	// Explicit exits fade out, the owner destroys the bar after the fade.
	selection->closeRequests(
	) | rpl::on_next([=] {
		outer->hide(anim::type::normal);
	}, bar->lifetime());

	flags->shown = true;
	outer->raise();
	outer->hide(anim::type::instant);
	outer->show(anim::type::normal);
}

} // namespace AyuBulkDownload
