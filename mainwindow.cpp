#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tableitems.h"
#include "statebadgedelegate.h"
#include "launchdialog.h"
#include "prioritydialog.h"
#include "affinitydialog.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <sddl.h>

#include <QTimer>
#include <QMenu>
#include <QLabel>
#include <QHeaderView>
#include <QScrollBar>
#include <QMessageBox>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QListWidgetItem>

#include <cstddef>
#include <string>
#include <utility>

namespace {

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE h = nullptr) : m_h(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    ~ScopedHandle() { if (m_h) CloseHandle(m_h); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }
private:
    HANDLE m_h;
};

struct VmCountersEx {
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG  PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
};

using NtQueryInformationProcessFn = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using GetThreadDescriptionFn = HRESULT (WINAPI*)(HANDLE, PWSTR*);

const QString kWorkerName = QStringLiteral("MyWorker.exe");

qulonglong toU64(const FILETIME& ft)
{
    return (qulonglong(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

qulonglong nowFileTime()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return toU64(ft);
}

QString fileTimeText(qulonglong ft)
{
    if (ft < 116444736000000000ULL) return QStringLiteral("Н/Д");
    const qint64 ms = qint64((ft - 116444736000000000ULL) / 10000ULL);
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

QString cpuTimeText(qulonglong t)
{
    const qulonglong ms = t / 10000ULL;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000ULL)
        .arg((ms / 60000ULL) % 60ULL, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000ULL) % 60ULL, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000ULL, 3, 10, QLatin1Char('0'));
}

QString maskText(quint64 m)
{
    return QStringLiteral("0x%1").arg(QString::number(m, 16).toUpper());
}

int maskWidth(quint64 m)
{
    int w = 0;
    for (int i = 0; i < 64; ++i)
        if (m & (quint64(1) << i)) w = i + 1;
    return w;
}

bool processTimes(HANDLE h, qulonglong* created, qulonglong* cpu)
{
    FILETIME c, e, k, u;
    if (!GetProcessTimes(h, &c, &e, &k, &u)) return false;
    *created = toU64(c);
    *cpu = toU64(k) + toU64(u);
    return true;
}

bool threadTimes(HANDLE h, qulonglong* created, qulonglong* cpu)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) return false;
    *created = toU64(c);
    *cpu = toU64(k) + toU64(u);
    return true;
}

int priorityRank(DWORD cls)
{
    switch (cls) {
    case IDLE_PRIORITY_CLASS:         return 0;
    case BELOW_NORMAL_PRIORITY_CLASS: return 1;
    case NORMAL_PRIORITY_CLASS:       return 2;
    case ABOVE_NORMAL_PRIORITY_CLASS: return 3;
    case HIGH_PRIORITY_CLASS:         return 4;
    case REALTIME_PRIORITY_CLASS:     return 5;
    default:                          return -1;
    }
}

QString priorityText(DWORD cls)
{
    const int r = priorityRank(cls);
    if (r < 0) return QStringLiteral("Н/Д");
    if (r == 5) return QStringLiteral("Realtime");
    return procPriorityLabel(static_cast<UiProcPriority>(r));
}

DWORD priorityClassFor(int value)
{
    switch (static_cast<UiProcPriority>(value)) {
    case UiProcPriority::Idle:        return IDLE_PRIORITY_CLASS;
    case UiProcPriority::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
    case UiProcPriority::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
    case UiProcPriority::High:        return HIGH_PRIORITY_CLASS;
    default:                          return NORMAL_PRIORITY_CLASS;
    }
}

bool isElevated()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    ScopedHandle t(tok);
    TOKEN_ELEVATION el{};
    DWORD len = 0;
    return GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len) && el.TokenIsElevated;
}

QString imagePath(HANDLE h)
{
    std::wstring buf(32768, L'\0');
    DWORD size = DWORD(buf.size());
    if (!QueryFullProcessImageNameW(h, 0, &buf[0], &size)) return {};
    return QString::fromWCharArray(buf.data(), int(size));
}

bool virtualSize(HANDLE h, quint64* va)
{
    static const auto fn = reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!fn) return false;
    VmCountersEx c{};
    ULONG len = 0;
    if (fn(h, 3, &c, sizeof(c), &len) < 0) {
        c = VmCountersEx{};
        if (fn(h, 3, &c, ULONG(offsetof(VmCountersEx, PrivateUsage)), &len) < 0) return false;
    }
    *va = c.VirtualSize;
    return true;
}

QString threadDescription(HANDLE h)
{
    static const auto fn = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    if (!fn) return {};
    PWSTR s = nullptr;
    if (FAILED(fn(h, &s)) || !s) return {};
    const QString r = QString::fromWCharArray(s);
    LocalFree(s);
    return r.trimmed();
}

HANDLE openProcessChecked(quint32 pid, qulonglong createTime, DWORD access)
{
    HANDLE h = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return nullptr;
    qulonglong c = 0, cpu = 0;
    if (createTime && processTimes(h, &c, &cpu) && c != createTime) {
        CloseHandle(h);
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return h;
}

HANDLE openThreadChecked(quint32 tid, quint32 pid, qulonglong createTime, DWORD access)
{
    HANDLE h = OpenThread(access | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!h) return nullptr;
    qulonglong c = 0, cpu = 0;
    const bool wrongOwner = pid && GetProcessIdOfThread(h) != pid;
    const bool wrongTime = createTime && threadTimes(h, &c, &cpu) && c != createTime;
    if (wrongOwner || wrongTime) {
        CloseHandle(h);
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return h;
}

double cpuPercent(qulonglong prev, qulonglong cur, qulonglong wall, int cpus)
{
    if (!wall || cur < prev) return 0.0;
    const double v = double(cur - prev) * 100.0 / (double(wall) * cpus);
    return v < 0.0 ? 0.0 : (v > 100.0 ? 100.0 : v);
}

QTableWidgetItem* sortedText(const QString& s, double sortValue, bool rightAlign = false)
{
    QTableWidgetItem* it = Cell::text(s);
    it->setData(UiRole::Sort, sortValue);
    if (rightAlign) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_cpuCount = qMax(1, int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    m_isAdmin = isElevated();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(ui->spinInterval->value() * 1000);

    setupTables();
    setupStatusBar();
    setupContextMenus();
    setupConnections();

    ui->mainSplitter->setStretchFactor(0, 5);
    ui->mainSplitter->setStretchFactor(1, 3);
    ui->mainSplitter->setStretchFactor(2, 1);
    ui->mainSplitter->setSizes({440, 260, 110});

    ui->spinInterval->setEnabled(ui->chkAutoRefresh->isChecked());
    ui->threadPane->setVisible(ui->chkShowThreads->isChecked());
    clearProcessHeader();
    setThreadActionsEnabled(false, false);

    onRefresh();

    if (ui->chkAutoRefresh->isChecked()) m_refreshTimer->start();
}

MainWindow::~MainWindow()
{
    for (const LaunchedProcess& lp : std::as_const(m_launched))
        if (lp.handle) CloseHandle(static_cast<HANDLE>(lp.handle));
    delete ui;
}

void MainWindow::setupTables()
{
    auto setup = [](QTableWidget* t) {
        t->verticalHeader()->setDefaultSectionSize(30);
        t->verticalHeader()->setMinimumSectionSize(26);
        t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        t->horizontalHeader()->setSectionsMovable(false);
        t->horizontalHeader()->setSortIndicatorShown(true);
        t->setIconSize(QSize(14, 14));
        t->setMouseTracking(true);
    };
    setup(ui->tblProcesses);
    setup(ui->tblThreads);

    auto* badge = new StateBadgeDelegate(this);
    ui->tblProcesses->setItemDelegateForColumn(PC_State, badge);
    ui->tblThreads->setItemDelegateForColumn(TC_State, badge);

    const int pw[PC_Count - 1] = {70, 220, 80, 120, 170, 95, 95, 130, 90};
    for (int c = 0; c < PC_Count - 1; ++c) ui->tblProcesses->setColumnWidth(c, pw[c]);
    const int tw[TC_Count - 1] = {90, 80, 80, 80, 190, 100, 100, 140};
    for (int c = 0; c < TC_Count - 1; ++c) ui->tblThreads->setColumnWidth(c, tw[c]);

    ui->tblProcesses->sortByColumn(PC_Cpu, Qt::DescendingOrder);
    ui->tblThreads->sortByColumn(TC_Tid, Qt::AscendingOrder);
}

void MainWindow::setupStatusBar()
{
    m_statTotal   = new QLabel(this);
    m_statVisible = new QLabel(this);
    m_statCpus    = new QLabel(this);
    m_statAccess  = new QLabel(this);
    statusBar()->addWidget(m_statTotal);
    statusBar()->addWidget(m_statVisible);
    statusBar()->addWidget(m_statCpus);
    statusBar()->addPermanentWidget(m_statAccess);
    statusBar()->setSizeGripEnabled(true);
    setStatusCounts(0, 0, m_cpuCount, m_isAdmin);
}

void MainWindow::setupContextMenus()
{
    m_procMenu = new QMenu(this);
    m_procMenu->addAction(ui->actionProcPriority);
    m_procMenu->addAction(ui->actionProcAffinity);
    m_procMenu->addSeparator();
    m_procMenu->addAction(ui->actionProcSuspend);
    m_procMenu->addAction(ui->actionProcResume);
    m_procMenu->addSeparator();
    m_procMenu->addAction(ui->actionOnlyThisPid);
    m_procMenu->addAction(ui->actionProcTerminate);

    m_threadMenu = new QMenu(this);
    m_threadMenu->addAction(ui->actionThrPriority);
    m_threadMenu->addAction(ui->actionThrAffinity);
    m_threadMenu->addSeparator();
    m_threadMenu->addAction(ui->actionThrSuspend);
    m_threadMenu->addAction(ui->actionThrResume);
    m_threadMenu->addSeparator();
    m_threadMenu->addAction(ui->actionThrTerminate);
}

void MainWindow::syncCheckWithAction()
{
    auto link = [](QCheckBox* c, QAction* a) {
        a->setChecked(c->isChecked());
        QObject::connect(c, &QCheckBox::toggled, a, &QAction::setChecked);
        QObject::connect(a, &QAction::toggled, c, &QCheckBox::setChecked);
    };
    link(ui->chkHideSystem,   ui->actionHideSystem);
    link(ui->chkOnlySelected, ui->actionOnlySelected);
    link(ui->chkShowThreads,  ui->actionShowThreads);
    link(ui->chkAutoRefresh,  ui->actionAutoRefresh);
}

void MainWindow::setupConnections()
{
    syncCheckWithAction();

    auto btn = [this](QPushButton* b, QAction* a) {
        connect(b, &QPushButton::clicked, a, &QAction::trigger);
        connect(a, &QAction::changed, b, [b, a] { b->setEnabled(a->isEnabled()); });
        b->setEnabled(a->isEnabled());
    };
    btn(ui->btnLaunch,        ui->actionLaunch);
    btn(ui->btnRefresh,       ui->actionRefresh);
    btn(ui->btnResetFilters,  ui->actionResetFilters);
    btn(ui->btnProcPriority,  ui->actionProcPriority);
    btn(ui->btnProcAffinity,  ui->actionProcAffinity);
    btn(ui->btnProcSuspend,   ui->actionProcSuspend);
    btn(ui->btnProcResume,    ui->actionProcResume);
    btn(ui->btnProcTerminate, ui->actionProcTerminate);
    btn(ui->btnThrPriority,   ui->actionThrPriority);
    btn(ui->btnThrAffinity,   ui->actionThrAffinity);
    btn(ui->btnThrSuspend,    ui->actionThrSuspend);
    btn(ui->btnThrResume,     ui->actionThrResume);
    btn(ui->btnThrTerminate,  ui->actionThrTerminate);

    connect(ui->actionLaunch,        &QAction::triggered, this, &MainWindow::onLaunchProcess);
    connect(ui->actionRefresh,       &QAction::triggered, this, &MainWindow::onRefresh);
    connect(ui->actionExit,          &QAction::triggered, this, &QWidget::close);
    connect(ui->actionProcPriority,  &QAction::triggered, this, &MainWindow::onProcessPriority);
    connect(ui->actionProcAffinity,  &QAction::triggered, this, &MainWindow::onProcessAffinity);
    connect(ui->actionProcSuspend,   &QAction::triggered, this, &MainWindow::onProcessSuspend);
    connect(ui->actionProcResume,    &QAction::triggered, this, &MainWindow::onProcessResume);
    connect(ui->actionProcTerminate, &QAction::triggered, this, &MainWindow::onProcessTerminate);
    connect(ui->actionOnlyThisPid,   &QAction::triggered, this, &MainWindow::onOnlyThisPid);
    connect(ui->actionThrPriority,   &QAction::triggered, this, &MainWindow::onThreadPriority);
    connect(ui->actionThrAffinity,   &QAction::triggered, this, &MainWindow::onThreadAffinity);
    connect(ui->actionThrSuspend,    &QAction::triggered, this, &MainWindow::onThreadSuspend);
    connect(ui->actionThrResume,     &QAction::triggered, this, &MainWindow::onThreadResume);
    connect(ui->actionThrTerminate,  &QAction::triggered, this, &MainWindow::onThreadTerminate);
    connect(ui->actionResetFilters,  &QAction::triggered, this, &MainWindow::onResetFilters);
    connect(ui->actionStatesLegend,  &QAction::triggered, this, &MainWindow::onStatesLegend);
    connect(ui->actionAbout,         &QAction::triggered, this, &MainWindow::onAbout);

    connect(ui->chkHideSystem,   &QCheckBox::toggled, this, &MainWindow::onHideSystemToggled);
    connect(ui->chkOnlySelected, &QCheckBox::toggled, this, &MainWindow::onOnlySelectedToggled);
    connect(ui->chkShowThreads,  &QCheckBox::toggled, this, &MainWindow::onShowThreadsToggled);
    connect(ui->chkAutoRefresh,  &QCheckBox::toggled, this, &MainWindow::onAutoRefreshToggled);
    connect(ui->editSearch,      &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(ui->spinInterval, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onIntervalChanged);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onAutoRefreshTick);

    connect(ui->tblProcesses, &QTableWidget::itemSelectionChanged, this, &MainWindow::onProcessSelectionChanged);
    connect(ui->tblThreads,   &QTableWidget::itemSelectionChanged, this, &MainWindow::onThreadSelectionChanged);
    connect(ui->tblProcesses, &QWidget::customContextMenuRequested, this, &MainWindow::onProcessContextMenu);
    connect(ui->tblThreads,   &QWidget::customContextMenuRequested, this, &MainWindow::onThreadContextMenu);

    connect(ui->btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLog);
}

QTableWidget* MainWindow::processTable() const { return ui->tblProcesses; }
QTableWidget* MainWindow::threadTable()  const { return ui->tblThreads; }

void MainWindow::beginTableUpdate(QTableWidget* t)
{
    if (t == ui->tblProcesses) { m_keepPid = selectedPid(); m_keepPidTime = selectedPidCreateTime(); }
    if (t == ui->tblThreads)   { m_keepTid = selectedTid(); m_keepTidTime = selectedTidCreateTime(); }
    t->setProperty("mtmScroll", t->verticalScrollBar()->value());
    t->setUpdatesEnabled(false);
    t->blockSignals(true);
    t->setSortingEnabled(false);
}

void MainWindow::endTableUpdate(QTableWidget* t)
{
    t->setSortingEnabled(true);
    if (t == ui->tblProcesses && m_keepPid) selectRowByKey(t, m_keepPid, m_keepPidTime);
    if (t == ui->tblThreads   && m_keepTid) selectRowByKey(t, m_keepTid, m_keepTidTime);
    t->doItemsLayout();
    t->verticalScrollBar()->setValue(t->property("mtmScroll").toInt());
    t->blockSignals(false);
    t->setUpdatesEnabled(true);
}

int MainWindow::selectedRow(QTableWidget* t) const
{
    const auto rows = t->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

quint32 MainWindow::keyOfSelectedRow(QTableWidget* t, int role) const
{
    const int r = selectedRow(t);
    if (r < 0) return 0;
    const QTableWidgetItem* it = t->item(r, 0);
    return it ? it->data(role).toUInt() : 0;
}

qulonglong MainWindow::createTimeOfSelectedRow(QTableWidget* t) const
{
    const int r = selectedRow(t);
    if (r < 0) return 0;
    const QTableWidgetItem* it = t->item(r, 0);
    return it ? it->data(UiRole::CreateTime).toULongLong() : 0;
}

void MainWindow::selectRowByKey(QTableWidget* t, quint32 key, qulonglong createTime)
{
    for (int r = 0; r < t->rowCount(); ++r) {
        const QTableWidgetItem* it = t->item(r, 0);
        if (!it || it->data(UiRole::Key).toUInt() != key) continue;
        if (createTime && it->data(UiRole::CreateTime).toULongLong() != createTime) continue;
        t->selectRow(r);
        return;
    }
}

quint32 MainWindow::selectedPid() const { return keyOfSelectedRow(ui->tblProcesses, UiRole::Key); }
quint32 MainWindow::selectedTid() const { return keyOfSelectedRow(ui->tblThreads, UiRole::Key); }
qulonglong MainWindow::selectedPidCreateTime() const { return createTimeOfSelectedRow(ui->tblProcesses); }
qulonglong MainWindow::selectedTidCreateTime() const { return createTimeOfSelectedRow(ui->tblThreads); }

void MainWindow::selectPid(quint32 pid) { selectRowByKey(ui->tblProcesses, pid); }
void MainWindow::selectTid(quint32 tid) { selectRowByKey(ui->tblThreads, tid); }

QString MainWindow::selectedProcessName() const
{
    const int r = selectedRow(ui->tblProcesses);
    const QTableWidgetItem* it = r < 0 ? nullptr : ui->tblProcesses->item(r, PC_Name);
    return it ? it->text() : QString();
}

bool MainWindow::selectedProcessExited() const
{
    const int r = selectedRow(ui->tblProcesses);
    const QTableWidgetItem* it = r < 0 ? nullptr : ui->tblProcesses->item(r, PC_State);
    return it && it->data(UiRole::State).toInt() == int(UiState::Exited);
}

bool MainWindow::selectedThreadExited() const
{
    const int r = selectedRow(ui->tblThreads);
    const QTableWidgetItem* it = r < 0 ? nullptr : ui->tblThreads->item(r, TC_State);
    return it && it->data(UiRole::State).toInt() == int(UiState::Exited);
}

bool MainWindow::isOwnWorker() const
{
    const int r = selectedRow(ui->tblProcesses);
    const QTableWidgetItem* it = r < 0 ? nullptr : ui->tblProcesses->item(r, PC_Name);
    return it && it->data(UiRole::OwnFlag).toBool()
           && it->text().compare(kWorkerName, Qt::CaseInsensitive) == 0;
}

bool MainWindow::isLaunchedByUs(quint32 pid, qulonglong createTime) const
{
    const auto it = m_launched.constFind(pid);
    return it != m_launched.constEnd() && createTime && it->createTime == createTime;
}

void MainWindow::setProcessHeader(const QString& name, quint32 pid)
{
    ui->lblProcTitle->setText(QStringLiteral("%1  ·  PID %2").arg(name).arg(pid));
    setProcessActionsEnabled(true);
}

void MainWindow::clearProcessHeader()
{
    ui->lblProcTitle->setText(QStringLiteral("Процес не обрано"));
    setProcessDetails(QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"));
    setProcessActionsEnabled(false);
}

void MainWindow::setProcessDetails(const QString& path, const QString& created,
                                   const QString& mask, const QString& extra)
{
    ui->lblProcPath->setText(QStringLiteral("Шлях: %1").arg(path));
    ui->lblProcCreated->setText(QStringLiteral("Створено: %1").arg(created));
    ui->lblProcMask->setText(QStringLiteral("Маска процесу: %1").arg(mask));
    ui->lblProcHandle->setText(extra);
}

void MainWindow::setThreadHeader(const QString& processName, quint32 pid, quint32 tid)
{
    if (!pid) { ui->lblThrTitle->setText(QStringLiteral("Потоки")); return; }
    QString t = QStringLiteral("Потоки %1  ·  PID %2").arg(processName).arg(pid);
    if (tid) t += QStringLiteral("  ·  обрано TID %1").arg(tid);
    ui->lblThrTitle->setText(t);
}

void MainWindow::setProcessActionsEnabled(bool on)
{
    for (QAction* a : {ui->actionProcPriority, ui->actionProcAffinity, ui->actionProcSuspend,
                       ui->actionProcResume, ui->actionProcTerminate, ui->actionOnlyThisPid})
        a->setEnabled(on);
}

void MainWindow::setThreadActionsEnabled(bool on, bool terminateAllowed)
{
    for (QAction* a : {ui->actionThrPriority, ui->actionThrAffinity,
                       ui->actionThrSuspend, ui->actionThrResume})
        a->setEnabled(on);
    ui->actionThrTerminate->setEnabled(on && terminateAllowed);
}

void MainWindow::appendLog(const QString& text, bool ok, quint32 errorCode)
{
    QString line = QStringLiteral("%1   %2   %3")
                       .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text,
                            ok ? QStringLiteral("успішно") : QStringLiteral("помилка"));
    if (!ok && errorCode) line += QStringLiteral(" (код %1)").arg(errorCode);
    auto* it = new QListWidgetItem(line);
    it->setForeground(ok ? QColor("#2C2C2A") : QColor("#A32D2D"));
    ui->lstLog->insertItem(0, it);
}

void MainWindow::setLastUpdate(const QString& text) { ui->lblLastUpdate->setText(text); }

void MainWindow::setStatusCounts(int total, int visible, int cpus, bool isAdmin)
{
    m_statTotal->setText(QStringLiteral("Процесів: %1").arg(total));
    m_statVisible->setText(QStringLiteral("Видимих: %1").arg(visible));
    m_statCpus->setText(QStringLiteral("Логічних CPU: %1").arg(cpus));
    m_statAccess->setText(isAdmin ? QStringLiteral("Доступ: адміністратор")
                                  : QStringLiteral("Доступ: користувач  ·  Н/Д = немає прав"));
}

bool MainWindow::confirmTerminateProcess(const QString& name, quint32 pid)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Завершити процес — PID %1, %2").arg(pid).arg(name));
    box.setText(QStringLiteral("Примусово завершити <b>%1</b> (PID %2)?").arg(name.toHtmlEscaped()).arg(pid));
    box.setInformativeText(QStringLiteral("Буде викликано TerminateProcess. Незбережені дані процесу буде втрачено."));
    QPushButton* kill = box.addButton(QStringLiteral("Завершити"), QMessageBox::DestructiveRole);
    kill->setProperty("danger", true);
    repolish(kill);
    QPushButton* cancel = box.addButton(QStringLiteral("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == kill;
}

bool MainWindow::confirmTerminateThread(const QString& processName, quint32 pid, quint32 tid)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Завершити потік — TID %1, %2 (PID %3)").arg(tid).arg(processName).arg(pid));
    box.setText(QStringLiteral("Завершити потік TID %1 через TerminateThread?").arg(tid));
    box.setInformativeText(QStringLiteral("Демонстраційна дія. Дозволена лише для власного одноразового MyWorker.exe."));
    QPushButton* kill = box.addButton(QStringLiteral("Завершити потік"), QMessageBox::DestructiveRole);
    kill->setProperty("danger", true);
    repolish(kill);
    QPushButton* cancel = box.addButton(QStringLiteral("Скасувати"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == kill;
}

QString MainWindow::ownerName(void* hProcess, bool* isSystem)
{
    *isSystem = false;
    HANDLE tok = nullptr;
    if (!OpenProcessToken(static_cast<HANDLE>(hProcess), TOKEN_QUERY, &tok)) return {};
    ScopedHandle t(tok);

    DWORD len = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    if (!len) return {};
    QByteArray buf(int(len), '\0');
    if (!GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) return {};
    PSID sid = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;

    *isSystem = IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinLocalServiceSid)
                || IsWellKnownSid(sid, WinNetworkServiceSid);

    QString key;
    LPWSTR sidText = nullptr;
    if (ConvertSidToStringSidW(sid, &sidText)) {
        key = QString::fromWCharArray(sidText);
        LocalFree(sidText);
    }
    if (!key.isEmpty()) {
        const auto cached = m_accountCache.constFind(key);
        if (cached != m_accountCache.constEnd()) return *cached;
    }

    wchar_t name[256];
    wchar_t domain[256];
    DWORD nameLen = 256, domainLen = 256;
    SID_NAME_USE use;
    const QString result = LookupAccountSidW(nullptr, sid, name, &nameLen, domain, &domainLen, &use)
                               ? QString::fromWCharArray(name, int(nameLen))
                               : key;
    if (!key.isEmpty()) m_accountCache.insert(key, result);
    return result;
}

void MainWindow::pollLaunchedProcesses()
{
    for (auto it = m_launched.begin(); it != m_launched.end(); ++it) {
        if (it->exited || !it->handle) continue;
        HANDLE h = static_cast<HANDLE>(it->handle);
        if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) continue;
        DWORD code = 0;
        GetExitCodeProcess(h, &code);
        it->exited = true;
        it->exitCode = code;
        appendLog(QStringLiteral("Процес %1 (PID %2) завершився, код %3").arg(it->name).arg(it.key()).arg(code), true);
    }
}

void MainWindow::purgeSuspendRecords(const QHash<quint32, quint32>& liveThreads)
{
    for (auto it = m_suspendedThreads.begin(); it != m_suspendedThreads.end();) {
        const auto live = liveThreads.constFind(it.key());
        if (live == liveThreads.constEnd() || live.value() != it->pid || it->count <= 0)
            it = m_suspendedThreads.erase(it);
        else
            ++it;
    }
}

void MainWindow::refreshProcessTable()
{
    pollLaunchedProcesses();

    ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0));
    if (!snap) {
        appendLog(QStringLiteral("CreateToolhelp32Snapshot: не вдалося отримати список процесів"), false, GetLastError());
        return;
    }

    QHash<quint32, quint32> liveThreads;
    QHash<quint32, int> threadsPerPid;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap.get(), &te)) {
        do {
            liveThreads.insert(te.th32ThreadID, te.th32OwnerProcessID);
            ++threadsPerPid[te.th32OwnerProcessID];
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap.get(), &te));
    }
    purgeSuspendRecords(liveThreads);

    QHash<quint32, int> ourSuspended;
    for (const SuspendRecord& rec : std::as_const(m_suspendedThreads))
        if (rec.count > 0) ++ourSuspended[rec.pid];

    const qulonglong now = nowFileTime();
    const qulonglong wall = m_procSampleTime && now > m_procSampleTime ? now - m_procSampleTime : 0;
    QHash<quint32, CpuSample> samples;

    QTableWidget* t = ui->tblProcesses;
    beginTableUpdate(t);
    t->setRowCount(0);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap.get(), &pe)) {
        do {
            const quint32 pid = pe.th32ProcessID;
            const QString name = QString::fromWCharArray(pe.szExeFile);

            HANDLE raw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!raw) raw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            ScopedHandle h(raw);

            qulonglong created = 0, cpu = 0;
            const bool haveTimes = h && processTimes(h.get(), &created, &cpu);
            const bool own = isLaunchedByUs(pid, created);
            const QString path = h ? imagePath(h.get()) : QString();

            const int row = t->rowCount();
            t->insertRow(row);

            QTableWidgetItem* keyItem = Cell::key(pid, created);
            t->setItem(row, PC_Pid, keyItem);
            t->setItem(row, PC_Name, Cell::processName(name, path, own));

            if (haveTimes) {
                const auto prev = m_procSamples.constFind(pid);
                if (wall && prev != m_procSamples.constEnd() && prev->createTime == created)
                    t->setItem(row, PC_Cpu, Cell::number(cpuPercent(prev->cpuTime, cpu, wall, m_cpuCount)));
                else
                    t->setItem(row, PC_Cpu, Cell::notMeasured());
                samples.insert(pid, {created, cpu});
            } else {
                t->setItem(row, PC_Cpu, Cell::na());
            }

            const DWORD cls = h ? GetPriorityClass(h.get()) : 0;
            if (cls) t->setItem(row, PC_Priority, sortedText(priorityText(cls), priorityRank(cls)));
            else     t->setItem(row, PC_Priority, Cell::na(QStringLiteral("Немає прав або значення недоступне"), false));

            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            const bool haveMem = h && GetProcessMemoryInfo(h.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            quint64 va = 0;
            const bool haveVa = h && virtualSize(h.get(), &va);
            t->setItem(row, PC_WS,      haveMem ? Cell::mib(pmc.WorkingSetSize) : Cell::na());
            t->setItem(row, PC_VA,      haveVa  ? Cell::mib(va)                 : Cell::na());
            t->setItem(row, PC_Private, haveMem ? Cell::mib(pmc.PrivateUsage)   : Cell::na());
            t->setItem(row, PC_Threads, Cell::integer(pe.cntThreads));

            bool systemOwner = false;
            const QString owner = h ? ownerName(h.get(), &systemOwner) : QString();
            t->setItem(row, PC_Owner, owner.isEmpty() ? Cell::na(QStringLiteral("Немає прав або значення недоступне"), false)
                                                      : Cell::text(owner));

            DWORD session = 0;
            const bool sessionZero = ProcessIdToSessionId(pid, &session) && session == 0;
            const bool system = pid == 0 || pid == 4 || systemOwner || (owner.isEmpty() && sessionZero);
            keyItem->setData(UiRole::System, system);

            DWORD code = STILL_ACTIVE;
            if (!h) {
                t->setItem(row, PC_State, Cell::state(UiState::Unknown));
            } else if (GetExitCodeProcess(h.get(), &code) && code != STILL_ACTIVE) {
                t->setItem(row, PC_State, Cell::state(UiState::Exited, QStringLiteral("код %1").arg(code)));
            } else {
                const int total = threadsPerPid.value(pid);
                const int ours = ourSuspended.value(pid);
                if (ours > 0 && ours >= total)
                    t->setItem(row, PC_State, Cell::state(UiState::SuspendedByUs));
                else if (ours > 0)
                    t->setItem(row, PC_State, Cell::state(UiState::Partial, QStringLiteral("%1/%2").arg(ours).arg(total)));
                else
                    t->setItem(row, PC_State, Cell::state(UiState::Active));
            }

            pe.dwSize = sizeof(pe);
        } while (Process32NextW(snap.get(), &pe));
    }

    for (auto it = m_launched.constBegin(); it != m_launched.constEnd(); ++it) {
        if (!it->exited) continue;
        const int row = t->rowCount();
        t->insertRow(row);
        QTableWidgetItem* keyItem = Cell::key(it.key(), it->createTime);
        keyItem->setData(UiRole::System, false);
        t->setItem(row, PC_Pid, keyItem);
        t->setItem(row, PC_Name, Cell::processName(it->name, it->path, true));
        t->setItem(row, PC_Cpu, Cell::notMeasured(QStringLiteral("Процес завершено")));
        t->setItem(row, PC_Priority, Cell::notMeasured(QStringLiteral("Процес завершено")));
        t->setItem(row, PC_State, Cell::state(UiState::Exited, QStringLiteral("код %1").arg(it->exitCode)));
        t->setItem(row, PC_WS, Cell::notMeasured(QStringLiteral("Процес завершено")));
        t->setItem(row, PC_VA, Cell::notMeasured(QStringLiteral("Процес завершено")));
        t->setItem(row, PC_Private, Cell::notMeasured(QStringLiteral("Процес завершено")));
        t->setItem(row, PC_Threads, Cell::integer(0));
        t->setItem(row, PC_Owner, Cell::text(QStringLiteral("—")));
    }

    m_procSamples = samples;
    m_procSampleTime = now;

    endTableUpdate(t);
    m_totalProcesses = t->rowCount();
    setLastUpdate(QStringLiteral("Оновлено %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
}

void MainWindow::refreshThreadTable(quint32 pid)
{
    if (pid != m_thrSamplePid) {
        m_thrSamples.clear();
        m_thrSampleTime = 0;
        m_thrSamplePid = pid;
    }

    const qulonglong now = nowFileTime();
    const qulonglong wall = m_thrSampleTime && now > m_thrSampleTime ? now - m_thrSampleTime : 0;
    QHash<quint32, CpuSample> samples;

    QTableWidget* t = ui->tblThreads;
    beginTableUpdate(t);
    t->setRowCount(0);

    ScopedHandle snap(pid ? CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) : nullptr);
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (snap && Thread32First(snap.get(), &te)) {
        do {
            if (te.th32OwnerProcessID != pid) { te.dwSize = sizeof(te); continue; }
            const quint32 tid = te.th32ThreadID;
            ScopedHandle h(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid));

            qulonglong created = 0, cpu = 0;
            const bool haveTimes = h && threadTimes(h.get(), &created, &cpu);
            const QString job = h ? threadDescription(h.get()) : QString();

            const int row = t->rowCount();
            t->insertRow(row);

            QTableWidgetItem* jobItem = Cell::text(job.isEmpty() ? QStringLiteral("—") : job,
                                                   job.isEmpty() ? QStringLiteral("Потік не має опису (SetThreadDescription)") : job);
            jobItem->setData(UiRole::Key, tid);
            jobItem->setData(UiRole::CreateTime, created);
            t->setItem(row, TC_Job, jobItem);
            t->setItem(row, TC_Tid, Cell::integer(tid));
            t->setItem(row, TC_Pid, Cell::integer(pid));

            if (haveTimes) {
                const auto prev = m_thrSamples.constFind(tid);
                if (wall && prev != m_thrSamples.constEnd() && prev->createTime == created)
                    t->setItem(row, TC_Cpu, Cell::number(cpuPercent(prev->cpuTime, cpu, wall, m_cpuCount)));
                else
                    t->setItem(row, TC_Cpu, Cell::notMeasured());
                samples.insert(tid, {created, cpu});
                t->setItem(row, TC_CpuTime, sortedText(cpuTimeText(cpu), double(cpu), true));
            } else {
                t->setItem(row, TC_Cpu, Cell::na());
                t->setItem(row, TC_CpuTime, Cell::na());
            }

            const int rel = h ? GetThreadPriority(h.get()) : THREAD_PRIORITY_ERROR_RETURN;
            if (rel != THREAD_PRIORITY_ERROR_RETURN) t->setItem(row, TC_RelPriority, sortedText(threadPriorityLabel(rel), rel));
            else                                     t->setItem(row, TC_RelPriority, Cell::na(QStringLiteral("Немає прав або значення недоступне"), false));

            t->setItem(row, TC_BasePriority, Cell::integer(te.tpBasePri));

            GROUP_AFFINITY ga{};
            if (h && GetThreadGroupAffinity(h.get(), &ga))
                t->setItem(row, TC_Affinity, sortedText(maskText(quint64(ga.Mask)), double(quint64(ga.Mask))));
            else
                t->setItem(row, TC_Affinity, Cell::na(QStringLiteral("Немає прав або значення недоступне"), false));

            const auto rec = m_suspendedThreads.constFind(tid);
            const int ours = rec != m_suspendedThreads.constEnd() && rec->pid == pid ? rec->count : 0;
            DWORD code = STILL_ACTIVE;
            if (h && GetExitCodeThread(h.get(), &code) && code != STILL_ACTIVE)
                t->setItem(row, TC_State, Cell::state(UiState::Exited, QStringLiteral("код %1").arg(code)));
            else if (ours > 0)
                t->setItem(row, TC_State, Cell::state(UiState::SuspendedByUs, ours > 1 ? QStringLiteral("×%1").arg(ours) : QString()));
            else if (!h)
                t->setItem(row, TC_State, Cell::state(UiState::Unknown));
            else
                t->setItem(row, TC_State, Cell::state(UiState::Active));

            te.dwSize = sizeof(te);
        } while (Thread32Next(snap.get(), &te));
    }

    m_thrSamples = samples;
    m_thrSampleTime = now;
    endTableUpdate(t);
}

void MainWindow::updateSelectedProcess()
{
    const int r = selectedRow(ui->tblProcesses);
    if (r < 0) {
        clearProcessHeader();
        refreshThreadTable(0);
        onThreadSelectionChanged();
        return;
    }

    const quint32 pid = selectedPid();
    const qulonglong created = selectedPidCreateTime();
    const QString name = selectedProcessName();
    const bool own = isLaunchedByUs(pid, created);
    const bool exited = selectedProcessExited();

    setProcessHeader(name, pid);
    if (exited) {
        setProcessActionsEnabled(false);
        ui->actionOnlyThisPid->setEnabled(true);
    }

    QStringList extra;
    if (own) extra << QStringLiteral("Запущено цим диспетчером");
    const auto launched = m_launched.constFind(pid);
    if (own && launched->exited) extra << QStringLiteral("Код завершення: %1").arg(launched->exitCode);

    ScopedHandle h(exited ? nullptr : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (h) {
        QString path = imagePath(h.get());
        if (path.isEmpty()) path = QStringLiteral("Н/Д");
        DWORD_PTR pm = 0, sm = 0;
        const QString mask = GetProcessAffinityMask(h.get(), &pm, &sm) ? maskText(quint64(pm)) : QStringLiteral("Н/Д");
        extra << QStringLiteral("Пріоритет: %1").arg(priorityText(GetPriorityClass(h.get())));
        setProcessDetails(path, fileTimeText(created), mask, extra.join(QStringLiteral("  ·  ")));
    } else if (own) {
        setProcessDetails(launched->path, fileTimeText(created), QStringLiteral("—"), extra.join(QStringLiteral("  ·  ")));
    } else {
        setProcessDetails(QStringLiteral("Н/Д"), fileTimeText(created), QStringLiteral("Н/Д"), QStringLiteral("Немає доступу до процесу"));
    }

    if (ui->chkShowThreads->isChecked()) refreshThreadTable(exited ? 0 : pid);
    onThreadSelectionChanged();
}

void MainWindow::onLaunchProcess()
{
    LaunchDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString exe = dlg.exePath();
    const std::wstring app = exe.toStdWString();
    std::wstring cmd = dlg.commandLine().toStdWString();
    const std::wstring dir = dlg.workingDirectory().toStdWString();
    const bool suspended = dlg.createSuspended();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NEW_CONSOLE;
    if (suspended) flags |= CREATE_SUSPENDED;

    if (!CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        appendLog(QStringLiteral("CreateProcessW: %1").arg(exe), false, GetLastError());
        return;
    }

    const quint32 pid = pi.dwProcessId;
    qulonglong created = 0, cpu = 0;
    processTimes(pi.hProcess, &created, &cpu);

    const auto old = m_launched.constFind(pid);
    if (old != m_launched.constEnd() && old->handle) CloseHandle(static_cast<HANDLE>(old->handle));

    LaunchedProcess lp;
    lp.handle = pi.hProcess;
    lp.createTime = created;
    lp.name = QFileInfo(exe).fileName();
    lp.path = exe;
    m_launched.insert(pid, lp);

    if (suspended) {
        qulonglong tc = 0, tcpu = 0;
        threadTimes(pi.hThread, &tc, &tcpu);
        m_suspendedThreads.insert(pi.dwThreadId, {pid, tc, 1});
    }
    CloseHandle(pi.hThread);

    appendLog(QStringLiteral("CreateProcessW: %1 (PID %2%3)")
                  .arg(exe).arg(pid)
                  .arg(suspended ? QStringLiteral(", CREATE_SUSPENDED, TID %1").arg(pi.dwThreadId) : QString()),
              true);

    onRefresh();
    selectRowByKey(ui->tblProcesses, pid, created);
}

void MainWindow::onRefresh()
{
    if (m_refreshing) return;
    m_refreshing = true;
    refreshProcessTable();
    applyProcessFilters();
    updateSelectedProcess();
    m_refreshing = false;
}

void MainWindow::onAutoRefreshToggled(bool on)
{
    ui->spinInterval->setEnabled(on);
    if (on) m_refreshTimer->start(); else m_refreshTimer->stop();
}

void MainWindow::onIntervalChanged(int seconds) { m_refreshTimer->setInterval(seconds * 1000); }

void MainWindow::onAutoRefreshTick()
{
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
    onRefresh();
}

void MainWindow::onHideSystemToggled(bool)           { applyProcessFilters(); }
void MainWindow::onOnlySelectedToggled(bool)         { applyProcessFilters(); }
void MainWindow::onSearchTextChanged(const QString&) { applyProcessFilters(); }

void MainWindow::onShowThreadsToggled(bool on)
{
    ui->threadPane->setVisible(on);
    if (on && !m_refreshing) updateSelectedProcess();
}

void MainWindow::onResetFilters()
{
    ui->chkHideSystem->setChecked(false);
    ui->chkOnlySelected->setChecked(false);
    ui->chkShowThreads->setChecked(true);
    ui->editSearch->clear();
}

void MainWindow::onProcessSelectionChanged()
{
    if (m_refreshing) return;
    if (ui->chkOnlySelected->isChecked()) applyProcessFilters();
    updateSelectedProcess();
}

void MainWindow::onThreadSelectionChanged()
{
    const quint32 tid = selectedTid();
    setThreadHeader(selectedProcessName(), selectedPid(), tid);
    const bool enabled = tid != 0 && !selectedThreadExited() && !selectedProcessExited();
    setThreadActionsEnabled(enabled, enabled && isOwnWorker());
}

void MainWindow::onProcessContextMenu(const QPoint& pos)
{
    if (QTableWidgetItem* it = ui->tblProcesses->itemAt(pos)) {
        ui->tblProcesses->selectRow(it->row());
        m_procMenu->exec(ui->tblProcesses->viewport()->mapToGlobal(pos));
    }
}

void MainWindow::onThreadContextMenu(const QPoint& pos)
{
    if (QTableWidgetItem* it = ui->tblThreads->itemAt(pos)) {
        ui->tblThreads->selectRow(it->row());
        m_threadMenu->exec(ui->tblThreads->viewport()->mapToGlobal(pos));
    }
}

void MainWindow::onProcessPriority()
{
    const quint32 pid = selectedPid();
    if (!pid) return;
    const qulonglong created = selectedPidCreateTime();
    const QString name = selectedProcessName();

    ScopedHandle h(openProcessChecked(pid, created, PROCESS_SET_INFORMATION));
    if (!h) {
        appendLog(QStringLiteral("OpenProcess PID %1 для зміни пріоритету").arg(pid), false, GetLastError());
        return;
    }

    const DWORD before = GetPriorityClass(h.get());
    PriorityDialog dlg(this);
    dlg.setTarget(QStringLiteral("Пріоритет процесу — PID %1, %2").arg(pid).arg(name), priorityText(before));
    dlg.setOptions(PriorityDialog::processOptions(), priorityRank(before));
    if (dlg.exec() != QDialog::Accepted) return;

    const DWORD target = priorityClassFor(dlg.selectedValue());
    if (!SetPriorityClass(h.get(), target)) {
        appendLog(QStringLiteral("SetPriorityClass PID %1 → %2").arg(pid).arg(priorityText(target)), false, GetLastError());
        return;
    }

    const DWORD after = GetPriorityClass(h.get());
    appendLog(QStringLiteral("SetPriorityClass PID %1: %2 → %3").arg(pid).arg(priorityText(before), priorityText(after)),
              after == target);
    onRefresh();
}

void MainWindow::onProcessAffinity()
{
    const quint32 pid = selectedPid();
    if (!pid) return;
    const qulonglong created = selectedPidCreateTime();
    const QString name = selectedProcessName();

    ScopedHandle h(openProcessChecked(pid, created, PROCESS_SET_INFORMATION));
    if (!h) {
        appendLog(QStringLiteral("OpenProcess PID %1 для зміни affinity").arg(pid), false, GetLastError());
        return;
    }

    DWORD_PTR pm = 0, sm = 0;
    if (!GetProcessAffinityMask(h.get(), &pm, &sm)) {
        appendLog(QStringLiteral("GetProcessAffinityMask PID %1").arg(pid), false, GetLastError());
        return;
    }

    AffinityDialog dlg(this);
    dlg.setTarget(QStringLiteral("Відповідність CPU — процес PID %1, %2").arg(pid).arg(name));
    dlg.setup(maskWidth(quint64(sm)), quint64(sm), quint64(pm));
    if (dlg.exec() != QDialog::Accepted) return;

    const quint64 target = dlg.mask();
    if (!SetProcessAffinityMask(h.get(), DWORD_PTR(target))) {
        appendLog(QStringLiteral("SetProcessAffinityMask PID %1 → %2").arg(pid).arg(maskText(target)), false, GetLastError());
        return;
    }

    DWORD_PTR after = 0;
    GetProcessAffinityMask(h.get(), &after, &sm);
    appendLog(QStringLiteral("SetProcessAffinityMask PID %1: %2 → %3").arg(pid).arg(maskText(pm), maskText(after)),
              quint64(after) == target);
    onRefresh();
}

void MainWindow::onProcessSuspend()
{
    const quint32 pid = selectedPid();
    if (!pid) return;
    if (pid == GetCurrentProcessId()) {
        appendLog(QStringLiteral("Диспетчер не може призупинити сам себе"), false);
        return;
    }

    ScopedHandle p(openProcessChecked(pid, selectedPidCreateTime(), 0));
    if (!p) {
        appendLog(QStringLiteral("OpenProcess PID %1 для призупинення").arg(pid), false, GetLastError());
        return;
    }

    ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snap) {
        appendLog(QStringLiteral("CreateToolhelp32Snapshot для потоків PID %1").arg(pid), false, GetLastError());
        return;
    }

    int ok = 0, failed = 0;
    DWORD lastError = 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap.get(), &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                ScopedHandle th(OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID));
                qulonglong tc = 0, tcpu = 0;
                if (!th) {
                    ++failed;
                    lastError = GetLastError();
                } else if (SuspendThread(th.get()) == DWORD(-1)) {
                    ++failed;
                    lastError = GetLastError();
                } else {
                    threadTimes(th.get(), &tc, &tcpu);
                    SuspendRecord& rec = m_suspendedThreads[te.th32ThreadID];
                    if (rec.pid != pid || rec.createTime != tc) rec = {pid, tc, 0};
                    ++rec.count;
                    ++ok;
                }
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap.get(), &te));
    }

    appendLog(QStringLiteral("SuspendThread для PID %1: %2 з %3 потоків").arg(pid).arg(ok).arg(ok + failed),
              ok > 0 && failed == 0, failed ? lastError : 0);
    onRefresh();
}

int MainWindow::resumeOwnSuspensions(quint32 pid, quint32 onlyTid, bool all, int* failed, quint32* lastError)
{
    int resumed = 0;
    *failed = 0;
    *lastError = 0;
    const QList<quint32> tids = m_suspendedThreads.keys();
    for (quint32 tid : tids) {
        if (onlyTid && tid != onlyTid) continue;
        SuspendRecord rec = m_suspendedThreads.value(tid);
        if (rec.pid != pid || rec.count <= 0) continue;

        ScopedHandle th(openThreadChecked(tid, pid, rec.createTime, THREAD_SUSPEND_RESUME));
        if (!th) {
            const DWORD err = GetLastError();
            if (err == ERROR_INVALID_PARAMETER) m_suspendedThreads.remove(tid);
            ++*failed;
            *lastError = err;
            continue;
        }

        const int times = all ? rec.count : 1;
        int done = 0;
        for (int i = 0; i < times; ++i) {
            if (ResumeThread(th.get()) == DWORD(-1)) {
                ++*failed;
                *lastError = GetLastError();
                break;
            }
            ++done;
        }
        rec.count -= done;
        if (rec.count <= 0) m_suspendedThreads.remove(tid);
        else                m_suspendedThreads.insert(tid, rec);
        if (done > 0) ++resumed;
    }
    return resumed;
}

void MainWindow::onProcessResume()
{
    const quint32 pid = selectedPid();
    if (!pid) return;

    int failed = 0;
    quint32 lastError = 0;
    const int resumed = resumeOwnSuspensions(pid, 0, true, &failed, &lastError);
    if (!resumed && !failed) {
        appendLog(QStringLiteral("PID %1 не має потоків, призупинених цим диспетчером").arg(pid), false);
        return;
    }
    appendLog(QStringLiteral("ResumeThread для PID %1: відновлено потоків %2").arg(pid).arg(resumed),
              failed == 0, lastError);
    onRefresh();
}

void MainWindow::onProcessTerminate()
{
    const quint32 pid = selectedPid();
    if (!pid) return;
    const qulonglong created = selectedPidCreateTime();
    const QString name = selectedProcessName();

    if (pid == GetCurrentProcessId()) {
        appendLog(QStringLiteral("Для завершення диспетчера використайте Файл → Вихід"), false);
        return;
    }
    if (!confirmTerminateProcess(name, pid)) return;

    ScopedHandle h(openProcessChecked(pid, created, PROCESS_TERMINATE | SYNCHRONIZE));
    if (!h) {
        appendLog(QStringLiteral("OpenProcess PID %1 для завершення").arg(pid), false, GetLastError());
        return;
    }
    if (!TerminateProcess(h.get(), 1)) {
        appendLog(QStringLiteral("TerminateProcess PID %1").arg(pid), false, GetLastError());
        return;
    }
    appendLog(QStringLiteral("TerminateProcess PID %1 (%2)").arg(pid).arg(name), true);

    if (WaitForSingleObject(h.get(), 3000) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(h.get(), &code);
        const auto it = m_launched.find(pid);
        if (it != m_launched.end() && it->createTime == created && !it->exited) {
            it->exited = true;
            it->exitCode = code;
        }
        appendLog(QStringLiteral("PID %1 завершено, код %2").arg(pid).arg(code), true);
    } else {
        appendLog(QStringLiteral("PID %1 ще не підтверджено як завершений").arg(pid), false);
    }

    for (auto it = m_suspendedThreads.begin(); it != m_suspendedThreads.end();) {
        if (it->pid == pid) it = m_suspendedThreads.erase(it);
        else ++it;
    }
    onRefresh();
}

void MainWindow::onOnlyThisPid()
{
    ui->chkOnlySelected->setChecked(true);
    applyProcessFilters();
}

void MainWindow::onThreadPriority()
{
    const quint32 tid = selectedTid();
    const quint32 pid = selectedPid();
    if (!tid || !pid) return;

    ScopedHandle h(openThreadChecked(tid, pid, selectedTidCreateTime(), THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION));
    if (!h) {
        appendLog(QStringLiteral("OpenThread TID %1 для зміни пріоритету").arg(tid), false, GetLastError());
        return;
    }

    const int before = GetThreadPriority(h.get());
    PriorityDialog dlg(this);
    dlg.setTarget(QStringLiteral("Пріоритет потоку — TID %1, PID %2").arg(tid).arg(pid),
                  before == THREAD_PRIORITY_ERROR_RETURN ? QStringLiteral("Н/Д") : threadPriorityLabel(before));
    dlg.setOptions(PriorityDialog::threadOptions(), before);
    if (dlg.exec() != QDialog::Accepted) return;

    const int target = dlg.selectedValue();
    if (!SetThreadPriority(h.get(), target)) {
        appendLog(QStringLiteral("SetThreadPriority TID %1 → %2").arg(tid).arg(threadPriorityLabel(target)), false, GetLastError());
        return;
    }

    const int after = GetThreadPriority(h.get());
    appendLog(QStringLiteral("SetThreadPriority TID %1: %2 → %3").arg(tid)
                  .arg(before == THREAD_PRIORITY_ERROR_RETURN ? QStringLiteral("Н/Д") : threadPriorityLabel(before),
                       threadPriorityLabel(after)),
              after == target);
    onRefresh();
}

void MainWindow::onThreadAffinity()
{
    const quint32 tid = selectedTid();
    const quint32 pid = selectedPid();
    if (!tid || !pid) return;

    ScopedHandle p(openProcessChecked(pid, selectedPidCreateTime(), 0));
    DWORD_PTR pm = 0, sm = 0;
    if (!p || !GetProcessAffinityMask(p.get(), &pm, &sm)) {
        appendLog(QStringLiteral("GetProcessAffinityMask PID %1").arg(pid), false, GetLastError());
        return;
    }

    ScopedHandle th(openThreadChecked(tid, pid, selectedTidCreateTime(), THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION));
    if (!th) {
        appendLog(QStringLiteral("OpenThread TID %1 для зміни affinity").arg(tid), false, GetLastError());
        return;
    }

    GROUP_AFFINITY ga{};
    const quint64 before = GetThreadGroupAffinity(th.get(), &ga) ? quint64(ga.Mask) : quint64(pm);

    AffinityDialog dlg(this);
    dlg.setTarget(QStringLiteral("Відповідність CPU — потік TID %1, PID %2").arg(tid).arg(pid));
    dlg.setup(maskWidth(quint64(sm)), quint64(pm), before);
    if (dlg.exec() != QDialog::Accepted) return;

    const quint64 target = dlg.mask();
    if (SetThreadAffinityMask(th.get(), DWORD_PTR(target)) == 0) {
        appendLog(QStringLiteral("SetThreadAffinityMask TID %1 → %2").arg(tid).arg(maskText(target)), false, GetLastError());
        return;
    }

    GROUP_AFFINITY now{};
    const quint64 after = GetThreadGroupAffinity(th.get(), &now) ? quint64(now.Mask) : target;
    appendLog(QStringLiteral("SetThreadAffinityMask TID %1: %2 → %3").arg(tid).arg(maskText(before), maskText(after)),
              after == target);
    onRefresh();
}

void MainWindow::onThreadSuspend()
{
    const quint32 tid = selectedTid();
    const quint32 pid = selectedPid();
    if (!tid || !pid) return;
    if (pid == GetCurrentProcessId()) {
        appendLog(QStringLiteral("Диспетчер не може призупиняти власні потоки"), false);
        return;
    }

    const qulonglong created = selectedTidCreateTime();
    ScopedHandle th(openThreadChecked(tid, pid, created, THREAD_SUSPEND_RESUME));
    if (!th) {
        appendLog(QStringLiteral("OpenThread TID %1 для призупинення").arg(tid), false, GetLastError());
        return;
    }

    const DWORD prev = SuspendThread(th.get());
    if (prev == DWORD(-1)) {
        appendLog(QStringLiteral("SuspendThread TID %1").arg(tid), false, GetLastError());
        return;
    }

    SuspendRecord& rec = m_suspendedThreads[tid];
    if (rec.pid != pid || rec.createTime != created) rec = {pid, created, 0};
    ++rec.count;

    appendLog(QStringLiteral("SuspendThread TID %1 (лічильник призупинень %2)").arg(tid).arg(prev + 1), true);
    onRefresh();
}

void MainWindow::onThreadResume()
{
    const quint32 tid = selectedTid();
    const quint32 pid = selectedPid();
    if (!tid || !pid) return;

    const auto rec = m_suspendedThreads.constFind(tid);
    if (rec == m_suspendedThreads.constEnd() || rec->pid != pid || rec->count <= 0) {
        appendLog(QStringLiteral("TID %1 не призупинено цим диспетчером").arg(tid), false);
        return;
    }

    int failed = 0;
    quint32 lastError = 0;
    const int resumed = resumeOwnSuspensions(pid, tid, false, &failed, &lastError);
    appendLog(QStringLiteral("ResumeThread TID %1").arg(tid), resumed > 0 && failed == 0, lastError);
    onRefresh();
}

void MainWindow::onThreadTerminate()
{
    const quint32 tid = selectedTid();
    const quint32 pid = selectedPid();
    if (!tid || !pid) return;
    const QString name = selectedProcessName();
    const qulonglong created = selectedTidCreateTime();

    if (!isOwnWorker()) {
        appendLog(QStringLiteral("TerminateThread дозволено лише для MyWorker.exe, запущеного цим диспетчером"), false);
        return;
    }
    if (!confirmTerminateThread(name, pid, tid)) return;

    ScopedHandle th(openThreadChecked(tid, pid, created, THREAD_TERMINATE | SYNCHRONIZE));
    if (!th) {
        appendLog(QStringLiteral("OpenThread TID %1 для завершення").arg(tid), false, GetLastError());
        return;
    }
    if (!TerminateThread(th.get(), 1)) {
        appendLog(QStringLiteral("TerminateThread TID %1").arg(tid), false, GetLastError());
        return;
    }
    appendLog(QStringLiteral("TerminateThread TID %1").arg(tid), true);

    if (WaitForSingleObject(th.get(), 2000) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeThread(th.get(), &code);
        appendLog(QStringLiteral("TID %1 завершено, код %2").arg(tid).arg(code), true);
    } else {
        appendLog(QStringLiteral("TID %1 ще не підтверджено як завершений").arg(tid), false);
    }

    m_suspendedThreads.remove(tid);
    onRefresh();
}

void MainWindow::onStatesLegend()
{
    QString html = QStringLiteral("<table cellpadding='4'>");
    for (UiState s : {UiState::Active, UiState::SuspendedByUs, UiState::Partial, UiState::Exited, UiState::Unknown})
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(stateLabel(s), stateTooltip(s));
    html += QStringLiteral("</table><p>Прапорець біля назви — процес запущено цим диспетчером.</p>");
    QMessageBox::information(this, QStringLiteral("Позначення станів"), html);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("Про програму"),
        QStringLiteral("<b>MyTaskManager</b><br>Лабораторна робота №2: створення, моніторинг і керування "
                       "процесами та потоками засобами Windows API."));
}

void MainWindow::onClearLog() { ui->lstLog->clear(); }

void MainWindow::applyProcessFilters()
{
    QTableWidget* t = ui->tblProcesses;
    const bool hideSystem = ui->chkHideSystem->isChecked();
    const bool onlySelected = ui->chkOnlySelected->isChecked();
    const QString search = ui->editSearch->text().trimmed();
    const quint32 pid = selectedPid();
    const qulonglong created = selectedPidCreateTime();

    int visible = 0;
    for (int row = 0; row < t->rowCount(); ++row) {
        const QTableWidgetItem* keyItem = t->item(row, PC_Pid);
        const QTableWidgetItem* nameItem = t->item(row, PC_Name);
        if (!keyItem) continue;
        const quint32 rowPid = keyItem->data(UiRole::Key).toUInt();

        bool show = true;
        if (onlySelected && pid) {
            show = rowPid == pid && (!created || keyItem->data(UiRole::CreateTime).toULongLong() == created);
        } else {
            if (hideSystem && keyItem->data(UiRole::System).toBool()) show = false;
            if (show && !search.isEmpty()) {
                const QString name = nameItem ? nameItem->text() : QString();
                show = name.contains(search, Qt::CaseInsensitive) || QString::number(rowPid).startsWith(search);
            }
        }

        t->setRowHidden(row, !show);
        if (show) ++visible;
    }
    setStatusCounts(m_totalProcesses, visible, m_cpuCount, m_isAdmin);
}
