#include "statebadgedelegate.h"
#include "uitypes.h"
#include <QPainter>
#include <QApplication>

namespace {
struct Pal { QColor bg, fg; };
Pal paletteFor(UiState s) {
    switch (s) {
    case UiState::Active:        return { QColor("#EAF3DE"), QColor("#3B6D11") };
    case UiState::SuspendedByUs: return { QColor("#FAEEDA"), QColor("#854F0B") };
    case UiState::Partial:       return { QColor("#FCEBEB"), QColor("#A32D2D") };
    case UiState::Exited:        return { QColor("#F1EFE8"), QColor("#5F5E5A") };
    default:                     return { QColor("#F1EFE8"), QColor("#888780") };
    }
}
}

void StateBadgeDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& idx) const
{
    const QVariant v = idx.data(UiRole::State);
    if (!v.isValid()) { QStyledItemDelegate::paint(p, option, idx); return; }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, idx);
    const QString full = opt.text;
    opt.text.clear();
    const QWidget* w = opt.widget;
    QStyle* style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

    const UiState s = static_cast<UiState>(v.toInt());
    const Pal pal = paletteFor(s);
    const QString label = stateLabel(s);
    const QString extra = full.mid(label.size()).trimmed();

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont f = opt.font;
    if (f.pointSizeF() > 1.5) f.setPointSizeF(f.pointSizeF() - 0.5);
    p->setFont(f);
    const QFontMetrics fm(f);

    const int padX = 8, h = fm.height() + 4;
    const int wBadge = fm.horizontalAdvance(label) + padX * 2;
    QRect r(opt.rect.left() + 6, opt.rect.center().y() - h / 2, wBadge, h);
    p->setPen(Qt::NoPen);
    p->setBrush(pal.bg);
    p->drawRoundedRect(r, h / 2.0, h / 2.0);
    p->setPen(pal.fg);
    p->drawText(r, Qt::AlignCenter, label);

    if (!extra.isEmpty()) {
        QRect er(r.right() + 8, opt.rect.top(), opt.rect.right() - r.right() - 8, opt.rect.height());
        p->setPen(QColor("#5F5E5A"));
        p->drawText(er, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(extra, Qt::ElideRight, er.width()));
    }
    p->restore();
}

QSize StateBadgeDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    s.setWidth(s.width() + 24);
    return s;
}
