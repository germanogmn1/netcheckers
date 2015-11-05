#include "startuploadingdialog.h"
#include "ui_startuploadingdialog.h"

StartupLoadingDialog::StartupLoadingDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StartupLoadingDialog)
{
    ui->setupUi(this);
}

StartupLoadingDialog::~StartupLoadingDialog()
{
    delete ui;
}
