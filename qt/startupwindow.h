#ifndef STARTUPWINDOW_H
#define STARTUPWINDOW_H

#include <QMainWindow>

namespace Ui {
class StartupWindow;
class StartupLoadingDialog;
}

class StartupWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit StartupWindow(QWidget *parent = 0);
    ~StartupWindow();

private slots:
    void on_modeComboBox_currentIndexChanged(int index);
    void on_startButton_clicked();

    void cancelLoading();
    void pollNetwork();

private:
    Ui::StartupWindow *ui;
    Ui::StartupLoadingDialog *dialog_ui;
    QDialog *dialog;
    QTimer *timer;
};

#endif // STARTUPWINDOW_H
