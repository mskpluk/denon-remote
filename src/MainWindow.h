#pragma once

#include <QMainWindow>
#include <QString>

#include "AvrConnection.h"

class AvrState;
class KStatusNotifierItem;
class SoundPanel;
class ZonePanel;
class QAction;
class QLabel;
class QTabWidget;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseDevice();
    void onStatusChanged(AvrConnection::Status status);
    void onConnectionError(const QString &message);
    void onTrayScroll(int delta, Qt::Orientation orientation);
    void flushWheelVolume();
    void showAbout();

private:
    QWidget *buildRemotePad();
    void buildMenus();
    void buildTrayIcon();
    void connectToStoredHost();
    void updateTrayTooltip();

    // One wheel notch over the tray icon moves the volume by this much.
    static constexpr double WheelStepDb = 0.5;
    // Qt and the tray host both report one wheel notch as 120.
    static constexpr int WheelNotch = 120;

    AvrConnection *m_connection;
    AvrState *m_state;

    QTabWidget *m_tabs;
    ZonePanel *m_mainZone;
    ZonePanel *m_zone2;
    SoundPanel *m_sound;
    QLabel *m_statusLabel;
    KStatusNotifierItem *m_tray = nullptr;
    QAction *m_minimiseToTray = nullptr;

    QTimer *m_wheelTimer;
    double m_wheelTarget = 0.0;   // raw volume the wheel is heading for
    bool m_wheelPending = false;  // a target is waiting for the timer
    int m_wheelDelta = 0;         // scrolling not yet adding up to a notch

    QString m_host;
};
