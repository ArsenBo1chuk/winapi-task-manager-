#pragma once
#include <QMainWindow>
#include <QHash>
#include <QSet>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QTimer;
class QMenu;
class QLabel;
class QTableWidget;
class QTableWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    QTableWidget* processTable() const;
    QTableWidget* threadTable() const;

    void beginTableUpdate(QTableWidget* t);
    void endTableUpdate(QTableWidget* t);

    quint32 selectedPid() const;
    quint32 selectedTid() const;
    qulonglong selectedPidCreateTime() const;
    qulonglong selectedTidCreateTime() const;
    void selectPid(quint32 pid);
    void selectTid(quint32 tid);

    void setProcessHeader(const QString& name, quint32 pid);
    void clearProcessHeader();
    void setProcessDetails(const QString& path, const QString& created,
                           const QString& mask, const QString& extra = {});
    void setThreadHeader(const QString& processName, quint32 pid, quint32 tid);

    void setProcessActionsEnabled(bool enabled);
    void setThreadActionsEnabled(bool enabled, bool terminateAllowed);

    void appendLog(const QString& text, bool ok, quint32 errorCode = 0);

    void setLastUpdate(const QString& text);
    void setStatusCounts(int total, int visible, int logicalCpus, bool isAdmin);

    bool confirmTerminateProcess(const QString& name, quint32 pid);
    bool confirmTerminateThread(const QString& processName, quint32 pid, quint32 tid);

private slots:
    void onLaunchProcess();
    void onRefresh();
    void onAutoRefreshToggled(bool on);
    void onIntervalChanged(int seconds);
    void onAutoRefreshTick();

    void onHideSystemToggled(bool on);
    void onOnlySelectedToggled(bool on);
    void onShowThreadsToggled(bool on);
    void onSearchTextChanged(const QString& text);
    void onResetFilters();

    void onProcessSelectionChanged();
    void onThreadSelectionChanged();
    void onProcessContextMenu(const QPoint& pos);
    void onThreadContextMenu(const QPoint& pos);

    void onProcessPriority();
    void onProcessAffinity();
    void onProcessSuspend();
    void onProcessResume();
    void onProcessTerminate();
    void onOnlyThisPid();

    void onThreadPriority();
    void onThreadAffinity();
    void onThreadSuspend();
    void onThreadResume();
    void onThreadTerminate();

    void onStatesLegend();
    void onAbout();
    void onClearLog();

private:
    struct LaunchedProcess {
        void* handle = nullptr;
        qulonglong createTime = 0;
        QString name;
        QString path;
        bool exited = false;
        quint32 exitCode = 0;
    };
    struct SuspendRecord {
        quint32 pid = 0;
        qulonglong createTime = 0;
        int count = 0;
    };
    struct CpuSample {
        qulonglong createTime = 0;
        qulonglong cpuTime = 0;
    };

    void applyProcessFilters();
    void setupTables();
    void setupStatusBar();
    void setupContextMenus();
    void setupConnections();
    void syncCheckWithAction();
    quint32 keyOfSelectedRow(QTableWidget* t, int role) const;
    qulonglong createTimeOfSelectedRow(QTableWidget* t) const;
    void selectRowByKey(QTableWidget* t, quint32 key, qulonglong createTime = 0);
    int selectedRow(QTableWidget* t) const;

    void refreshProcessTable();
    void refreshThreadTable(quint32 pid);
    void updateSelectedProcess();
    void pollLaunchedProcesses();
    void purgeSuspendRecords(const QHash<quint32, quint32>& liveThreads);
    bool isLaunchedByUs(quint32 pid, qulonglong createTime) const;
    bool selectedProcessExited() const;
    bool selectedThreadExited() const;
    bool isOwnWorker() const;
    QString selectedProcessName() const;
    QString ownerName(void* hProcess, bool* isSystem);
    int resumeOwnSuspensions(quint32 pid, quint32 onlyTid, bool all, int* failed, quint32* lastError);

    Ui::MainWindow* ui;
    QTimer* m_refreshTimer = nullptr;
    QMenu*  m_procMenu = nullptr;
    QMenu*  m_threadMenu = nullptr;
    QLabel* m_statTotal = nullptr;
    QLabel* m_statVisible = nullptr;
    QLabel* m_statCpus = nullptr;
    QLabel* m_statAccess = nullptr;
    quint32 m_keepPid = 0;
    quint32 m_keepTid = 0;
    qulonglong m_keepPidTime = 0;
    qulonglong m_keepTidTime = 0;
    int m_cpuCount = 1;
    bool m_isAdmin = false;
    int m_totalProcesses = 0;
    bool m_refreshing = false;

    QHash<quint32, SuspendRecord> m_suspendedThreads;
    QHash<quint32, LaunchedProcess> m_launched;
    QHash<QString, QString> m_accountCache;
    QHash<quint32, CpuSample> m_procSamples;
    qulonglong m_procSampleTime = 0;
    QHash<quint32, CpuSample> m_thrSamples;
    qulonglong m_thrSampleTime = 0;
    quint32 m_thrSamplePid = 0;
};
