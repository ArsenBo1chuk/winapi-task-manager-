#pragma once
#include <QDialog>
#include <QVector>

namespace Ui { class AffinityDialog; }
class QCheckBox;

class AffinityDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AffinityDialog(QWidget* parent = nullptr);
    ~AffinityDialog() override;

    void setTarget(const QString& title);
    void setup(int cpuCount, quint64 allowedMask, quint64 currentMask);
    quint64 mask() const;

protected:
    void accept() override;

private slots:
    void updateMaskLabel();

private:
    static QString maskToText(quint64 m);
    Ui::AffinityDialog* ui;
    QVector<QCheckBox*> m_boxes;
    quint64 m_allowed = 0;
};
