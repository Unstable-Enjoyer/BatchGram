// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/download_videos_box.h"

#include "ayu/features/bulk_download/bulk_download.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace {

struct State {
	rpl::variable<QDate> from;
	rpl::variable<QDate> till;
	std::unique_ptr<AyuBulkDownload::VideoSearch> search;
	bool searching = false;
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

} // namespace

void DownloadVideosBox(
		not_null<Ui::GenericBox*> box,
		not_null<ChannelData*> channel) {
	const auto state = box->lifetime().make_state<State>();
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
	box->addSkip(st::boxPadding.bottom());
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

	const auto found = [=](AyuBulkDownload::SearchResult result) {
		state->searching = false;
		box->showLoading(false);
		const auto ids = std::move(result.ids);
		if (ids.empty()) {
			box->uiShow()->showBox(Ui::MakeInformBox(result.restricted
				? tr::ayu_DownloadVideosRestricted()
				: tr::ayu_DownloadVideosNotFound()));
			return;
		}
		const auto weak = base::make_weak(box);
		const auto session = &channel->session();
		box->uiShow()->showBox(Ui::MakeConfirmBox({
			.text = tr::ayu_DownloadVideosConfirm(
				tr::now,
				lt_count,
				int(ids.size())),
			.confirmed = [=](Fn<void()> &&close) {
				AyuBulkDownload::StartDownloads(session, ids);
				close();
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			},
			.confirmText = tr::lng_media_download(),
			.cancelText = tr::lng_cancel(),
		}));
	};
	const auto failed = [=] {
		state->searching = false;
		box->showLoading(false);
		box->uiShow()->showToast(tr::ayu_DownloadVideosFailed(tr::now));
	};

	const auto submit = [=] {
		if (state->searching) {
			return;
		}
		state->searching = true;
		box->showLoading(true);
		state->search = std::make_unique<AyuBulkDownload::VideoSearch>(
			channel,
			AyuBulkDownload::PrepareQuery(
				phrase->getLastText(),
				hashtag->getLastText(),
				state->from.current(),
				state->till.current()),
			crl::guard(box, found),
			crl::guard(box, failed));
		state->search->start();
	};

	phrase->submits(
	) | rpl::on_next([=] {
		hashtag->setFocusFast();
	}, phrase->lifetime());
	hashtag->submits(
	) | rpl::on_next(submit, hashtag->lifetime());

	// The box stays alive during its hide animation, so make sure late
	// results don't open anything after it was closed.
	box->boxClosing() | rpl::on_next([=] {
		state->search = nullptr;
		state->searching = false;
	}, box->lifetime());

	box->setFocusCallback([=] {
		phrase->setFocusFast();
	});

	box->addButton(tr::ayu_DownloadVideosSearch(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}
