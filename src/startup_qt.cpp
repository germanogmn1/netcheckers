#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QDesktopWidget>
#include <QDialog>

#include "ui_startupwindow.h"
#include "ui_startuploadingdialog.h"

#include "../src/startup.h"

class StartupWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit StartupWindow(QWidget *parent = 0) : QMainWindow(parent),
        ui(new Ui::StartupWindow),
        dialog_ui(new Ui::StartupLoadingDialog),
        dialog(new QDialog(0,0)),
        timer(new QTimer(this))
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
        qDebug("Start!");

        timer->start(50);
        dialog->show();
    }
    void cancelLoading() {
        qDebug("Cancel loading!");
        timer->stop();
    }
    void pollNetwork() {
        qDebug("poll");
    }
private:
    Ui::StartupWindow *ui;
    Ui::StartupLoadingDialog *dialog_ui;
    QDialog *dialog;
    QTimer *timer;
};

// int main(int argc, char *argv[])

extern "C"
startup_info_t startup(int argc, char **argv) {
    startup_info_t result = {};

    QApplication a(argc, argv);
    StartupWindow w;
    w.show();

    while (w.isVisible()) {
        a.processEvents();
    }

    return result;
}

#include "moc_startup_qt.cpp"
