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
#include "export/view/export_view_content.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/format_values.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/qt_object_factory.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>

#include "styles/style_boxes.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

using namespace AyuBulkDownload;

namespace {

struct FilterState {
	rpl::variable<QDate> from;
	rpl::variable<QDate> till;
};

// Same as Export::View::SettingsWidget::editDateLimit, with dates.
void EditDateLimit(
		not_null<Ui::GenericBox*> box,
		QDate current,
		QDate min,
		QDate max,
		rpl::producer<QString> resetLabel,
		Fn<void(QDate)> done) {
	const auto highlighted = current.isValid()
		? current
		: max.isValid()
		? max
		: min.isValid()
		? min
		: QDate::currentDate();
	const auto shared = std::make_shared<base::weak_qptr<Ui::CalendarBox>>();
	const auto finalize = [=](not_null<Ui::CalendarBox*> calendar) {
		calendar->addLeftButton(
			std::move(resetLabel),
			crl::guard(box, [=] {
				done(QDate());
				if (const auto weak = shared->get()) {
					weak->closeBox();
				}
			}));
	};
	const auto callback = crl::guard(box, [=](
			const QDate &date,
			Fn<void()> close) {
		done(date);
		close();
	});
	auto calendar = Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = highlighted,
		.highlighted = highlighted,
		.callback = callback,
		.finalize = finalize,
		.st = st::exportCalendarSizes,
		.minDate = (min.isValid()
			? min
			: QDate(2013, 8, 1)), // Telegram was launched in August 2013 :)
		.maxDate = (max.isValid() ? max : QDate::currentDate()),
	});
	*shared = base::make_weak(calendar.data());
	box->uiShow()->showBox(std::move(calendar));
}

[[nodiscard]] rpl::producer<TextWithEntities> DateLink(
		rpl::producer<QDate> value,
		Fn<rpl::producer<QString>()> empty,
		const QString &url) {
	return std::move(
		value
	) | rpl::map([=](QDate date) {
		return (date.isValid()
			? rpl::single(langDayOfMonthFull(date))
			: empty()
		) | rpl::map(tr::url(url));
	}) | rpl::flatten_latest();
}

[[nodiscard]] QString CountVideos(int count) {
	return tr::ayu_DownloadVideosCount(tr::now, lt_count_decimal, count);
}

[[nodiscard]] QString CountPhotos(int count) {
	return tr::ayu_DownloadPhotosCount(tr::now, lt_count_decimal, count);
}

[[nodiscard]] QString ProgressInfo(const SearchProgress &progress) {
	if (!progress.scanned && !progress.total) {
		return QString();
	}
	return tr::ayu_DownloadVideosProgress(
		tr::now,
		lt_count_decimal,
		ProgressTotal(progress),
		lt_scanned,
		Lang::FormatCountDecimal(progress.scanned),
		lt_videos,
		CountVideos(progress.videos));
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

// Same confirmation as Export::View::PanelController::stopWithConfirmation.
void StopWithConfirmation(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session) {
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::ayu_DownloadVideosSureStop(),
		.confirmed = [=](Fn<void()> &&close) {
			if (const auto search = VideoSearch::Find(session)) {
				search->stop();
			}
			close();
		},
		.confirmText = tr::lng_export_stop(),
		.confirmStyle = &st::attentionBoxButton,
	}));
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

void ShowDownloadVideos(
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel) {
	const auto session = &channel->session();
	if (VideoSearch::Find(session)) {
		controller->show(Box(DownloadVideosProgressBox, session));
	} else {
		controller->show(Box(DownloadVideosBox, channel));
	}
}

void DownloadVideosBox(
		not_null<Ui::GenericBox*> box,
		not_null<ChannelData*> channel) {
	const auto state = box->lifetime().make_state<FilterState>();
	const auto content = box->verticalLayout();

	box->setTitle(tr::ayu_DownloadVideosTitle());
	box->setWidth(st::boxWidth);

	const auto phrase = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			Ui::InputField::Mode::SingleLine,
			tr::ayu_DownloadVideosPhrase(),
			QString()),
		st::markdownLinkFieldPadding);
	const auto hashtag = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			Ui::InputField::Mode::SingleLine,
			tr::ayu_DownloadVideosHashtag(),
			QString()),
		st::markdownLinkFieldPadding);

	auto datesText = tr::lng_export_limits(
		lt_from,
		DateLink(
			state->from.value(),
			[] { return tr::lng_export_beginning(); },
			u"internal:edit_from"_q),
		lt_till,
		DateLink(
			state->till.value(),
			[] { return tr::lng_export_end(); },
			u"internal:edit_till"_q),
		tr::marked
	) | rpl::after_next([=] {
		content->resizeToWidth(content->width());
	});
	const auto label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			std::move(datesText),
			st::boxLabel),
		st::exportLimitsPadding);
	label->overrideLinkClickHandler([=](const QString &url) {
		if (url == u"internal:edit_from"_q) {
			EditDateLimit(
				box,
				state->from.current(),
				QDate(),
				state->till.current(),
				tr::lng_export_from_beginning(),
				[=](QDate date) { state->from = date; });
		} else if (url == u"internal:edit_till"_q) {
			EditDateLimit(
				box,
				state->till.current(),
				state->from.current(),
				QDate(),
				tr::lng_export_till_end(),
				[=](QDate date) { state->till = date; });
		}
	});

	// Same as the options of the Export chat history settings.
	const auto includePhotos = content->add(
		object_ptr<Ui::Checkbox>(
			content,
			tr::ayu_DownloadVideosIncludePhotos(tr::now),
			false,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);

	const auto submit = [=] {
		const auto session = &channel->session();
		// One search per account: if one already runs (started from
		// another window or channel), show its progress instead.
		if (!VideoSearch::Find(session)) {
			VideoSearch::Start(
				channel,
				PrepareQuery(
					phrase->getLastText(),
					hashtag->getLastText(),
					state->from.current(),
					state->till.current()),
				includePhotos->checked());
		}
		box->uiShow()->showBox(
			Box(DownloadVideosProgressBox, session),
			Ui::LayerOption::CloseOther);
	};

	phrase->submits(
	) | rpl::on_next([=] {
		hashtag->setFocusFast();
	}, phrase->lifetime());
	hashtag->submits(
	) | rpl::on_next(submit, hashtag->lifetime());

	box->setFocusCallback([=] {
		phrase->setFocusFast();
	});

	box->addButton(tr::ayu_DownloadVideosSearch(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void DownloadVideosProgressBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(tr::ayu_DownloadVideosTitle());
	box->setWidth(st::boxWidth);

	const auto search = VideoSearch::Find(session);
	if (!search) {
		// Not from inside the box setup: close once it is shown.
		crl::on_main(box, [=] { box->closeBox(); });
		return;
	}
	// The search lives in the account: hiding the box keeps it running,
	// and the box closes when the search finishes or is stopped.
	search->lifetime().add(crl::guard(box, [=] {
		box->closeBox();
	}));

	// Same layout as Export::View::ProgressWidget::Row: a bold label on
	// the left, the info on the right, the progress line below.
	const auto row = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::exportProgressRowPadding);
	row->resize(row->width(), st::exportProgressRowHeight);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		search->channel()->name(),
		st::exportProgressLabel);
	const auto info = Ui::CreateChild<Ui::FlatLabel>(
		row,
		search->progressValue() | rpl::map(ProgressInfo),
		st::exportProgressInfoLabel);
	struct Line {
		float64 value = 0.;
		Ui::Animations::Simple progress;
	};
	const auto line = row->lifetime().make_state<Line>();
	// Same as ProgressWidget::Row::setInstanceProgress.
	search->progressValue(
	) | rpl::on_next([=](const SearchProgress &progress) {
		const auto value = ProgressRatio(progress);
		if (line->value < value) {
			line->progress.start(
				[=] { row->update(); },
				line->value,
				value,
				st::exportProgressDuration,
				anim::sineInOut);
		} else if (line->value > value) {
			line->progress.stop();
		}
		line->value = value;
		row->update();
	}, row->lifetime());
	// Same as ProgressWidget::Row::paintEvent / paintInstance.
	row->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(row);
		const auto width = row->width();
		const auto thickness = st::exportProgressWidth;
		const auto top = row->height() - thickness;
		p.fillRect(0, top, width, thickness, st::shadowFg);
		const auto progress = line->progress.value(line->value);
		const auto till = int(base::SafeRound(progress * width));
		if (till > 0) {
			p.fillRect(0, top, till, thickness, st::exportProgressFg);
		}
		if (till < width) {
			p.fillRect(till, top, width - till, thickness, st::exportProgressBg);
		}
	}, row->lifetime());
	rpl::combine(
		row->widthValue(),
		info->naturalWidthValue()
	) | rpl::on_next([=](int width, int) {
		info->resizeToNaturalWidth(width);
		label->resizeToWidth(std::max(width - info->width(), 0));
		info->moveToRight(0, 0, width);
		label->moveToLeft(0, 0, width);
	}, row->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_DownloadVideosProgressAbout(),
			st::exportAboutLabel),
		st::exportAboutPadding);
	box->addSkip(st::boxPadding.bottom());

	box->addButton(tr::ayu_DownloadVideosHide(), [=] { box->closeBox(); });
	box->addButton(
		tr::lng_export_stop(),
		[=] { StopWithConfirmation(box->uiShow(), session); },
		st::attentionBoxButton);
}

rpl::producer<std::optional<Export::View::Content>>
DownloadVideosTopBarContent(not_null<Main::Session*> session) {
	using Result = std::optional<Export::View::Content>;
	return rpl::single(
		rpl::empty
	) | rpl::then(
		SearchChanges(session)
	) | rpl::map([=]() -> rpl::producer<Result> {
		const auto search = VideoSearch::Find(session);
		if (!search) {
			return rpl::single(Result());
		}
		const auto name = search->channel()->name();
		return search->progressValue(
		) | rpl::map([=](const SearchProgress &progress) {
			auto result = Export::View::Content();
			result.title = tr::ayu_DownloadVideosTopBarTitle(tr::now);
			result.closable = true;
			result.rows.push_back({
				.id = u"ayu_bulk_download"_q,
				.label = name,
				.info = ProgressInfo(progress),
				.progress = ProgressRatio(progress),
			});
			return Result(std::move(result));
		});
	}) | rpl::flatten_latest();
}

void DownloadVideosTopBarClicked(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	if (VideoSearch::Find(session)) {
		controller->show(Box(DownloadVideosProgressBox, session));
	}
}

void DownloadVideosTopBarClosed(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	if (VideoSearch::Find(session)) {
		StopWithConfirmation(controller->uiShow(), session);
	}
}

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
