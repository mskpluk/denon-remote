#include "ConnectDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ConnectDialog::ConnectDialog(const QString &currentHost, QWidget *parent)
    : QDialog(parent)
    , m_discovery(new Discovery(this))
    , m_list(new QListWidget(this))
    , m_manualHost(new QLineEdit(currentHost, this))
    , m_scanButton(new QPushButton(tr("Scan again"), this))
    , m_status(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Connect to receiver"));
    setMinimumWidth(420);

    m_manualHost->setPlaceholderText(tr("Host name or IP address"));
    m_list->setAlternatingRowColors(true);

    auto *found = new QGroupBox(tr("Found on the network"), this);
    auto *foundLayout = new QVBoxLayout(found);
    foundLayout->addWidget(m_list);

    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_scanButton);
    foundLayout->addLayout(statusRow);

    auto *manual = new QGroupBox(tr("Or enter the address directly"), this);
    auto *manualLayout = new QHBoxLayout(manual);
    manualLayout->addWidget(m_manualHost);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(found, 1);
    layout->addWidget(manual);
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_scanButton, &QPushButton::clicked, this, &ConnectDialog::startScan);
    connect(m_list, &QListWidget::currentRowChanged, this, &ConnectDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_discovery, &Discovery::deviceFound, this, &ConnectDialog::onDeviceFound);
    connect(m_discovery, &Discovery::finished, this, &ConnectDialog::onScanFinished);

    startScan();
}

QString ConnectDialog::selectedHost() const
{
    return m_manualHost->text().trimmed();
}

void ConnectDialog::startScan()
{
    m_list->clear();
    m_scanButton->setEnabled(false);
    m_status->setText(tr("Searching…"));
    m_discovery->start();
}

void ConnectDialog::onDeviceFound(const DiscoveredDevice &device)
{
    auto *item = new QListWidgetItem(tr("%1 — %2").arg(device.model, device.host), m_list);
    item->setData(Qt::UserRole, device.host);

    // Preselecting writes into the address field, so it must not overwrite an
    // address the user has typed: only the configured receiver, or the first
    // hit when the field is still empty.
    const QString typed = m_manualHost->text().trimmed();
    if (device.host == typed || (typed.isEmpty() && m_list->count() == 1))
        m_list->setCurrentItem(item);
}

void ConnectDialog::onScanFinished()
{
    m_scanButton->setEnabled(true);
    m_status->setText(m_list->count() == 0
                          ? tr("No receivers found — enter the address below.")
                          : tr("%n receiver(s) found.", nullptr, m_list->count()));
}

void ConnectDialog::onSelectionChanged()
{
    if (auto *item = m_list->currentItem())
        m_manualHost->setText(item->data(Qt::UserRole).toString());
}
