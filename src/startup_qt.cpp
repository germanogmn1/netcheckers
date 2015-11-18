#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QDesktopWidget>
#include <QDialog>
#include <stdio.h>

#include "ui_startupwindow.h"
#include "ui_startuploadingdialog.h"

#include "../src/startup.h"

class StartupWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit StartupWindow(startup_info_t *info, QWidget *parent = 0) : QMainWindow(parent),
		ui(new Ui::StartupWindow),
		dialog_ui(new Ui::StartupLoadingDialog),
		dialog(new QDialog(0,0)),
		timer(new QTimer(this)),
		info(info)
	{
		ui->setupUi(this);
		dialog_ui->setupUi(dialog);
		dialog->setWindowModality(Qt::ApplicationModal);
		QObject::connect(dialog, SIGNAL(rejected()), this, SLOT(cancelLoading()));
		connect(timer, SIGNAL(timeout()), this, SLOT(pollNetwork()));
		on_modeComboBox_currentIndexChanged(ui->modeComboBox->currentIndex());

		this->move(QApplication::desktop()->screen()->rect().center() - this->rect().center());
	}
	~StartupWindow() {
		delete ui;
		delete dialog_ui;
		delete dialog;
		if (timer)
			delete timer;
	}
private slots:
	void on_modeComboBox_currentIndexChanged(int index) {
		bool disable = (index == 0);
		ui->hostLineEdit->setDisabled(disable);
		ui->hostLabel->setDisabled(disable);
	}
	void on_startButton_clicked() {
		info->net_mode = (ui->modeComboBox->currentIndex() == 0) ? NET_SERVER : NET_CLIENT;
		strncpy(info->host, ui->hostLineEdit->text().toUtf8().data(), sizeof(info->host));
		snprintf(info->port, sizeof(info->port), "%d", ui->portSpinBox->value());
printf("mode: %d\n", info->net_mode);
		net_start(info->network, info->net_mode, info->host, info->port);

		timer->start(50);
		dialog->show();
	}
	void cancelLoading() {
		net_stop(info->network);
	}
	void pollNetwork() {
		net_state_t state = net_get_state(info->network);
		if (state != NET_CONNECTING) {
			printf("state: %d\n", state);
			timer->stop();
			dialog->hide();
			if (state == NET_RUNNING) {
				info->success = true;
				close();
			} else if (state == NET_ERROR) {
				info->success = false;
				switch (net_get_error(info->network)) {
					case NET_EUNKNOWN:
						printf("ERROR: NET_EUNKNOWN '%s'\n", net_error_str(info->network));
						break;
					case NET_ENONE:
						printf("ERROR: NET_ENONE '%s'\n", net_error_str(info->network));
						break;
					case NET_EPORTINUSE:
						printf("ERROR: NET_EPORTINUSE '%s'\n", net_error_str(info->network));
						break;
					case NET_EPORTNOACCESS:
						printf("ERROR: NET_EPORTNOACCESS '%s'\n", net_error_str(info->network));
						break;
				}
			}
		}
	}
private:
	Ui::StartupWindow *ui;
	Ui::StartupLoadingDialog *dialog_ui;
	QDialog *dialog;
	QTimer *timer;
	startup_info_t *info;
};

extern "C"
startup_info_t startup(int argc, char **argv) {
	startup_info_t result = {};
	result.network = net_init();

	QApplication a(argc, argv);
	StartupWindow w(&result);
	w.show();

	while (w.isVisible()) {
		a.processEvents();
	}

	strcpy(result.assets_path, "assets");

	return result;
}

#include "moc_startup_qt.cpp"
