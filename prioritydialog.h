#pragma once
#include <QDialog>
#include <QVector>

namespace Ui { class PriorityDialog; }
class QButtonGroup;

class PriorityDialog : public QDialog
{
    Q_OBJECT
public:
    struct Option { int value; QString label; };

    explicit PriorityDialog(QWidget* parent = nullptr);
    ~PriorityDialog() override;

    void setTarget(const QString& title, const QString& currentText);
    void setOptions(const QVector<Option>& options, int currentValue);
    bool hasSelection() const;
    int  selectedValue() const;

    static QVector<Option> processOptions();
    static QVector<Option> threadOptions();

protected:
    void accept() override;

private:
    void updateOkButton();

    Ui::PriorityDialog* ui;
    QButtonGroup* m_group;
    QVector<int> m_values;
};
