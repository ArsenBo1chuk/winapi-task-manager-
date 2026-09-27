#include "launchdialog.h"
#include "ui_launchdialog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPushButton>
#include "uitypes.h"

LaunchDialog::LaunchDialog(QWidget* parent) : QDialog(parent), ui(new Ui::LaunchDialog)
{
    ui->setupUi(this);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Запустити"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    repolish(ui->buttonBox->button(QDialogButtonBox::Ok));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));

    connect(ui->btnBrowseExe, &QPushButton::clicked, this, &LaunchDialog::onBrowseExe);
    connect(ui->btnBrowseDir, &QPushButton::clicked, this, &LaunchDialog::onBrowseDir);
    connect(ui->editExe,  &QLineEdit::textChanged, this, &LaunchDialog::updatePreview);
    connect(ui->editArgs, &QLineEdit::textChanged, this, &LaunchDialog::updatePreview);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LaunchDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &LaunchDialog::reject);
    updatePreview();
}

LaunchDialog::~LaunchDialog() { delete ui; }

QString LaunchDialog::exePath() const          { return QDir::toNativeSeparators(ui->editExe->text().trimmed()); }
QString LaunchDialog::arguments() const        { return ui->editArgs->text().trimmed(); }
QString LaunchDialog::workingDirectory() const { return QDir::toNativeSeparators(ui->editWorkDir->text().trimmed()); }
bool    LaunchDialog::createSuspended() const  { return ui->chkSuspended->isChecked(); }
void    LaunchDialog::setExePath(const QString& p)   { ui->editExe->setText(p); }
void    LaunchDialog::setArguments(const QString& a) { ui->editArgs->setText(a); }

QString LaunchDialog::commandLine() const
{
    QString cmd = QStringLiteral("\"%1\"").arg(exePath());
    if (!arguments().isEmpty()) cmd += QLatin1Char(' ') + arguments();
    return cmd;
}

void LaunchDialog::onBrowseExe()
{
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Оберіть виконуваний файл"),
                                                   ui->editExe->text(), QStringLiteral("Програми (*.exe)"));
    if (f.isEmpty()) return;
    ui->editExe->setText(QDir::toNativeSeparators(f));
    if (ui->editWorkDir->text().isEmpty())
        ui->editWorkDir->setText(QDir::toNativeSeparators(QFileInfo(f).absolutePath()));
}

void LaunchDialog::onBrowseDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Робочий каталог"), ui->editWorkDir->text());
    if (!d.isEmpty()) ui->editWorkDir->setText(QDir::toNativeSeparators(d));
}

void LaunchDialog::updatePreview()
{
    ui->lblError->clear();
    ui->lblCmdPreview->setText(ui->editExe->text().trimmed().isEmpty()
        ? QStringLiteral("Командний рядок: —")
        : QStringLiteral("Командний рядок: %1").arg(commandLine()));
}

void LaunchDialog::accept()
{
    const QFileInfo fi(exePath());
    if (exePath().isEmpty())                           { ui->lblError->setText(QStringLiteral("Вкажіть виконуваний файл.")); return; }
    if (fi.suffix().compare("exe", Qt::CaseInsensitive)) { ui->lblError->setText(QStringLiteral("Оберіть файл із розширенням .exe.")); return; }
    if (!fi.exists())                                  { ui->lblError->setText(QStringLiteral("Файл не знайдено. Перевірте шлях.")); return; }
    if (!workingDirectory().isEmpty() && !QFileInfo(workingDirectory()).isDir())
                                                       { ui->lblError->setText(QStringLiteral("Робочий каталог не існує.")); return; }
    QDialog::accept();
}
