#pragma once
#include <Qt>
#include <QString>
#include <QWidget>
#include <QStyle>

inline void repolish(QWidget* w) { w->style()->unpolish(w); w->style()->polish(w); w->update(); }

namespace UiRole {
    constexpr int Key        = Qt::UserRole + 1;
    constexpr int CreateTime = Qt::UserRole + 2;
    constexpr int State      = Qt::UserRole + 3;
    constexpr int Sort       = Qt::UserRole + 4;
    constexpr int OwnFlag    = Qt::UserRole + 5;
    constexpr int System     = Qt::UserRole + 6;
}

enum ProcCol {
    PC_Pid = 0, PC_Name, PC_Cpu, PC_Priority, PC_State,
    PC_WS, PC_VA, PC_Private, PC_Threads, PC_Owner,
    PC_Count
};

enum ThrCol {
    TC_Job = 0, TC_Tid, TC_Pid, TC_Cpu, TC_RelPriority,
    TC_BasePriority, TC_CpuTime, TC_Affinity, TC_State,
    TC_Count
};

enum class UiState : int {
    Active = 0,
    SuspendedByUs,
    Partial,
    Exited,
    Unknown
};

inline QString stateLabel(UiState s) {
    switch (s) {
    case UiState::Active:        return QStringLiteral("Активний");
    case UiState::SuspendedByUs: return QStringLiteral("Призупинено");
    case UiState::Partial:       return QStringLiteral("Частково");
    case UiState::Exited:        return QStringLiteral("Завершено");
    default:                     return QStringLiteral("Н/Д");
    }
}

inline QString stateTooltip(UiState s) {
    switch (s) {
    case UiState::Active:        return QStringLiteral("Об'єкт ще не завершений. Це не означає, що він зараз виконується на CPU.");
    case UiState::SuspendedByUs: return QStringLiteral("Призупинено цим диспетчером (підтверджено успішну власну операцію).");
    case UiState::Partial:       return QStringLiteral("Частково призупинено: лише частина потоків або перелік змінився під час операції.");
    case UiState::Exited:        return QStringLiteral("HANDLE сигналізований, код завершення прочитано.");
    default:                     return QStringLiteral("Немає прав, об'єкт зник або немає надійної підстави визначити стан.");
    }
}

enum class UiProcPriority : int { Idle = 0, BelowNormal, Normal, AboveNormal, High };

inline QString procPriorityLabel(UiProcPriority p) {
    switch (p) {
    case UiProcPriority::Idle:        return QStringLiteral("Idle");
    case UiProcPriority::BelowNormal: return QStringLiteral("Below normal");
    case UiProcPriority::Normal:      return QStringLiteral("Normal");
    case UiProcPriority::AboveNormal: return QStringLiteral("Above normal");
    default:                          return QStringLiteral("High");
    }
}

inline QString threadPriorityLabel(int rel) {
    switch (rel) {
    case -15: return QStringLiteral("Idle (−15)");
    case -2:  return QStringLiteral("Lowest (−2)");
    case -1:  return QStringLiteral("Below normal (−1)");
    case  0:  return QStringLiteral("Normal (0)");
    case  1:  return QStringLiteral("Above normal (+1)");
    case  2:  return QStringLiteral("Highest (+2)");
    case  15: return QStringLiteral("Time critical (+15)");
    default:  return QString::number(rel);
    }
}
