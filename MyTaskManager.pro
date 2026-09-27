QT       += core gui widgets svg
CONFIG   += c++17
TARGET    = MyTaskManager
TEMPLATE  = app

DEFINES  += UNICODE _UNICODE NOMINMAX

SOURCES  += main.cpp mainwindow.cpp launchdialog.cpp prioritydialog.cpp affinitydialog.cpp statebadgedelegate.cpp
HEADERS  += mainwindow.h launchdialog.h prioritydialog.h affinitydialog.h statebadgedelegate.h uitypes.h tableitems.h
FORMS    += mainwindow.ui launchdialog.ui prioritydialog.ui affinitydialog.ui
RESOURCES += resources.qrc

win32: LIBS += -lpsapi -ladvapi32
