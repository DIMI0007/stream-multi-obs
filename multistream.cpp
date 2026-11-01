#include "config-utils.hpp"
#include "multistream.hpp"
#include "obs-module.h"
#include "version.h"
#include <obs-frontend-api.h>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVector>
#include <QVBoxLayout>
#include <util/config-file.h>
#include <util/platform.h>

extern "C" {
#include "file-updater.h"
}

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("DMITRII; based on Aitum Multistream");
OBS_MODULE_USE_DEFAULT_LOCALE("stream-multi", "en-US")

static MultistreamDock *multistream_dock = nullptr;

update_info_t *version_update_info = nullptr;

bool version_info_downloaded(void *param, struct file_download_data *file)
{
	UNUSED_PARAMETER(param);
	if (!file || !file->buffer.num) {
		return true;
	}
	if (multistream_dock) {
		QMetaObject::invokeMethod(multistream_dock, "ApiInfo",
					  Q_ARG(QString, QString::fromUtf8((const char *)file->buffer.array)));
	}

	if (version_update_info) {
		update_info_destroy(version_update_info);
		version_update_info = nullptr;
	}
	return true;
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[OZK Multi] loaded version %s (based on Aitum Multistream)", PROJECT_VERSION);

	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	multistream_dock = new MultistreamDock(main_window);
	obs_frontend_add_dock_by_id("StreamMultiDock", obs_module_text("AitumMultistream"), multistream_dock);

	return true;
}

void obs_module_post_load()
{
	if (multistream_dock) {
		multistream_dock->LoadWebsocket();
	}
}

void obs_module_unload()
{
	if (version_update_info) {
		update_info_destroy(version_update_info);
		version_update_info = nullptr;
	}
	if (multistream_dock) {
		delete multistream_dock;
	}
}

const char *obs_module_name(void)
{
	return obs_module_text("AitumMultistream");
}

void RemoveWidget(QWidget *widget);

void RemoveLayoutItem(QLayoutItem *item)
{
	if (!item) {
		return;
	}
	RemoveWidget(item->widget());
	if (item->layout()) {
		while (QLayoutItem *item2 = item->layout()->takeAt(0)) {
			RemoveLayoutItem(item2);
		}
	}
	delete item;
}

void RemoveWidget(QWidget *widget)
{
	if (!widget) {
		return;
	}
	if (widget->layout()) {
		auto l = widget->layout();
		QLayoutItem *item;
		while (l->count() > 0 && (item = l->takeAt(0))) {
			RemoveLayoutItem(item);
		}
		delete l;
	}
	delete widget;
}

// Output button styling
void MultistreamDock::outputButtonStyle(QPushButton *button)
{
	button->setMinimumHeight(25);
	button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	button->setIcon(QIcon());
	const auto state = button->property("outputState").toString();
	const auto key = state == "connecting" ? "ConnectingOutputButton" :
		state == "stopping" ? "StoppingOutputButton" :
		state == "error" ? "RetryOutputButton" :
		button->isChecked() ? "StopOutputButton" : "StartOutputButton";
	button->setText(QString::fromUtf8(obs_module_text(key)));
	auto tooltip = button->property("outputName").toString() + QStringLiteral(" · ") + button->text();
	if (state == "error" && !button->property("outputError").toString().isEmpty())
		tooltip += QStringLiteral("\n") + button->property("outputError").toString();
	button->setToolTip(tooltip);

	button->setStyleSheet(state == "error"
		? "QPushButton {padding:2px 4px; color:#ff9ba9; background:#35202a; border:1px solid #9c4257; border-radius:5px;} QPushButton:hover {background:#482733;}"
		: state == "connecting" || state == "stopping"
		? "QPushButton {padding:2px 4px; color:#ffe184; background:#322d1c; border:1px solid #806c32; border-radius:5px;}"
		: button->isChecked()
		? "QPushButton {padding:2px 4px; color:#40e6b6; background:#12352f; border:1px solid #1cb98e; border-radius:5px;} QPushButton:hover {background:#19483e;}"
		: "QPushButton {padding:2px 4px; color:#d7e5f2; background:#172536; border:1px solid #344c63; border-radius:5px;} QPushButton:hover {background:#21364b; border-color:#4a7188;}");
}

// Common styling things here
auto canvasGroupStyle = QString("QGroupBox {background:#0b1521; border:1px solid #273c51; border-radius:7px; padding:5px;}");
auto canvasGroupHeaderStyle = QString("QLabel {color:#e8f2fb; font-weight:600; padding:2px 0;}");
auto outputTitleStyle = QString("QLabel {color:#e6f0fa; font-weight:600;}");
auto outputGroupStyle = QString("QGroupBox {background:#101d2c; border:1px solid #294158; border-radius:7px; padding:2px;}");

auto outputPlatformIconSize = 32;

// For showing warning for no vertical integration
void showVerticalWarning(QVBoxLayout *verticalLayout)
{
	auto verticalWarning = new QWidget;
	verticalWarning->setContentsMargins(0, 0, 0, 0);

	auto verticalWarningLayout = new QVBoxLayout;
	verticalWarningLayout->setContentsMargins(0, 0, 0, 0);

	auto label = new QLabel(QString::fromUtf8(obs_module_text("NoVerticalWarning")));
	label->setStyleSheet(QString("padding: 0px;"));
	label->setWordWrap(true);
	label->setTextFormat(Qt::RichText);
	label->setOpenExternalLinks(true);
	verticalWarningLayout->addWidget(label);
	verticalWarning->setLayout(verticalWarningLayout);

	verticalLayout->addWidget(verticalWarning);
}

config_t *get_user_config(void)
{
	return obs_frontend_get_user_config();
}

MultistreamDock::MultistreamDock(QWidget *parent) : QFrame(parent)
{
	// Main layout
	mainLayout = new QVBoxLayout;
	mainLayout->setContentsMargins(0, 0, 0, 0);
	setLayout(mainLayout);
	setStyleSheet("QFrame {background:#080f19; color:#d7e5f2;} QScrollArea {background:#080f19; border:none;} QScrollArea > QWidget > QWidget {background:#080f19;}");
	auto header = new QHBoxLayout;
	header->setContentsMargins(8, 8, 8, 5);
	header->setSpacing(7);
	auto brand = new QLabel;
	brand->setPixmap(QIcon(":/aitum/media/stream-multi.svg").pixmap(28, 28));
	header->addWidget(brand);
	auto title = new QLabel(QStringLiteral("<span style='color:#57dfd1'>OZK</span> Multi"));
	title->setTextFormat(Qt::RichText);
	title->setAccessibleName(QStringLiteral("OZK Multi"));
	title->setStyleSheet("font-size:17px; font-weight:700; color:#f0f7ff;");
	header->addWidget(title, 1);
	mainLayout->addLayout(header);

	auto t = new QWidget;
	auto tl = new QVBoxLayout;
	tl->setSpacing(8); // between canvas groups
	tl->setContentsMargins(0, 0, 0, 0);
	t->setStyleSheet(QString("padding: 0px; margin:0px;"));
	t->setLayout(tl);

	// Group for built in canvas
	auto mainCanvasGroup = new QGroupBox;
	mainCanvasGroup->setStyleSheet(canvasGroupStyle);

	mainCanvasLayout = new QVBoxLayout;
	mainCanvasLayout->setContentsMargins(5, 5, 5, 5);
	mainCanvasLayout->setSpacing(0);

	// We store the actual outputs here
	mainCanvasOutputLayout = new QGridLayout;
	mainCanvasOutputLayout->setContentsMargins(0, 0, 0, 0);
	mainCanvasOutputLayout->setHorizontalSpacing(4);
	mainCanvasOutputLayout->setVerticalSpacing(4);

	auto mainStreamGroup = new QGroupBox;
	mainStreamGroup->setStyleSheet(outputGroupStyle);
	mainStreamGroup->setAccessibleName(QString::fromUtf8(obs_module_text("BuiltinStream")));
	mainStreamGroup->setMinimumWidth(86);
	mainStreamGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto mainStreamLayout = new QVBoxLayout;
	mainStreamLayout->setContentsMargins(4, 4, 4, 4);
	mainStreamLayout->setSpacing(3);

	// blank because we're pulling settings through from bis later
	mainPlatformIconLabel = new QLabel;
	mainPlatformIconLabel->setAlignment(Qt::AlignCenter);
	mainPlatformIconLabel->setFixedHeight(34);
	mainPlatformIconLabel->setStyleSheet(QStringLiteral("background:transparent;"));
	mainPlatformIconLabel->setToolTip(QString::fromUtf8(obs_module_text("BuiltinStream")));
	auto platformIcon = ConfigUtils::getPlatformIconFromEndpoint(QString::fromUtf8(""));
	mainPlatformIconLabel->setPixmap(platformIcon.pixmap(outputPlatformIconSize, outputPlatformIconSize));

	mainStreamLayout->addWidget(mainPlatformIconLabel);

	mainStreamButton = new QPushButton;
	mainStreamButton->setObjectName(QStringLiteral("canvasStream"));
	mainStreamButton->setIcon(streamInactiveIcon);
	mainStreamButton->setCheckable(true);
	mainStreamButton->setChecked(false);
	mainStreamButton->setProperty("outputName", QString::fromUtf8(obs_module_text("BuiltinStream")));

	outputButtonStyle(mainStreamButton);

	connect(mainStreamButton, &QPushButton::clicked, [this] {
		const auto config = get_user_config();
		if (obs_frontend_streaming_active()) {
			bool stop = true;
			bool warnBeforeStreamStop = config_get_bool(config, "BasicWindow", "WarnBeforeStoppingStream");
			if (warnBeforeStreamStop && isVisible()) {
				auto button = QMessageBox::question(
					this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
					QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (button == QMessageBox::No) {
					stop = false;
				}
			}
			if (stop) {
				obs_frontend_streaming_stop();
				mainStreamButton->setChecked(false);
			} else {
				mainStreamButton->setChecked(true);
			}
		} else {
			bool warnBeforeStreamStart = config_get_bool(config, "BasicWindow", "WarnBeforeStartingStream");
			if (warnBeforeStreamStart && isVisible()) {
				auto button = QMessageBox::question(
					this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
					QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (button == QMessageBox::No) {
					mainStreamButton->setChecked(false);
				} else {
					obs_frontend_streaming_start();
				}
			} else {
				obs_frontend_streaming_start();
			}
			mainStreamButton->setChecked(true);
		}
		outputButtonStyle(mainStreamButton);
	});
	//streamButton->setSizePolicy(sp2);
	mainStreamLayout->addWidget(mainStreamButton);

	mainStreamGroup->setLayout(mainStreamLayout);

	mainCanvasOutputLayout->addWidget(mainStreamGroup, 0, 0);

	mainCanvasLayout->addLayout(mainCanvasOutputLayout);
	mainCanvasGroup->setLayout(mainCanvasLayout);

	tl->addWidget(mainCanvasGroup);

	tl->addStretch(1);
	QScrollArea *scrollArea = new QScrollArea;
	scrollArea->setWidget(t);
	scrollArea->setWidgetResizable(true);
	scrollArea->setLineWidth(0);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	mainLayout->addWidget(scrollArea, 1);

	// Bottom Button Row
	auto buttonRow = new QHBoxLayout;
	buttonRow->setContentsMargins(11, 3, 11, 4);
	buttonRow->setSpacing(6);

	// Config Button
	configButton = new QPushButton;
	configButton->setMinimumHeight(30);
	configButton->setProperty("themeID", "configIconSmall");
	configButton->setProperty("class", "icon-gear");
	configButton->setFlat(true);
	configButton->setAutoDefault(false);
	//configButton->setSizePolicy(sp2);
	configButton->setToolTip(QString::fromUtf8(obs_module_text("AitumMultistreamSettings")));
	QPushButton::connect(configButton, &QPushButton::clicked, [this] {
		if (!configDialog) {
			configDialog = new OBSBasicSettings((QMainWindow *)obs_frontend_get_main_window());
		}
		auto settings = obs_data_create();
		if (current_config) {
			obs_data_apply(settings, current_config);
		}
		configDialog->LoadSettings(settings);
		configDialog->LoadOutputStats(&oldVideo);
		configDialog->SetNewerVersion(newer_version_available);
		configDialog->setResult(QDialog::Rejected);
		if (configDialog->exec() == QDialog::Accepted) {
			if (current_config) {
				obs_data_apply(current_config, settings);
				obs_data_release(settings);
				SaveSettings();
				LoadSettings();
			} else {
				current_config = settings;
			}
		} else {
			obs_data_release(settings);
		}
	});

	configButton->setMinimumWidth(30);
	configButton->setMaximumWidth(34);
	configButton->setStyleSheet("QPushButton {background:#172536; border:1px solid #344c63; border-radius:6px; color:#b9cce0;} QPushButton:hover {background:#21364b; border-color:#4a7188;}");
	header->addWidget(configButton);

	// Compact links keep both the project support and the upstream credit visible.
	footerSupportLink = new QLabel;
	footerSupportLink->setText(QStringLiteral("<a href='https://dalink.to/otmopozok' style='color:#57d9c3;text-decoration:none'>") +
			     QString::fromUtf8(obs_module_text("DonateFooter")) + QStringLiteral("</a>"));
	footerSupportLink->setOpenExternalLinks(true);
	footerSupportLink->setToolTip(QString::fromUtf8(obs_module_text("AitumMultistreamDonate")));
	footerSupportLink->setStyleSheet(QStringLiteral("font-size:11px;background:transparent;"));
	buttonRow->addWidget(footerSupportLink);
	buttonRow->addStretch(1);
	footerCreditLink = new QLabel;
	footerCreditLink->setText(QStringLiteral("<span style='color:#5e7790'>✦ </span><a href='https://www.youtube.com/channel/UCTAKYR3T2Ud8zZgNShgQfyw/' style='color:#57dfd1;text-decoration:none;font-weight:700'>OTMOPOZOK</a><span style='color:#5e7790'> · </span><a href='https://github.com/Aitum/obs-aitum-multistream' style='color:#9fb8d1;text-decoration:none'>Aitum</a>"));
	footerCreditLink->setOpenExternalLinks(true);
	footerCreditLink->setToolTip(QStringLiteral("OZK Multi · OTMOPOZOK\nНа основе Aitum Multistream (GPL-2.0)"));
	footerCreditLink->setStyleSheet(QStringLiteral("font-size:11px;background:transparent;"));
	buttonRow->addWidget(footerCreditLink);

	mainLayout->addLayout(buttonRow);
	UpdateFooterWrapping();

	obs_frontend_add_event_callback(frontend_event, this);

	mainVideo = obs_get_video();
	connect(&videoCheckTimer, &QTimer::timeout, [this] {
		if (exiting) {
			return;
		}
		if (obs_get_video() != mainVideo) {
			oldVideo.push_back(mainVideo);
			mainVideo = obs_get_video();
			for (auto it = outputs.begin(); it != outputs.end(); it++) {
				auto venc = obs_output_get_video_encoder(std::get<obs_output_t *>(*it));
				if (venc && !obs_encoder_active(venc)) {
					obs_encoder_set_video(venc, mainVideo);
				}
			}
		}

		auto service = obs_frontend_get_streaming_service();
		auto url = QString::fromUtf8(service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL)
						     : "");
		if (url != mainPlatformUrl) {
			mainPlatformUrl = url;
			mainPlatformIconLabel->setPixmap(ConfigUtils::getPlatformIconFromEndpoint(url).pixmap(
				outputPlatformIconSize, outputPlatformIconSize));
		}

		int idx = 0;
		while (auto item = mainCanvasOutputLayout->itemAt(idx++)) {
			auto streamGroup = item->widget();
			if (idx == 1) {
				auto active = obs_frontend_streaming_active();
				foreach(QObject * c, streamGroup->children())
				{
					std::string cn = c->metaObject()->className();
					if (cn == "QPushButton") {
						auto pb = (QPushButton *)c;
						if (pb->isChecked() != active) {
							pb->setChecked(active);
							outputButtonStyle(pb);
						}
					}
				}
				continue;
			}
			std::string name = streamGroup->objectName().toUtf8().constData();
			if (name.empty()) {
				continue;
			}
			for (auto it = outputs.begin(); it != outputs.end(); it++) {
				if (std::get<std::string>(*it) != name) {
					continue;
				}

				auto active = obs_output_active(std::get<obs_output_t *>(*it));
				foreach(QObject * c, streamGroup->children())
				{
					std::string cn = c->metaObject()->className();
					if (cn == "QPushButton") {
						auto pb = (QPushButton *)c;
						if (pb->isChecked() != active) {
							pb->setChecked(active);
							outputButtonStyle(pb);
						}
					}
				}
			}
		}
	});
	videoCheckTimer.start(500);
	LoadSettingsFile();
}

MultistreamDock::~MultistreamDock()
{
	if (vendor && obs_get_module("obs-websocket")) {
		obs_websocket_vendor_unregister_request(vendor, "version");
		obs_websocket_vendor_unregister_request(vendor, "get_outputs");
		obs_websocket_vendor_unregister_request(vendor, "start_output");
		obs_websocket_vendor_unregister_request(vendor, "stop_output");
		vendor = nullptr;
	}
	videoCheckTimer.stop();
	for (auto it = outputs.begin(); it != outputs.end(); it++) {
		auto old = std::get<obs_output_t *>(*it);
		signal_handler_t *signal = obs_output_get_signal_handler(old);
		signal_handler_disconnect(signal, "start", stream_output_start, this);
		signal_handler_disconnect(signal, "stop", stream_output_stop, this);
		signal_handler_disconnect(signal, "reconnect", stream_output_reconnect, this);
		signal_handler_disconnect(signal, "reconnect_success", stream_output_reconnect_success, this);
		auto service = obs_output_get_service(old);
		if (obs_output_active(old)) {
			obs_output_force_stop(old);
		}
		if (!exiting) {
			obs_output_release(old);
		}
		obs_service_release(service);
	}
	outputs.clear();
	obs_data_array_release(vertical_outputs);
	obs_data_release(current_config);
	obs_frontend_remove_event_callback(frontend_event, this);
	multistream_dock = nullptr;
}

void MultistreamDock::frontend_event(enum obs_frontend_event event, void *private_data)
{
	auto md = (MultistreamDock *)private_data;
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		md->finished_loading = true;
		md->LoadSettingsFile();
		if (!md->newer_version_available.isEmpty()) {
			md->AskUpdate();
		}
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
		md->LoadSettingsFile();
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING || event == OBS_FRONTEND_EVENT_PROFILE_RENAMED) {
		md->SaveSettings();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		md->SaveSettings();
		md->exiting = true;
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING || event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		md->mainStreamButton->setChecked(true);
		md->outputButtonStyle(md->mainStreamButton);
		md->storeMainStreamEncoders();
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING || event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		md->mainStreamButton->setChecked(false);
		md->outputButtonStyle(md->mainStreamButton);
	}
}

void MultistreamDock::LoadSettingsFile()
{
	char *profile = obs_frontend_get_current_profile();
	if (current_config && strcmp(obs_data_get_string(current_config, "name"), profile) == 0) {
		bfree(profile);
		return;
	}
	obs_data_release(current_config);
	current_config = nullptr;
	char *path = obs_module_config_path("config.json");
	if (!path) {
		bfree(profile);
		return;
	}
	obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");
	if (!config) {
		// Import horizontal outputs once, leaving the original Aitum settings untouched.
		const QFileInfo target(QString::fromUtf8(path));
		const QString legacyPath = target.dir().filePath(QStringLiteral("../aitum-multistream/config.json"));
		config = obs_data_create_from_json_file_safe(legacyPath.toUtf8().constData(), "bak");
		if (config) {
			QDir().mkpath(target.absolutePath());
			obs_data_save_json_safe(config, path, "tmp", "bak");
			blog(LOG_INFO, "[OZK Multi] imported horizontal output settings from Aitum Multistream");
		}
	}
	bfree(path);
	if (!config) {
		config = obs_data_create();
		blog(LOG_WARNING, "[Aitum Multistream] No configuration file loaded");
	} else {
		blog(LOG_INFO, "[Aitum Multistream] Loaded configuration file");
	}
	partnerBlockTime = obs_data_get_int(config, "partner_block");

	auto profiles = obs_data_get_array(config, "profiles");
	auto pc = obs_data_array_count(profiles);
	obs_data_t *pd = nullptr;
	for (size_t i = 0; i < pc; i++) {
		obs_data_t *t = obs_data_array_item(profiles, i);
		if (!t) {
			continue;
		}
		auto name = obs_data_get_string(t, "name");
		if (strcmp(profile, name) == 0) {
			pd = t;
			break;
		}
		obs_data_release(t);
	}
	obs_data_array_release(profiles);

	obs_data_release(config);
	if (!pd) {
		current_config = obs_data_create();
		obs_data_set_string(current_config, "name", profile);
		bfree(profile);
		blog(LOG_INFO, "[Aitum Multistream] profile not found");
		LoadSettings();
		return;
	}
	bfree(profile);
	current_config = pd;
	LoadSettings();
}

void MultistreamDock::LoadSettings()
{

	auto outputs2 = obs_data_get_array(current_config, "outputs");
	int idx = 1;
	while (auto item = mainCanvasOutputLayout->itemAt(idx)) {
		auto streamGroup = item->widget();
		mainCanvasOutputLayout->removeWidget(streamGroup);
		RemoveWidget(streamGroup);
	}

	obs_data_array_enum(
		outputs2,
		[](obs_data_t *data2, void *param) {
			auto d = (MultistreamDock *)param;
			d->LoadOutput(data2, false);
		},
		this);
	obs_data_array_release(outputs2);
	outputColumns = 0;
	ReflowOutputCards();
}

void MultistreamDock::resizeEvent(QResizeEvent *event)
{
	QFrame::resizeEvent(event);
	ReflowOutputCards();
	UpdateFooterWrapping();
}

void MultistreamDock::UpdateFooterWrapping()
{
	if (!footerSupportLink || !footerCreditLink)
		return;
	const int needed = 40 + footerSupportLink->fontMetrics().horizontalAdvance(
		QString::fromUtf8(obs_module_text("DonateFooter"))) +
		footerCreditLink->fontMetrics().horizontalAdvance(QStringLiteral("✦ OTMOPOZOK · Aitum"));
	const bool wrap = width() < needed;
	footerSupportLink->setWordWrap(wrap);
	footerCreditLink->setWordWrap(wrap);
}

void MultistreamDock::ReflowOutputCards()
{
	if (!mainCanvasOutputLayout)
		return;
	const int count = mainCanvasOutputLayout->count();
	if (count == 0)
		return;
	const int columns = qMax(1, qMin(count, (width() - 24) / 100));
	if (columns == outputColumns)
		return;
	QVector<QWidget *> cards;
	cards.reserve(count);
	while (QLayoutItem *item = mainCanvasOutputLayout->takeAt(0)) {
		cards.append(item->widget());
		delete item;
	}
	for (int i = 0; i < cards.size(); ++i)
		mainCanvasOutputLayout->addWidget(cards.at(i), i / columns, i % columns);
	outputColumns = columns;
}

void MultistreamDock::LoadOutput(obs_data_t *output_data, bool vertical)
{
	auto nameChars = obs_data_get_string(output_data, "name");
	auto name = QString::fromUtf8(nameChars);
	if (vertical) {
		for (int i = 0; i < verticalCanvasOutputLayout->count(); i++) {
			auto item = verticalCanvasOutputLayout->itemAt(i);
			auto oName = item->widget()->objectName();
			if (oName == name) {
				return;
			}
		}
	} else {
		for (int i = 1; i < mainCanvasOutputLayout->count(); i++) {
			auto item = mainCanvasOutputLayout->itemAt(i);
			auto oName = item->widget()->objectName();
			if (oName == name) {
				return;
			}
		}
	}
	auto streamButton = new QPushButton;
	for (auto it = outputs.begin(); it != outputs.end(); it++) {
		if (std::get<std::string>(*it) != nameChars) {
			continue;
		}
		if (obs_data_get_bool(output_data, "advanced")) {
			auto output = std::get<obs_output_t *>(*it);
			auto video_encoder = obs_output_get_video_encoder(output);
			if (video_encoder &&
			    strcmp(obs_encoder_get_id(video_encoder), obs_data_get_string(output_data, "video_encoder")) == 0) {
				auto ves = obs_data_get_obj(output_data, "video_encoder_settings");
				obs_encoder_update(video_encoder, ves);
				obs_data_release(ves);
			}
		}
		std::get<QPushButton *>(*it) = streamButton;
	}
	auto streamGroup = new QGroupBox;
	streamGroup->setStyleSheet(outputGroupStyle);
	streamGroup->setObjectName(name);
	streamGroup->setAccessibleName(name);
	streamGroup->setMinimumWidth(86);
	streamGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto streamLayout = new QVBoxLayout;
	streamLayout->setContentsMargins(4, 4, 4, 4);
	streamLayout->setSpacing(3);

	auto endpoint = QString::fromUtf8(obs_data_get_string(output_data, "stream_server"));
	auto platformIconLabel = new QLabel;
	platformIconLabel->setAlignment(Qt::AlignCenter);
	platformIconLabel->setFixedHeight(34);
	platformIconLabel->setStyleSheet(QStringLiteral("background:transparent;"));
	platformIconLabel->setToolTip(name);
	auto platformIcon = ConfigUtils::getPlatformIconFromEndpoint(endpoint);

	platformIconLabel->setPixmap(platformIcon.pixmap(outputPlatformIconSize, outputPlatformIconSize));

	streamLayout->addWidget(platformIconLabel);

	streamButton->setMinimumHeight(30);
	streamButton->setObjectName(QStringLiteral("canvasStream"));
	streamButton->setIcon(streamInactiveIcon);
	streamButton->setCheckable(true);
	streamButton->setChecked(false);
	streamButton->setProperty("outputName", name);
	streamButton->setProperty("outputState", "idle");
	outputButtonStyle(streamButton);

	if (vertical) {
		std::string output_name = obs_data_get_string(output_data, "name");
		connect(streamButton, &QPushButton::clicked, [this, streamButton, output_name] {
			auto ph = obs_get_proc_handler();
			struct calldata cd;
			calldata_init(&cd);
			calldata_set_string(&cd, "name", output_name.c_str());
			auto config = get_user_config();
			if (streamButton->isChecked()) {
				bool start = true;
				bool warnBeforeStreamStart = config_get_bool(config, "BasicWindow", "WarnBeforeStartingStream");
				if (warnBeforeStreamStart && isVisible()) {
					auto button = QMessageBox::question(
						this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
						QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
					if (button == QMessageBox::No) {
						start = false;
					}
				}
				if (!start || !proc_handler_call(ph, "aitum_vertical_start_stream_output", &cd)) {
					streamButton->setChecked(false);
				}
			} else {
				bool stop = true;
				bool warnBeforeStreamStop = config_get_bool(config, "BasicWindow", "WarnBeforeStoppingStream");
				if (warnBeforeStreamStop && isVisible()) {
					auto button = QMessageBox::question(
						this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
						QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
					if (button == QMessageBox::No) {
						stop = false;
					}
				}
				if (stop) {
					proc_handler_call(ph, "aitum_vertical_stop_stream_output", &cd);
				} else {
					streamButton->setChecked(true);
				}
			}
			calldata_free(&cd);

			outputButtonStyle(streamButton);
		});
	} else {
		connect(streamButton, &QPushButton::clicked, [this, streamButton, output_data] {
			if (streamButton->isChecked()) {
				streamButton->setProperty("outputState", "connecting");
				streamButton->setProperty("outputError", QString());
				blog(LOG_INFO, "[Aitum Multistream] start stream clicked '%s'",
				     obs_data_get_string(output_data, "name"));
				if (!StartOutput(output_data, streamButton)) {
					streamButton->setChecked(false);
					if (streamButton->property("outputState").toString() != "error")
						streamButton->setProperty("outputState", "idle");
				}
			} else {
				bool stop = true;
				bool warnBeforeStreamStop =
					config_get_bool(get_user_config(), "BasicWindow", "WarnBeforeStoppingStream");
				if (warnBeforeStreamStop && isVisible()) {
					auto button = QMessageBox::question(
						this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
						QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
					if (button == QMessageBox::No) {
						stop = false;
					}
				}
				if (stop) {
					streamButton->setProperty("outputState", "stopping");
					blog(LOG_INFO, "[Aitum Multistream] stop stream clicked '%s'",
					     obs_data_get_string(output_data, "name"));
					const char *name2 = obs_data_get_string(output_data, "name");
					for (auto it = outputs.begin(); it != outputs.end(); it++) {
						if (std::get<std::string>(*it) != name2) {
							continue;
						}

						obs_queue_task(
							OBS_TASK_GRAPHICS,
							[](void *param) { obs_output_stop((obs_output_t *)param); },
							std::get<obs_output *>(*it), false);
					}
				} else {
					streamButton->setChecked(true);
					streamButton->setProperty("outputState", "live");
				}
			}
			outputButtonStyle(streamButton);
		});
	}
	//streamButton->setSizePolicy(sp2);
	streamLayout->addWidget(streamButton);

	streamGroup->setLayout(streamLayout);

	if (vertical) {
		verticalCanvasOutputLayout->addWidget(streamGroup);
	} else {
		mainCanvasOutputLayout->addWidget(streamGroup, mainCanvasOutputLayout->count(), 0);
	}
}

static void ensure_directory(char *path)
{
#ifdef _WIN32
	char *backslash = strrchr(path, '\\');
	if (backslash) {
		*backslash = '/';
	}
#endif

	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = 0;
		os_mkdirs(path);
		*slash = '/';
	}

#ifdef _WIN32
	if (backslash) {
		*backslash = '\\';
	}
#endif
}

void MultistreamDock::SaveSettings()
{
	char *path = obs_module_config_path("config.json");
	if (!path) {
		return;
	}
	obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");
	if (!config) {
		ensure_directory(path);
		config = obs_data_create();
		blog(LOG_WARNING, "[Aitum Multistream] New configuration file");
	}
	obs_data_set_int(config, "partner_block", partnerBlockTime);
	auto profiles = obs_data_get_array(config, "profiles");
	if (!profiles) {
		profiles = obs_data_array_create();
		obs_data_set_array(config, "profiles", profiles);
	}
	obs_data_t *pd = nullptr;
	if (current_config) {
		auto old_name = obs_data_get_string(current_config, "name");
		auto pc = obs_data_array_count(profiles);
		for (size_t i = 0; i < pc; i++) {
			obs_data_t *t = obs_data_array_item(profiles, i);
			if (!t) {
				continue;
			}
			auto name = obs_data_get_string(t, "name");
			if (strcmp(old_name, name) == 0) {
				pd = t;
				break;
			}
			obs_data_release(t);
		}
	}
	if (!pd) {
		pd = obs_data_create();
		obs_data_array_push_back(profiles, pd);
	}
	obs_data_array_release(profiles);
	char *profile = obs_frontend_get_current_profile();
	obs_data_set_string(pd, "name", profile);
	bfree(profile);
	if (current_config) {
		obs_data_apply(pd, current_config);
	}
	obs_data_release(pd);

	if (obs_data_save_json_safe(config, path, "tmp", "bak")) {
		blog(LOG_INFO, "[Aitum Multistream] Saved settings");
	} else {
		blog(LOG_ERROR, "[Aitum Multistream] Failed saving settings");
	}
	obs_data_release(config);
	bfree(path);
}

bool MultistreamDock::StartOutput(obs_data_t *settings, QPushButton *streamButton)
{
	if (!settings) {
		return false;
	}

	bool warnBeforeStreamStart = config_get_bool(get_user_config(), "BasicWindow", "WarnBeforeStartingStream");
	if (warnBeforeStreamStart && isVisible()) {
		auto button = QMessageBox::question(this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
						    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (button == QMessageBox::No) {
			return false;
		}
	}

	const char *name = obs_data_get_string(settings, "name");
	for (auto it = outputs.begin(); it != outputs.end(); it++) {
		if (std::get<std::string>(*it) != name) {
			continue;
		}
		auto old = std::get<obs_output_t *>(*it);
		signal_handler_t *signal = obs_output_get_signal_handler(old);
		signal_handler_disconnect(signal, "start", stream_output_start, this);
		signal_handler_disconnect(signal, "stop", stream_output_stop, this);
		signal_handler_disconnect(signal, "reconnect", stream_output_reconnect, this);
		signal_handler_disconnect(signal, "reconnect_success", stream_output_reconnect_success, this);
		auto service = obs_output_get_service(old);
		if (obs_output_active(old)) {
			obs_output_force_stop(old);
		}
		obs_output_release(old);
		obs_service_release(service);
		outputs.erase(it);
		break;
	}
	obs_encoder_t *venc = nullptr;
	obs_encoder_t *aenc = nullptr;
	auto advanced = obs_data_get_bool(settings, "advanced");
	if (advanced) {
		auto venc_name = obs_data_get_string(settings, "video_encoder");
		if (!venc_name || venc_name[0] == '\0') {
			//use main encoder
			auto main_output = obs_frontend_get_streaming_output();
			if (!obs_output_active(main_output)) {
				obs_output_release(main_output);
				blog(LOG_WARNING, "[Aitum Multistream] failed to start stream '%s' because main was not started",
				     obs_data_get_string(settings, "name"));
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("MainOutputNotActive")),
						     QString::fromUtf8(obs_module_text("MainOutputNotActive")));
				return false;
			}
			auto vei = (int)obs_data_get_int(settings, "video_encoder_index");
			venc = obs_output_get_video_encoder2(main_output, vei);
			obs_output_release(main_output);
			if (!venc) {
				blog(LOG_WARNING,
				     "[Aitum Multistream] failed to start stream '%s' because encoder index %d was not found",
				     obs_data_get_string(settings, "name"), vei);
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("MainOutputEncoderIndexNotFound")),
						     QString::fromUtf8(obs_module_text("MainOutputEncoderIndexNotFound")));
				return false;
			}
		} else {
			obs_data_t *s = nullptr;
			auto ves = obs_data_get_obj(settings, "video_encoder_settings");
			if (ves) {
				s = obs_data_create();
				obs_data_apply(s, ves);
				obs_data_release(ves);
			}
			std::string video_encoder_name = "aitum_multi_video_encoder_";
			video_encoder_name += name;
			venc = obs_video_encoder_create(venc_name, video_encoder_name.c_str(), s, nullptr);
			obs_data_release(s);
			obs_encoder_set_video(venc, obs_get_video());
			auto divisor = obs_data_get_int(settings, "frame_rate_divisor");
			if (divisor > 1) {
				obs_encoder_set_frame_rate_divisor(venc, (uint32_t)divisor);
			}

			bool scale = obs_data_get_bool(settings, "scale");
			if (scale) {
				obs_encoder_set_scaled_size(venc, (uint32_t)obs_data_get_int(settings, "width"),
							    (uint32_t)obs_data_get_int(settings, "height"));
				obs_encoder_set_gpu_scale_type(venc, (obs_scale_type)obs_data_get_int(settings, "scale_type"));
			}
		}
		auto aenc_name = obs_data_get_string(settings, "audio_encoder");
		if (!aenc_name || aenc_name[0] == '\0') {
			//use main encoder
			auto main_output = obs_frontend_get_streaming_output();
			if (!obs_output_active(main_output)) {
				obs_output_release(main_output);
				blog(LOG_WARNING, "[Aitum Multistream] failed to start stream '%s' because main was not started",
				     obs_data_get_string(settings, "name"));
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("MainOutputNotActive")),
						     QString::fromUtf8(obs_module_text("MainOutputNotActive")));
				return false;
			}
			auto aei = (int)obs_data_get_int(settings, "audio_encoder_index");
			aenc = obs_output_get_audio_encoder(main_output, aei);
			obs_output_release(main_output);
			if (!aenc) {
				blog(LOG_WARNING,
				     "[Aitum Multistream] failed to start stream '%s' because encoder index %d was not found",
				     obs_data_get_string(settings, "name"), aei);
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("MainOutputEncoderIndexNotFound")),
						     QString::fromUtf8(obs_module_text("MainOutputEncoderIndexNotFound")));
				return false;
			}
		} else {
			obs_data_t *s = nullptr;
			auto aes = obs_data_get_obj(settings, "audio_encoder_settings");
			if (aes) {
				s = obs_data_create();
				obs_data_apply(s, aes);
				obs_data_release(aes);
			}
			std::string audio_encoder_name = "aitum_multi_audio_encoder_";
			audio_encoder_name += name;
			aenc = obs_audio_encoder_create(aenc_name, audio_encoder_name.c_str(), s,
							obs_data_get_int(settings, "audio_track"), nullptr);
			obs_data_release(s);
			obs_encoder_set_audio(aenc, obs_get_audio());
		}
	} else {
		auto main_output = obs_frontend_get_streaming_output();
		venc = main_output ? obs_output_get_video_encoder(main_output) : nullptr;
		if (!venc || !obs_output_active(main_output)) {
			obs_output_release(main_output);
			blog(LOG_WARNING, "[Aitum Multistream] failed to start stream '%s' because main was not started",
			     obs_data_get_string(settings, "name"));
			QMessageBox::warning(this, QString::fromUtf8(obs_module_text("MainOutputNotActive")),
					     QString::fromUtf8(obs_module_text("MainOutputNotActive")));
			return false;
		}

		aenc = obs_output_get_audio_encoder(main_output, 0);
		obs_output_release(main_output);
	}
	if (!aenc || !venc) {
		return false;
	}
	auto server = obs_data_get_string(settings, "stream_server");
	if (!server || !strlen(server)) {
		server = obs_data_get_string(settings, "server");
		if (server && strlen(server)) {
			obs_data_set_string(settings, "stream_server", server);
		}
	}
	bool whip = strstr(server, "whip") != nullptr;
	auto s = obs_data_create();
	obs_data_set_string(s, "server", server);
	auto key = obs_data_get_string(settings, "stream_key");
	if (!key || !strlen(key)) {
		key = obs_data_get_string(settings, "key");
		if (key && strlen(key)) {
			obs_data_set_string(settings, "stream_key", key);
		}
	}
	if (whip) {
		obs_data_set_string(s, "bearer_token", key);
	} else {
		obs_data_set_string(s, "key", key);
	}
	//use_auth
	//username
	//password
	std::string service_name = "aitum_multi_service_";
	service_name += name;
	auto service = obs_service_create(whip ? "whip_custom" : "rtmp_custom", service_name.c_str(), s, nullptr);
	obs_data_release(s);

	const char *type = obs_service_get_preferred_output_type(service);
	if (!type) {
		type = "rtmp_output";
		if (strncmp(server, "ftl", 3) == 0) {
			type = "ftl_output";
		} else if (strncmp(server, "rtmp", 4) != 0) {
			type = "ffmpeg_mpegts_muxer";
		}
	}
	std::string output_name = "aitum_multi_output_";
	output_name += name;
	auto output = obs_output_create(type, output_name.c_str(), nullptr, nullptr);
	obs_output_set_service(output, service);

	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		obs_data_t *output_settings = obs_data_create();
		obs_data_set_string(output_settings, "bind_ip", config_get_string(config, "Output", "BindIP"));
		obs_data_set_string(output_settings, "ip_family", config_get_string(config, "Output", "IPFamily"));
		obs_output_update(output, output_settings);
		obs_data_release(output_settings);

		bool useDelay = config_get_bool(config, "Output", "DelayEnable");
		auto delaySec = (uint32_t)config_get_int(config, "Output", "DelaySec");
		bool preserveDelay = config_get_bool(config, "Output", "DelayPreserve");
		obs_output_set_delay(output, useDelay ? delaySec : 0, preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	}

	signal_handler_t *signal = obs_output_get_signal_handler(output);
	signal_handler_disconnect(signal, "start", stream_output_start, this);
	signal_handler_disconnect(signal, "stop", stream_output_stop, this);
	signal_handler_disconnect(signal, "reconnect", stream_output_reconnect, this);
	signal_handler_disconnect(signal, "reconnect_success", stream_output_reconnect_success, this);
	signal_handler_connect(signal, "start", stream_output_start, this);
	signal_handler_connect(signal, "stop", stream_output_stop, this);
	signal_handler_connect(signal, "reconnect", stream_output_reconnect, this);
	signal_handler_connect(signal, "reconnect_success", stream_output_reconnect_success, this);

	//for (size_t i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; i++) {
	//auto venc = obs_output_get_video_encoder2(main_output, 0);
	//for (size_t i = 0; i < MAX_OUTPUT_AUDIO_ENCODERS; i++) {
	//obs_output_get_audio_encoder(main_output, 0);

	obs_output_set_video_encoder(output, venc);
	obs_output_set_audio_encoder(output, aenc, 0);

	if (!obs_output_start(output)) {
		const char *error = obs_output_get_last_error(output);
		streamButton->setProperty("outputState", "error");
		streamButton->setProperty("outputError", error ? QString::fromUtf8(error) : QString());
		signal_handler_disconnect(signal, "start", stream_output_start, this);
		signal_handler_disconnect(signal, "stop", stream_output_stop, this);
		signal_handler_disconnect(signal, "reconnect", stream_output_reconnect, this);
		signal_handler_disconnect(signal, "reconnect_success", stream_output_reconnect_success, this);
		obs_output_release(output);
		obs_service_release(service);
		return false;
	}

	outputs.push_back({obs_data_get_string(settings, "name"), output, streamButton});
	if (obs_output_active(output))
		streamButton->setProperty("outputState", "live");

	return true;
}

void MultistreamDock::stream_output_start(void *data, calldata_t *calldata)
{
	auto md = (MultistreamDock *)data;
	auto output = (obs_output_t *)calldata_ptr(calldata, "output");
	if (md->vendor) {
		auto d = obs_data_create();
		auto name = obs_output_get_name(output);
		if (name) {
			obs_data_set_string(d, "name", name);
		}
		obs_websocket_vendor_emit_event(md->vendor, "output_started", d);
		obs_data_release(d);
	}
	for (auto it = md->outputs.begin(); it != md->outputs.end(); it++) {
		if (std::get<obs_output_t *>(*it) != output) {
			continue;
		}
		auto button = std::get<QPushButton *>(*it);
		QMetaObject::invokeMethod(button, [button, md] {
			button->setChecked(true);
			button->setProperty("outputState", "live");
			button->setProperty("outputError", QString());
			md->outputButtonStyle(button);
		}, Qt::QueuedConnection);
	}
}

void MultistreamDock::stream_output_reconnect(void *data, calldata_t *calldata)
{
	auto md = (MultistreamDock *)data;
	auto output = (obs_output_t *)calldata_ptr(calldata, "output");
	for (auto &item : md->outputs) {
		if (std::get<obs_output_t *>(item) != output)
			continue;
		auto button = std::get<QPushButton *>(item);
		QMetaObject::invokeMethod(button, [button, md] {
			button->setProperty("outputState", "connecting");
			md->outputButtonStyle(button);
		}, Qt::QueuedConnection);
		break;
	}
}

void MultistreamDock::stream_output_reconnect_success(void *data, calldata_t *calldata)
{
	auto md = (MultistreamDock *)data;
	auto output = (obs_output_t *)calldata_ptr(calldata, "output");
	for (auto &item : md->outputs) {
		if (std::get<obs_output_t *>(item) != output)
			continue;
		auto button = std::get<QPushButton *>(item);
		QMetaObject::invokeMethod(button, [button, md] {
			button->setChecked(true);
			button->setProperty("outputState", "live");
			button->setProperty("outputError", QString());
			md->outputButtonStyle(button);
		}, Qt::QueuedConnection);
		break;
	}
}

void MultistreamDock::stream_output_stop(void *data, calldata_t *calldata)
{
	auto md = (MultistreamDock *)data;
	auto output = (obs_output_t *)calldata_ptr(calldata, "output");
	const int stopCode = (int)calldata_int(calldata, "code");
	const char *lastError = obs_output_get_last_error(output);
	const QString errorText = lastError && *lastError ? QString::fromUtf8(lastError) :
		(stopCode == OBS_OUTPUT_SUCCESS ? QString() : QStringLiteral("OBS: error code %1").arg(stopCode));
	if (md->vendor) {
		auto d = obs_data_create();
		auto name = obs_output_get_name(output);
		if (name) {
			obs_data_set_string(d, "name", name);
		}
		obs_websocket_vendor_emit_event(md->vendor, "output_stopped", d);
		obs_data_release(d);
	}
	for (auto it = md->outputs.begin(); it != md->outputs.end(); it++) {
		if (std::get<obs_output_t *>(*it) != output) {
			continue;
		}
		auto button = std::get<QPushButton *>(*it);
		QMetaObject::invokeMethod(button, [button, md, stopCode, errorText] {
			button->setChecked(false);
			button->setProperty("outputState", stopCode == OBS_OUTPUT_SUCCESS ? "idle" : "error");
			button->setProperty("outputError", errorText);
			md->outputButtonStyle(button);
		}, Qt::QueuedConnection);
		if (!md->exiting) {
			QMetaObject::invokeMethod(button, [output] { obs_output_release(output); }, Qt::QueuedConnection);
		}
		md->outputs.erase(it);
		break;
	}
	//const char *last_error = (const char *)calldata_ptr(calldata, "last_error");
}

void MultistreamDock::ApiInfo(QString info)
{
	auto d = obs_data_create_from_json(info.toUtf8().constData());
	if (!d) {
		return;
	}
	auto data_obj = obs_data_get_obj(d, "data");
	obs_data_release(d);
	if (!data_obj) {
		return;
	}
	auto version = obs_data_get_string(data_obj, "version");
	int major;
	int minor;
	int patch;
	if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3) {
		auto sv = MAKE_SEMANTIC_VERSION(major, minor, patch);
		if (sv > MAKE_SEMANTIC_VERSION(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH)) {
			newer_version_available = QString::fromUtf8(version);
			configButton->setStyleSheet(QString::fromUtf8("background: rgb(192,128,0);"));
			if (finished_loading) {
				AskUpdate();
			}
		}
	}
	time_t current_time = time(nullptr);
	if (current_time < partnerBlockTime || current_time - partnerBlockTime > 1209600) {
		obs_data_array_t *blocks = obs_data_get_array(data_obj, "partnerBlocks");
		size_t count = obs_data_array_count(blocks);
		size_t added_count = 0;
		for (size_t i = count; i > 0; i--) {
			obs_data_t *block = obs_data_array_item(blocks, i - 1);
			auto block_type = obs_data_get_string(block, "type");
			QBoxLayout *layout = nullptr;
			if (strcmp(block_type, "LINK") == 0) {
				auto button = new QPushButton(QString::fromUtf8(obs_data_get_string(block, "label")));
				button->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
				auto url = QString::fromUtf8(obs_data_get_string(block, "data"));
				connect(button, &QPushButton::clicked, [url] { QDesktopServices::openUrl(QUrl(url)); });
				auto buttonRow = new QHBoxLayout;
				buttonRow->setContentsMargins(8, 0, 8, 0);
				buttonRow->setSpacing(8);
				buttonRow->addWidget(button);
				layout = buttonRow;
			} else if (strcmp(block_type, "IMAGE") == 0) {
				auto image_data = QString::fromUtf8(obs_data_get_string(block, "data"));
				if (image_data.startsWith("data:image/")) {
					auto pos = image_data.indexOf(";");
					auto format = image_data.mid(11, pos - 11);
					QImage image;
					if (image.loadFromData(QByteArray::fromBase64(image_data.mid(pos + 7).toUtf8().constData()),
							       format.toUtf8().constData())) {
						auto label = new AspectRatioPixmapLabel;
						label->setPixmap(QPixmap::fromImage(image));
						label->setAlignment(Qt::AlignCenter);
						label->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
						auto labelRow = new QHBoxLayout;
						labelRow->addWidget(label, 1, Qt::AlignCenter);
						layout = labelRow;
					}
				}
			}
			if (layout) {
				added_count++;
				if (i == 1) {
					auto closeButton = new QPushButton("🞫");
					connect(closeButton, &QPushButton::clicked, [this, added_count] {
						for (size_t j = 0; j < added_count; j++) {
							auto item = mainLayout->takeAt(1);
							RemoveLayoutItem(item);
						}
						partnerBlockTime = time(nullptr);
						SaveSettings();
					});
					layout->addWidget(closeButton);
				}
				mainLayout->insertLayout(1, layout, 0);
			}
			obs_data_release(block);
		}
		obs_data_array_release(blocks);
	}
	obs_data_release(data_obj);
}

void MultistreamDock::LoadVerticalOutputs(bool firstLoad)
{
	auto ph = obs_get_proc_handler();
	struct calldata cd;
	calldata_init(&cd);
	if (!proc_handler_call(ph, "aitum_vertical_get_stream_settings", &cd)) {
		if (firstLoad) {                                         // only display warning on first load
			showVerticalWarning(verticalCanvasOutputLayout); // show warning
		}
		calldata_free(&cd);
		return;
	}

	if (vertical_outputs) {
		obs_data_array_release(vertical_outputs);
	}
	vertical_outputs = (obs_data_array_t *)calldata_ptr(&cd, "outputs");

	calldata_free(&cd);
	int idx = 0;
	while (auto item = verticalCanvasOutputLayout->itemAt(idx)) {
		auto streamGroup = item->widget();
		verticalCanvasOutputLayout->removeWidget(streamGroup);
		RemoveWidget(streamGroup);
	}

	obs_data_array_enum(
		vertical_outputs,
		[](obs_data_t *data2, void *param) {
			auto d = (MultistreamDock *)param;
			d->LoadOutput(data2, true);
		},
		this);
}

void MultistreamDock::storeMainStreamEncoders()
{
	if (!current_config) {
		return;
	}
	struct obs_video_info ovi = {0};
	obs_get_video_info(&ovi);
	double fps = ovi.fps_den > 0 ? (double)ovi.fps_num / (double)ovi.fps_den : 0.0;
	auto output = obs_frontend_get_streaming_output();
	bool found = false;
	for (auto i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; i++) {
		auto encoder = obs_output_get_video_encoder2(output, i);
		QString settingName = QString::fromUtf8("video_encoder_description") + QString::number(i);
		if (encoder) {
			found = true;
			auto mainEncoderDescription = QString::number(obs_encoder_get_width(encoder)) + "x" +
						      QString::number(obs_encoder_get_height(encoder));
			auto divisor = obs_encoder_get_frame_rate_divisor(encoder);
			if (divisor > 0) {
				mainEncoderDescription +=
					QString::fromUtf8(" ") + QString::number(fps / divisor, 'g', 4) + QString::fromUtf8("fps");
			}

			auto settings = obs_encoder_get_settings(encoder);
			auto bitrate = settings ? obs_data_get_int(settings, "bitrate") : 0;
			if (bitrate > 0) {
				mainEncoderDescription +=
					QString::fromUtf8(" ") + QString::number(bitrate) + QString::fromUtf8("Kbps");
			}
			obs_data_release(settings);

			obs_data_set_string(current_config, settingName.toUtf8().constData(),
					    mainEncoderDescription.toUtf8().constData());

		} else if (found && obs_data_has_user_value(current_config, settingName.toUtf8().constData())) {
			obs_data_unset_user_value(current_config, settingName.toUtf8().constData());
		}
	}
	obs_output_release(output);
}

void MultistreamDock::AskUpdate()
{
	auto parts = newer_version_available.split(".");
	if (parts.count() < 3) {
		return;
	}
	int major = parts.value(0).toInt();
	int minor = parts.value(1).toInt();
	int patch = parts.value(2).toInt();
	auto sv = MAKE_SEMANTIC_VERSION(major, minor, patch);

	char *path = obs_module_config_path("config.json");
	if (!path) {
		return;
	}

	obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");

	auto skip_version = config ? obs_data_get_int(config, "skip_version") : 0;
	if (sv == skip_version) {
		obs_data_release(config);
		bfree(path);
		return;
	}

	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	QMessageBox mb(QMessageBox::Question, QString::fromUtf8(obs_frontend_get_locale_string("Updater.Title")),
		       QString::fromUtf8(obs_frontend_get_locale_string("Updater.Text")) + " " +
			       QString::fromUtf8(obs_module_text("AitumMultistream")) + " " + newer_version_available,
		       QMessageBox::StandardButtons(), main_window);
	auto update = mb.addButton(QString::fromUtf8(obs_frontend_get_locale_string("Updater.UpdateNow")), QMessageBox::YesRole);
	auto remind =
		mb.addButton(QString::fromUtf8(obs_frontend_get_locale_string("Updater.RemindMeLater")), QMessageBox::RejectRole);
	auto skip = mb.addButton(QString::fromUtf8(obs_frontend_get_locale_string("Updater.Skip")), QMessageBox::NoRole);
	mb.setDefaultButton(remind);
	mb.exec();
	if (mb.clickedButton() == update) {
		QDesktopServices::openUrl(QUrl(QString::fromUtf8("https://github.com/DIMI0007/ozk-multi/releases")));
	} else if (mb.clickedButton() == skip) {
		if (!config) {
			config = obs_data_create();
		}
		obs_data_set_int(config, "skip_version", sv);
		if (obs_data_save_json_safe(config, path, "tmp", "bak")) {
			blog(LOG_INFO, "[Aitum Multistream] Saved settings");
		} else {
			blog(LOG_ERROR, "[Aitum Multistream] Failed saving settings");
		}
	}
	obs_data_release(config);
	bfree(path);
}

void MultistreamDock::LoadWebsocket()
{
	if (vendor) {
		return;
	}
	vendor = obs_websocket_register_vendor("stream-multi");
	if (!vendor) {
		blog(LOG_ERROR, "[Aitum Multistream] Failed to register websocket vendor");
		return;
	}
	obs_websocket_vendor_register_request(vendor, "version", vendor_request_version, this);
	obs_websocket_vendor_register_request(vendor, "get_outputs", vendor_request_get_outputs, this);
	obs_websocket_vendor_register_request(vendor, "start_output", vendor_request_start_output, this);
	obs_websocket_vendor_register_request(vendor, "stop_output", vendor_request_stop_output, this);
}

void MultistreamDock::vendor_request_version(obs_data_t *request_data, obs_data_t *response_data, void *self)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(self);
	obs_data_set_string(response_data, "version", PROJECT_VERSION);
	obs_data_set_bool(response_data, "success", true);
}

void MultistreamDock::vendor_request_get_outputs(obs_data_t *request_data, obs_data_t *response_data, void *self)
{
	UNUSED_PARAMETER(request_data);
	auto md = (MultistreamDock *)self;
	auto outputs3 = obs_data_array_create();
	auto outputs2 = obs_data_get_array(md->current_config, "outputs");
	obs_data_array_enum(
		outputs2,
		[](obs_data_t *o, void *data) {
			auto outputs3 = (obs_data_array_t *)data;
			auto o2 = obs_data_create();
			obs_data_set_string(o2, "name", obs_data_get_string(o, "name"));
			obs_data_set_bool(o2, "vertical", false);
			obs_data_array_push_back(outputs3, o2);
			obs_data_release(o2);
		},
		outputs3);
	obs_data_array_release(outputs2);
	obs_data_set_array(response_data, "outputs", outputs3);
	obs_data_array_release(outputs3);
	obs_data_set_bool(response_data, "success", true);
}

void MultistreamDock::vendor_request_start_output(obs_data_t *request_data, obs_data_t *response_data, void *self)
{
	auto md = (MultistreamDock *)self;
	auto name = obs_data_get_string(request_data, "name");
	if (!name || !strlen(name)) {
		obs_data_set_bool(response_data, "success", false);
		obs_data_set_string(response_data, "error", "Missing name");
		return;
	}
	auto success = false;
	auto sb = &success;
	QMetaObject::invokeMethod(
		md,
		[md, sb, name] {
			for (int i = 1; i < md->mainCanvasOutputLayout->count(); i++) {
				auto item = md->mainCanvasOutputLayout->itemAt(i);
				auto w = item->widget();
				if (!w) {
					continue;
				}
				auto oName = w->objectName().toStdString();
				if (oName == name) {
					auto button = w->findChild<QPushButton *>(QStringLiteral("canvasStream"));
					if (button && !button->isChecked()) {
						QMetaObject::invokeMethod(
							button, [button] { button->click(); }, Qt::QueuedConnection);
					}
					*sb = true;
					return;
				}
			}
		},
		Qt::BlockingQueuedConnection);
	if (success) {
		obs_data_set_bool(response_data, "success", true);
	} else {
		obs_data_set_string(response_data, "error", "Output not found");
		obs_data_set_bool(response_data, "success", false);
	}
}

void MultistreamDock::vendor_request_stop_output(obs_data_t *request_data, obs_data_t *response_data, void *self)
{
	auto md = (MultistreamDock *)self;
	auto name = obs_data_get_string(request_data, "name");
	if (!name || !strlen(name)) {
		obs_data_set_bool(response_data, "success", false);
		obs_data_set_string(response_data, "error", "Missing name");
		return;
	}
	auto success = false;
	auto sb = &success;
	QMetaObject::invokeMethod(
		md,
		[md, sb, name] {
			for (int i = 1; i < md->mainCanvasOutputLayout->count(); i++) {
				auto item = md->mainCanvasOutputLayout->itemAt(i);
				auto w = item->widget();
				if (!w) {
					continue;
				}
				auto oName = w->objectName().toStdString();
				if (oName == name) {
					auto button = w->findChild<QPushButton *>(QStringLiteral("canvasStream"));
					if (button && button->isChecked()) {
						QMetaObject::invokeMethod(
							button, [button] { button->click(); }, Qt::QueuedConnection);
					}
					*sb = true;
					return;
				}
			}
		},
		Qt::BlockingQueuedConnection);
	if (success) {
		obs_data_set_bool(response_data, "success", true);
	} else {
		obs_data_set_string(response_data, "error", "Output not found");
		obs_data_set_bool(response_data, "success", false);
	}
}

AspectRatioPixmapLabel::AspectRatioPixmapLabel(QWidget *parent) : QLabel(parent)
{
	setMinimumSize(1, 1);
	setScaledContents(false);
}

void AspectRatioPixmapLabel::setPixmap(const QPixmap &p)
{
	pix = p;
	QLabel::setPixmap(scaledPixmap());
}

int AspectRatioPixmapLabel::heightForWidth(int width) const
{
	return pix.isNull() ? height() : (pix.height() * width) / pix.width();
}

QSize AspectRatioPixmapLabel::sizeHint() const
{
	int w = width();
	return QSize(w, heightForWidth(w));
}

QPixmap AspectRatioPixmapLabel::scaledPixmap() const
{
	return pix.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void AspectRatioPixmapLabel::resizeEvent(QResizeEvent *e)
{
	UNUSED_PARAMETER(e);
	if (!pix.isNull()) {
		QLabel::setPixmap(scaledPixmap());
	}
}
