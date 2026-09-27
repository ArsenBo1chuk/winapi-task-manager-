# MyTaskManager

A process and thread manager for Windows, written in C++ with a Qt user interface.
All system operations are performed directly through the **Windows API**, without `tasklist`, `taskkill`, PowerShell, or WMI.

## Features

**Monitoring**
- List of all processes: PID, name, CPU %, priority class, state, memory (WS, VA, Private), thread count, owner.
- Threads of the selected process: TID, CPU %, relative and base priority, CPU time, affinity mask, state.
- Automatic refresh with a configurable interval.

**Control**
- Launch a new process with arguments, a working directory, and optional `CREATE_SUSPENDED` mode.
- Change the priority class of a process and the relative priority of a thread.
- Change CPU affinity for a process or a thread.
- Suspend and resume a whole process or individual threads.
- Terminate a process (with confirmation).

**Usability**
- Sorting by any column.
- Filters: "Hide system processes", "Selected PID only", and search by name or PID.
- Operation log with the result and error code of every action.
- Context menus in both tables.

## Requirements

- Windows 10/11, x64
- Qt 6 (tested with Qt 6.11.1, MSVC 2022 64-bit) with the **Widgets** and **Svg** modules
- Visual Studio 2022 (MSVC compiler)

## Build

1. Open Qt Creator.
2. Go to **File → Open File or Project** and select `CMakeLists.txt` (or the `.pro` file) in the `MyTaskManager` folder.
3. Choose the **Desktop Qt 6.x MSVC2022 64bit** kit.
4. Press **Build** (`Ctrl+B`), then **Run** (`Ctrl+R`).

`MyWorker` is built the same way.

## Usage

Start a worker process with 4 threads:

```
MyWorker.exe 4
```

Or from the manager: **Launch process** → select `MyWorker.exe` → enter the number of threads in the "Arguments" field.

> **Note:** Without administrator rights, processes of other users and system services cannot be opened, so some of their values are shown as "N/A". This is normal Windows behavior.

## Windows API functions used

| Task | Functions |
|---|---|
| Listing processes and threads | `CreateToolhelp32Snapshot`, `Process32FirstW/NextW`, `Thread32First/Next` |
| CPU time | `GetProcessTimes`, `GetThreadTimes`, `QueryPerformanceCounter` |
| Memory | `GetProcessMemoryInfo`, `VirtualQueryEx` |
| Priority | `Get/SetPriorityClass`, `Get/SetThreadPriority` |
| Affinity | `Get/SetProcessAffinityMask`, `GetThreadGroupAffinity`, `SetThreadAffinityMask` |
| Suspend / resume | `SuspendThread`, `ResumeThread` |
| Launch and termination | `CreateProcessW`, `TerminateProcess`, `WaitForSingleObject`, `GetExitCodeProcess` |
| Process owner | `OpenProcessToken`, `GetTokenInformation` |

## Author

**Arsen** — Backend Developer (C++)

[![GitHub](https://img.shields.io/badge/GitHub-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/ArsenBo1chuk)
[![Telegram](https://img.shields.io/badge/Telegram-26A5E4?style=for-the-badge&logo=telegram&logoColor=white)](https://t.me/Arsen_Bo1chuk)