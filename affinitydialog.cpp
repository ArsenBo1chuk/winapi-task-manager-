#include "affinitydialog.h"
#include "ui_affinitydialog.h"
#include <QCheckBox>
#include <QPushButton>
#include "uitypes.h"

AffinityDialog::AffinityDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AffinityDialog)
{
    ui->setupUi(this);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Застосувати"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    repolish(ui->buttonBox->button(QDialogButtonBox::Ok));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &AffinityDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->btnSelectAll, &QPushButton::clicked, this, [this] {
        for (QCheckBox* b : m_boxes) if (b->isEnabled()) b->setChecked(true);
    });
    connect(ui->btnSelectNone, &QPushButton::clicked, this, [this] {
        for (QCheckBox* b : m_boxes) b->setChecked(false);
    });
}

AffinityDialog::~AffinityDialog() { delete ui; }

void AffinityDialog::setTarget(const QString& title)
{
    setWindowTitle(title);
    ui->lblTarget->setText(title);
}

QString AffinityDialog::maskToText(quint64 m)
{
    QStringList parts;
    for (int i = 0; i < 64; ++i) if (m & (quint64(1) << i)) parts << QString::number(i);
    return parts.isEmpty() ? QStringLiteral("—") : QStringLiteral("CPU ") + parts.join(QStringLiteral(", "));
}

void AffinityDialog::setup(int cpuCount, quint64 allowedMask, quint64 currentMask)
{
    qDeleteAll(m_boxes);
    m_boxes.clear();
    m_allowed = allowedMask;
    const int cols = cpuCount <= 8 ? 4 : 8;
    for (int i = 0; i < cpuCount && i < 64; ++i) {
        const quint64 bit = quint64(1) << i;
        auto* cb = new QCheckBox(QStringLiteral("CPU %1").arg(i), ui->grpCpus);
        cb->setEnabled((allowedMask & bit) != 0);
        cb->setChecked((currentMask & bit) != 0);
        if (!cb->isEnabled()) cb->setToolTip(QStringLiteral("Не входить у дозволену маску"));
        connect(cb, &QCheckBox::toggled, this, &AffinityDialog::updateMaskLabel);
        ui->cpuGrid->addWidget(cb, i / cols, i % cols);
        m_boxes.push_back(cb);
    }
    ui->lblAllowed->setText(QStringLiteral("Дозволено: %1  (0x%2)")
                                .arg(maskToText(allowedMask), QString::number(allowedMask, 16).toUpper()));
    updateMaskLabel();
    adjustSize();
}

quint64 AffinityDialog::mask() const
{
    quint64 m = 0;
    for (int i = 0; i < m_boxes.size(); ++i) if (m_boxes[i]->isChecked()) m |= quint64(1) << i;
    return m;
}

void AffinityDialog::updateMaskLabel()
{
    ui->lblError->clear();
    ui->lblMaskHex->setText(QStringLiteral("Маска: 0x%1").arg(QString::number(mask(), 16).toUpper()));
}

void AffinityDialog::accept()
{
    const quint64 m = mask();
    if (m == 0)             { ui->lblError->setText(QStringLiteral("Маска не може бути нульовою. Оберіть хоча б одне ядро.")); return; }
    if (m & ~m_allowed)     { ui->lblError->setText(QStringLiteral("Маска виходить за межі дозволеної.")); return; }
    QDialog::accept();
}
