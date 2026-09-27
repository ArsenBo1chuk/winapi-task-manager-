#pragma once
#include <QTableWidgetItem>
#include <QColor>
#include <QIcon>
#include "uitypes.h"

class SortItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const QVariant a = data(UiRole::Sort), b = other.data(UiRole::Sort);
        if (a.isValid() || b.isValid()) {
            const double x = a.isValid() ? a.toDouble() : -1e300;
            const double y = b.isValid() ? b.toDouble() : -1e300;
            return x < y;
        }
        return text().localeAwareCompare(other.text()) < 0;
    }
};

namespace Cell {

inline QColor mutedColor() { return QColor(0x88, 0x87, 0x80); }

inline QTableWidgetItem* text(const QString& s, const QString& tip = {}) {
    auto* it = new SortItem(s);
    if (!tip.isEmpty()) it->setToolTip(tip);
    return it;
}

inline QTableWidgetItem* number(double v, int decimals = 1) {
    auto* it = new SortItem(QString::number(v, 'f', decimals));
    it->setData(UiRole::Sort, v);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

inline QTableWidgetItem* integer(qint64 v) {
    auto* it = new SortItem(QString::number(v));
    it->setData(UiRole::Sort, static_cast<double>(v));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

inline QTableWidgetItem* mib(quint64 bytes) {
    return number(static_cast<double>(bytes) / (1024.0 * 1024.0), 1);
}

inline QTableWidgetItem* na(const QString& tip = QStringLiteral("Немає прав або значення недоступне"),
                            bool rightAlign = true) {
    auto* it = new SortItem(QStringLiteral("Н/Д"));
    it->setForeground(mutedColor());
    it->setToolTip(tip);
    if (rightAlign) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

inline QTableWidgetItem* notMeasured(const QString& tip = QStringLiteral("Ще не виміряно")) {
    auto* it = new SortItem(QStringLiteral("—"));
    it->setForeground(mutedColor());
    it->setToolTip(tip);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

inline QTableWidgetItem* state(UiState s, const QString& extra = {}) {
    auto* it = new SortItem(extra.isEmpty() ? stateLabel(s) : stateLabel(s) + QStringLiteral("  ") + extra);
    it->setData(UiRole::State, static_cast<int>(s));
    it->setData(UiRole::Sort, static_cast<double>(static_cast<int>(s)));
    it->setToolTip(stateTooltip(s));
    return it;
}

inline QTableWidgetItem* processName(const QString& name, const QString& fullPath, bool launchedByUs) {
    auto* it = new SortItem(name);
    it->setToolTip(fullPath.isEmpty() ? name : fullPath);
    it->setData(UiRole::OwnFlag, launchedByUs);
    if (launchedByUs) it->setIcon(QIcon(QStringLiteral(":/icons/flag.svg")));
    return it;
}

inline QTableWidgetItem* key(quint32 id, qulonglong createTime) {
    auto* it = integer(id);
    it->setData(UiRole::Key, id);
    it->setData(UiRole::CreateTime, createTime);
    return it;
}

}
