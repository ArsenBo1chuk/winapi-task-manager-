#include "prioritydialog.h"
#include "ui_prioritydialog.h"
#include "uitypes.h"
#include <QButtonGroup>
#include <QRadioButton>
#include <QPushButton>

PriorityDialog::PriorityDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::PriorityDialog), m_group(new QButtonGroup(this))
{
    ui->setupUi(this);
    m_group->setExclusive(true);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Застосувати"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    repolish(ui->buttonBox->button(QDialogButtonBox::Ok));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &PriorityDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_group, &QButtonGroup::buttonToggled, this, [this](QAbstractButton*, bool) { updateOkButton(); });
    updateOkButton();
}

PriorityDialog::~PriorityDialog() { delete ui; }

void PriorityDialog::setTarget(const QString& title, const QString& currentText)
{
    setWindowTitle(title);
    ui->lblTarget->setText(title);
    ui->lblCurrent->setText(QStringLiteral("Поточне значення: %1").arg(currentText));
}

void PriorityDialog::setOptions(const QVector<Option>& options, int currentValue)
{
    const QList<QAbstractButton*> old = m_group->buttons();
    for (QAbstractButton* b : old) { m_group->removeButton(b); delete b; }
    m_values.clear();
    for (int i = 0; i < options.size(); ++i) {
        auto* rb = new QRadioButton(options[i].label, ui->grpOptions);
        m_group->addButton(rb, i);
        m_values.push_back(options[i].value);
        ui->optionsLayout->addWidget(rb);
        if (options[i].value == currentValue) rb->setChecked(true);
    }
    updateOkButton();
    adjustSize();
}

bool PriorityDialog::hasSelection() const
{
    const int id = m_group->checkedId();
    return id >= 0 && id < m_values.size();
}

int PriorityDialog::selectedValue() const
{
    return hasSelection() ? m_values[m_group->checkedId()] : 0;
}

void PriorityDialog::updateOkButton()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(hasSelection());
}

void PriorityDialog::accept()
{
    if (!hasSelection()) return;
    QDialog::accept();
}

QVector<PriorityDialog::Option> PriorityDialog::processOptions()
{
    QVector<Option> v;
    for (int i = 0; i <= static_cast<int>(UiProcPriority::High); ++i)
        v.push_back({i, procPriorityLabel(static_cast<UiProcPriority>(i))});
    return v;
}

QVector<PriorityDialog::Option> PriorityDialog::threadOptions()
{
    QVector<Option> v;
    for (int rel = -2; rel <= 2; ++rel) v.push_back({rel, threadPriorityLabel(rel)});
    return v;
}
