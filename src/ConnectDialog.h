#pragma once

#include <QDialog>
#include <QString>

#include "Discovery.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Picks a receiver: either one found on the network or an address typed by hand.
class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(const QString &currentHost, QWidget *parent = nullptr);

    QString selectedHost() const;

private slots:
    void startScan();
    void onDeviceFound(const DiscoveredDevice &device);
    void onScanFinished();
    void onSelectionChanged();

private:
    Discovery *m_discovery;
    QListWidget *m_list;
    QLineEdit *m_manualHost;
    QPushButton *m_scanButton;
    QLabel *m_status;
    QDialogButtonBox *m_buttons;
};
