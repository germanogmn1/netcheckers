#include "startupwindow.h"
#include "ui_startupwindow.h"
#include "ui_startuploadingdialog.h"
#include "qtimer.h"

StartupWindow::StartupWindow(QWidget *parent) :
    QMainWindow(parent),
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
}

StartupWindow::~StartupWindow()
{
    delete ui;
    delete dialog_ui;
    delete dialog;
    if (timer)
        delete timer;
}

void StartupWindow::on_modeComboBox_currentIndexChanged(int index)
{
    bool disable = (index == 0);
    ui->hostLineEdit->setDisabled(disable);
    ui->hostLabel->setDisabled(disable);
}

void StartupWindow::on_startButton_clicked()
{
    qDebug("Start!");

    timer->start(50);
    dialog->show();
}

void StartupWindow::cancelLoading()
{
    qDebug("Cancel loading!");
    timer->stop();
}

void StartupWindow::pollNetwork()
{
    qDebug("poll");
}
