#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QDesktopWidget>
#include <QDialog>
#include <QLocale>
#include <QTranslator>
#include <QTextCodec>
#include <QMessageBox>
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
		dialog(new QDialog(0, 0)),
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
		net_start(info->network, info->net_mode, info->host, info->port);

		QString loadingText;
		if (info->net_mode == NET_SERVER)
			loadingText = QString(tr("Waiting for connections on port %1...")).arg(info->port);
		else
			loadingText = QString(tr("Connecting on %1 at port %2...")).arg(info->host).arg(info->port);

		dialog_ui->label->setText(loadingText);
		dialog->show();
		timer->start(50);
	}
	void cancelLoading() {
		net_stop(info->network);
	}
	void pollNetwork() {
		net_state_t state = net_get_state(info->network);
		if (state != NET_CONNECTING) {
			timer->stop();
			dialog->hide();
			if (state == NET_RUNNING) {
				info->success = true;
				close();
			} else if (state == NET_ERROR) {
				info->success = false;
				QString message = "NO MESSAGE";
				switch (net_get_error(info->network)) {
					case NET_ECONNREFUSED:
						message = tr("Connection refused");
						break;
					case NET_EDNSFAIL:
						message = tr("Host %1 could not be found").arg(info->host);
						break;
					case NET_EPORTINUSE:
						message = tr("The port %1 is alredy in use").arg(info->port);
						break;
					case NET_EPORTNOACCESS:
						message = tr("No permission to use the port %1").arg(info->port);
						break;
					case NET_EUNKNOWN:
						fprintf(stderr, "%s\n", net_error_str(info->network));
						message = tr("Unknown error") + QString(":\n\n%1").arg(net_error_str(info->network));
						break;
					case NET_ENONE: // should be impossible
						break;
				}
				QMessageBox::critical(this, tr("Failed to connect"), message);
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
	startup_info_t result = {0};
	result.network = net_init();

	QApplication app(argc, argv);

	QString locale = QLocale::system().name();
	QTranslator translator;
	translator.load(QString("netcheckers_") + locale);
	app.installTranslator(&translator);

	StartupWindow win(&result);
	win.show();

	while (win.isVisible()) {
		app.processEvents();
	}

	strcpy(result.assets_path, "assets");

	return result;
}

#include "moc_startup_qt.cpp"
