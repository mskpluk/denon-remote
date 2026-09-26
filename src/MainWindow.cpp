#include "MainWindow.h"

#include "AvrState.h"
#include "ConnectDialog.h"
#include "SoundPanel.h"
#include "ZonePanel.h"

#include <KStatusNotifierItem>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QIcon applicationIcon()
{
    QIcon icon(QStringLiteral(":/data/icons/denon-remote.svg"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("audio-card"));
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connection(new AvrConnection(this))
    , m_state(new AvrState(this))
    , m_tabs(new QTabWidget(this))
    , m_statusLabel(new QLabel(this))
    , m_wheelTimer(new QTimer(this))
{
    setWindowTitle(tr("Denon Remote"));
    setWindowIcon(applicationIcon());

    m_mainZone = new ZonePanel(ZonePanel::Zone::Main, m_state, this);
    m_zone2 = new ZonePanel(ZonePanel::Zone::Zone2, m_state, this);
    m_sound = new SoundPanel(m_state, this);

    m_tabs->addTab(m_mainZone, tr("Main zone"));
    m_tabs->addTab(m_zone2, tr("Zone 2"));
    m_tabs->addTab(m_sound, tr("Sound"));
    m_tabs->addTab(buildRemotePad(), tr("Menu"));
    m_tabs->setCurrentIndex(QSettings().value(QStringLiteral("window/tab"), 0).toInt());
    setCentralWidget(m_tabs);

    statusBar()->addWidget(m_statusLabel);

    // Everything the AVR says feeds the mirrored state; everything the panels
    // ask for goes back out on the same link.
    connect(m_connection, &AvrConnection::lineReceived, m_state, &AvrState::handleLine);
    connect(m_connection, &AvrConnection::statusChanged, this, &MainWindow::onStatusChanged);
    connect(m_connection, &AvrConnection::errorOccurred, this, &MainWindow::onConnectionError);
    connect(m_state, &AvrState::powerChanged, this, &MainWindow::updateTrayTooltip);
    connect(m_state, &AvrState::volumeChanged, this, &MainWindow::updateTrayTooltip);
    connect(m_state, &AvrState::muteChanged, this, &MainWindow::updateTrayTooltip);

    // Spinning the wheel sends the first step at once and then at most one
    // volume command per interval, always the latest target.
    m_wheelTimer->setSingleShot(true);
    m_wheelTimer->setInterval(150);
    connect(m_wheelTimer, &QTimer::timeout, this, &MainWindow::flushWheelVolume);

    for (ZonePanel *panel : {m_mainZone, m_zone2})
        connect(panel, &ZonePanel::commandRequested, m_connection, &AvrConnection::send);
    connect(m_sound, &SoundPanel::commandRequested, m_connection, &AvrConnection::send);

    buildMenus();
    buildTrayIcon();

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    m_host = settings.value(QStringLiteral("avr/host")).toString();
    if (m_minimiseToTray) {
        m_minimiseToTray->setChecked(
            settings.value(QStringLiteral("window/minimiseToTray"), false).toBool());
    }

    onStatusChanged(m_connection->status());
    if (m_host.isEmpty())
        QMetaObject::invokeMethod(this, &MainWindow::chooseDevice, Qt::QueuedConnection);
    else
        connectToStoredHost();
}

QWidget *MainWindow::buildRemotePad()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *grid = new QGridLayout;
    struct PadButton {
        const char *label;
        const char *command;
        int row;
        int column;
    };
    // The on-screen setup menu is driven entirely by these cursor commands.
    const PadButton buttons[] = {
        {QT_TR_NOOP("Up"), "MNCUP", 0, 1},    {QT_TR_NOOP("Left"), "MNCLT", 1, 0},
        {QT_TR_NOOP("Enter"), "MNENT", 1, 1}, {QT_TR_NOOP("Right"), "MNCRT", 1, 2},
        {QT_TR_NOOP("Down"), "MNCDN", 2, 1},  {QT_TR_NOOP("Back"), "MNRTN", 2, 2},
    };
    for (const PadButton &spec : buttons) {
        auto *button = new QPushButton(tr(spec.label), page);
        button->setMinimumSize(80, 44);
        const QString command = QString::fromLatin1(spec.command);
        connect(button, &QPushButton::clicked, this,
                [this, command] { m_connection->send(command); });
        grid->addWidget(button, spec.row, spec.column);
    }
    layout->addLayout(grid);

    auto *menuOn = new QPushButton(tr("Open setup menu"), page);
    auto *menuOff = new QPushButton(tr("Close setup menu"), page);
    connect(menuOn, &QPushButton::clicked, this,
            [this] { m_connection->send(QStringLiteral("MNMEN ON")); });
    connect(menuOff, &QPushButton::clicked, this,
            [this] { m_connection->send(QStringLiteral("MNMEN OFF")); });
    layout->addWidget(menuOn);
    layout->addWidget(menuOff);
    layout->addStretch(1);

    return page;
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&Receiver"));

    QAction *connectAction = fileMenu->addAction(tr("&Connect to…"));
    connectAction->setShortcut(QKeySequence::Open);
    connect(connectAction, &QAction::triggered, this, &MainWindow::chooseDevice);

    QAction *refreshAction = fileMenu->addAction(tr("&Refresh state"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, m_connection, &AvrConnection::refreshAll);

    fileMenu->addSeparator();

    m_minimiseToTray = fileMenu->addAction(tr("Close to &tray"));
    m_minimiseToTray->setCheckable(true);
    m_minimiseToTray->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());

    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    connect(helpMenu->addAction(tr("&About")), &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    // KStatusNotifierItem rather than QSystemTrayIcon: Qt's own
    // StatusNotifierItem ignores the Scroll call, so the mouse wheel over the
    // icon would never reach the application.
    m_tray = new KStatusNotifierItem(QStringLiteral("denon-remote"), this);
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setStatus(KStatusNotifierItem::Active);
    m_tray->setTitle(tr("Denon Remote"));
    // The menu below already has its own Quit; the standard one would add a
    // second.
    m_tray->setStandardActionsEnabled(false);
    if (QIcon::hasThemeIcon(QStringLiteral("denon-remote"))) {
        m_tray->setIconByName(QStringLiteral("denon-remote"));
        m_tray->setToolTipIconByName(QStringLiteral("denon-remote"));
    } else {
        m_tray->setIconByPixmap(applicationIcon());
        m_tray->setToolTipIconByPixmap(applicationIcon());
    }

    // No parent: the tray item takes ownership of its menu.
    auto *menu = new QMenu;
    connect(menu->addAction(tr("Show window")), &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Power on")), &QAction::triggered, this,
            [this] { m_connection->send(QStringLiteral("PWON")); });
    connect(menu->addAction(tr("Standby")), &QAction::triggered, this,
            [this] { m_connection->send(QStringLiteral("PWSTANDBY")); });
    connect(menu->addAction(tr("Toggle mute")), &QAction::triggered, this, [this] {
        m_connection->send(m_state->mute() ? QStringLiteral("MUOFF") : QStringLiteral("MUON"));
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Quit")), &QAction::triggered, qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this] {
        setVisible(!isVisible());
        if (isVisible()) {
            raise();
            activateWindow();
        }
    });
    connect(m_tray, &KStatusNotifierItem::scrollRequested, this, &MainWindow::onTrayScroll);
    updateTrayTooltip();
}

void MainWindow::onTrayScroll(int delta, Qt::Orientation orientation)
{
    // In standby the receiver ignores volume commands without a word.
    if (orientation != Qt::Vertical || !m_connection->isConnected() || !m_state->power())
        return;

    // Touchpads scroll in fractions of a notch; they are collected until they
    // add up to a whole one.
    m_wheelDelta += delta;
    const int notches = m_wheelDelta / WheelNotch;
    if (notches == 0)
        return;
    m_wheelDelta -= notches * WheelNotch;

    // While the wheel is still turning, steps build on the target rather than
    // on the receiver's echo, which lags behind a fast spin.
    const bool spinning = m_wheelTimer->isActive();
    const double base = spinning ? m_wheelTarget : m_state->volume();
    m_wheelTarget = qBound(0.0, base + notches * WheelStepDb, m_state->volumeMax());

    if (spinning) {
        m_wheelPending = true;
        return;
    }
    m_connection->send(QStringLiteral("MV") + AvrState::volumeArgument(m_wheelTarget));
    m_wheelTimer->start();
}

void MainWindow::flushWheelVolume()
{
    if (!m_wheelPending)
        return;
    m_wheelPending = false;
    m_connection->send(QStringLiteral("MV") + AvrState::volumeArgument(m_wheelTarget));
    // Keeps the spin going: the next notch still builds on the target.
    m_wheelTimer->start();
}

void MainWindow::connectToStoredHost()
{
    if (m_host.isEmpty())
        return;
    m_state->reset();
    m_connection->open(m_host);
}

void MainWindow::chooseDevice()
{
    ConnectDialog dialog(m_host, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString host = dialog.selectedHost();
    if (host.isEmpty())
        return;

    m_host = host;
    QSettings().setValue(QStringLiteral("avr/host"), m_host);
    connectToStoredHost();
}

void MainWindow::onStatusChanged(AvrConnection::Status status)
{
    const bool connected = status == AvrConnection::Status::Connected;
    m_tabs->setEnabled(connected);

    switch (status) {
    case AvrConnection::Status::Connected:
        m_statusLabel->setText(tr("Connected to %1").arg(m_connection->host()));
        break;
    case AvrConnection::Status::Connecting:
        m_statusLabel->setText(tr("Connecting to %1…").arg(m_connection->host()));
        break;
    case AvrConnection::Status::Disconnected:
        m_statusLabel->setText(m_host.isEmpty() ? tr("No receiver configured")
                                                : tr("Disconnected — retrying…"));
        break;
    }
    updateTrayTooltip();
}

void MainWindow::onConnectionError(const QString &message)
{
    // Retries are automatic, so a failure belongs in the status bar rather than
    // in a dialog that would reappear on every attempt.
    m_statusLabel->setText(tr("%1 — %2").arg(m_connection->host(), message));
}

void MainWindow::updateTrayTooltip()
{
    if (!m_tray)
        return;
    m_tray->setToolTipTitle(tr("Denon Remote"));
    if (!m_connection->isConnected()) {
        m_tray->setToolTipSubTitle(tr("Not connected"));
        return;
    }
    if (!m_state->power()) {
        m_tray->setToolTipSubTitle(tr("%1 — standby").arg(m_connection->host()));
        return;
    }
    m_tray->setToolTipSubTitle(
        tr("%1 — %2%3")
            .arg(m_connection->host(), AvrState::formatDecibel(m_state->volume()),
                 m_state->mute() ? tr(", muted") : QString()));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Denon Remote"),
        tr("<h3>Denon Remote %1</h3>"
           "<p>Controls Denon and Marantz network receivers over the local network.</p>"
           "<p>A single telnet connection carries both commands and the receiver's own "
           "notifications, so the window follows changes made from the front panel, "
           "the infrared remote or the HEOS app.</p>")
            .arg(QStringLiteral(DENON_REMOTE_VERSION)));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/tab"), m_tabs->currentIndex());
    if (m_minimiseToTray)
        settings.setValue(QStringLiteral("window/minimiseToTray"), m_minimiseToTray->isChecked());

    if (m_tray && m_minimiseToTray && m_minimiseToTray->isChecked()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
