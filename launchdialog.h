#pragma once
#include <QDialog>

namespace Ui { class LaunchDialog; }

class LaunchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LaunchDialog(QWidget* parent = nullptr);
    ~LaunchDialog() override;

    QString exePath() const;
    QString arguments() const;
    QString workingDirectory() const;
    bool    createSuspended() const;
    QString commandLine() const;

    void setExePath(const QString& path);
    void setArguments(const QString& args);

protected:
    void accept() override;

private slots:
    void onBrowseExe();
    void onBrowseDir();
    void updatePreview();

private:
    Ui::LaunchDialog* ui;
};
