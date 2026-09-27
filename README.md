# WinAPI-Task-Manager

**WinAPI-Task-Manager** is a lightweight process and thread manager for Windows. It is written in C++ with a Qt user interface, and every system operation is performed directly through the **Windows API**, with no `tasklist`, `taskkill`, PowerShell, WMI, or other high-level wrappers.

It lets you see what every process and thread is doing and control them: change priority, restrict them to specific CPU cores, suspend and resume them, launch new processes, and terminate existing ones.


---

## Features

### Process monitoring
| Column | Meaning |
|---|---|
| **PID / Name** | Process ID and executable name; the full path is shown in the details bar |
| **CPU, %** | CPU load, normalized to the number of logical processors |
| **Priority** | Priority class: Idle, Below normal, Normal, Above normal, High |
| **State** | Active, Suspended, Partially suspended, Exited, or N/A |
| **WS, MiB** | Working set: pages currently in physical memory, including shared ones |
| **VA, MiB** | Virtual address space in use (committed + reserved regions) |
| **Private, MiB** | Private committed memory (same as *Private Bytes* in Process Explorer) |
| **Threads / Owner** | Thread count and the account the process runs under |

### Thread monitoring
For the selected process: TID, CPU %, relative priority (−2…+2), base priority level, total CPU time, affinity mask, and state.

### Control
- **Launch** any `.exe` with arguments and a working directory, optionally in suspended mode (`CREATE_SUSPENDED`).
- **Priority**: change the process priority class or the relative priority of a single thread.
- **Affinity**: choose which CPU cores a process or thread is allowed to run on.
- **Suspend / Resume**: pause a whole process or individual threads.
- **Terminate**: force-close a process after confirmation.

### Usability
- Sort by any column (numbers are sorted as numbers, text as text).
- Filters: *Hide system processes*, *Selected PID only*, and search by name or PID.
- The selected process stays selected after refreshing and sorting.
- Auto-refresh with a configurable interval (5 s by default).
- Operation log showing the time, action, target PID/TID, result, and error code.
- The same actions are available from the toolbar, the top menu, and the right-click context menu.

---

## How it works

### CPU usage
CPU load is calculated from two consecutive measurements of kernel and user time:

```
CPU% = 100 × Δ(kernel + user time) / (Δt × number of logical CPUs)
```

On an 8-core machine, one fully loaded thread therefore shows about **12.5 %**, not 100 %. This matches how Windows Task Manager and Process Explorer report CPU usage.

### Process states
Windows does not provide a single "running / suspended" flag for a process, so ProcScope shows the process lifecycle and the result of its own actions:

| State | Meaning |
|---|---|
| **Active** | The process has not exited (it is not necessarily running on a CPU right now) |
| **Suspended** | All threads were suspended by ProcScope |
| **Partial** | Only some threads were suspended, or the thread list changed during the operation |
| **Exited** | The process has finished; its exit code is shown |
| **N/A** | No access rights, or the process no longer exists |

### Safe suspend and resume
Each thread in Windows has a suspend counter. ProcScope remembers which threads **it** suspended:
- pressing *Suspend* twice does not suspend a thread twice;
- *Resume* releases only ProcScope's own suspension and never forces the counter to zero, so it does not interfere with other programs.

### System processes
The *Hide system processes* filter hides processes owned by **LocalSystem**, **LocalService**, or **NetworkService**, as well as PID 0 and PID 4. Processes with an unknown owner stay visible.

---

## Windows API used

| Task | Functions |
|---|---|
| Listing processes and threads | `CreateToolhelp32Snapshot`, `Process32FirstW/NextW`, `Thread32First/Next` |
| Opening objects | `OpenProcess`, `OpenThread`, `CloseHandle` |
| CPU time | `GetProcessTimes`, `GetThreadTimes`, `QueryPerformanceCounter`, `QueryPerformanceFrequency` |
| Memory | `GetProcessMemoryInfo`, `VirtualQueryEx` |
| Priority | `GetPriorityClass`, `SetPriorityClass`, `GetThreadPriority`, `SetThreadPriority` |
| Affinity | `GetProcessAffinityMask`, `SetProcessAffinityMask`, `GetThreadGroupAffinity`, `SetThreadAffinityMask` |
| Suspend / resume | `SuspendThread`, `ResumeThread` |
| Launch and termination | `CreateProcessW`, `TerminateProcess`, `WaitForSingleObject`, `GetExitCodeProcess` |
| Owner and path | `OpenProcessToken`, `GetTokenInformation`, `QueryFullProcessImageNameW` |
| Errors | `GetLastError`, `FormatMessageW` |

---

## Requirements

- Windows 10 or 11, x64
- Qt 6 with the **Widgets** and **Svg** modules (tested with Qt 6.11.1, MSVC 2022 64-bit)
- Visual Studio 2022 (MSVC compiler)

## Build

1. Clone the repository:
   ```
   git clone https://github.com/<your-username>/procscope.git
   ```
2. Open the project in **Qt Creator** (`CMakeLists.txt` or the `.pro` file).
3. Select the **Desktop Qt 6.x MSVC2022 64bit** kit.
4. Press **Build** (`Ctrl+B`), then **Run** (`Ctrl+R`).

To run the built `.exe` on another computer, collect the required Qt libraries next to it:
```
windeployqt MyTaskManager.exe
```

## Usage

1. Start the application. The process table fills in automatically.
2. Click a process to see its threads and details in the lower panel.
3. Use the buttons above each table, the top menu, or right-click a row to change priority, set affinity, suspend, resume, or terminate.
4. Click **Launch process** to start a new program. In the dialog, select an `.exe` file, enter arguments if needed, and click **Launch**.

| Shortcut | Action |
|---|---|
| `F5` | Refresh |
| `Ctrl+N` | Launch a process |
| `Del` | Terminate the selected process |

---

## Limitations

- **Access rights.** Without administrator rights, processes of other users and system services cannot be opened, so some of their values are shown as **N/A**. Run the application as administrator to see more. Protected system processes stay partly unavailable even then.
- **Be careful with system processes.** Suspending or terminating processes such as `explorer.exe` or system services can make Windows unstable. Experiment with your own programs, for example `notepad.exe` launched from ProcScope.
- **Large VA values are normal.** Browsers and Chromium-based apps (Discord, Edge) reserve large amounts of address space, so VA can reach terabytes while real memory use stays small.
- **Suspending a process is not atomic.** New threads can appear while the operation is in progress; in that case the state is shown as *Partial*.

## Verification

The values shown by ProcScope were compared with **Sysinternals Process Explorer**. CPU %, working set, private bytes, thread count, and priority matched, and a process suspended in ProcScope appeared as *Suspended* in Process Explorer.

---

## Author

**Arsen** — Backend Developer (C++)

[![GitHub](https://img.shields.io/badge/GitHub-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/ArsenBo1chuk)
[![Telegram](https://img.shields.io/badge/Telegram-26A5E4?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Arsen_Bo1chuk)
