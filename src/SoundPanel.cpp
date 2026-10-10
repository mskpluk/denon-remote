#include "SoundPanel.h"

#include "AvrState.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

// Surround modes the receiver accepts as an MS command. The AVR reports back
// the mode it actually settled on, which is often more specific than what was
// asked for, so the current mode is shown as text rather than as a selection.
const char *const kSoundModes[] = {
    "MOVIE", "MUSIC", "GAME",          "DIRECT",  "PURE DIRECT", "STEREO",
    "AUTO",  "VIRTUAL", "MCH STEREO",  "DOLBY DIGITAL", "DTS SURROUND",
};

struct OptionSet {
    const char *command;   // full command prefix, space included where needed
    const char *values[6];
};

const OptionSet kDrc = {"PSDRC ", {"AUTO", "LOW", "MID", "HI", "OFF", nullptr}};
const OptionSet kRestorer = {"PSRSTR ", {"OFF", "LOW", "MED", "HI", nullptr, nullptr}};
const OptionSet kEco = {"ECO", {"ON", "AUTO", "OFF", nullptr, nullptr, nullptr}};

QString formatOffset(int decibels)
{
    return QStringLiteral("%1%2 dB")
        .arg(decibels > 0 ? QStringLiteral("+") : QString())
        .arg(decibels);
}

void fillOptions(QComboBox *box, const OptionSet &set)
{
    for (const char *value : set.values) {
        if (!value)
            break;
        box->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    }
}

} // namespace

SoundPanel::SoundPanel(AvrState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_currentMode(new QLabel(this))
    , m_toneControl(new QCheckBox(tr("Tone control"), this))
    , m_drc(new QComboBox(this))
    , m_restorer(new QComboBox(this))
    , m_eco(new QComboBox(this))
    , m_sleep(new QComboBox(this))
    , m_quickSelect(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);

    // Sound mode
    auto *modeGroup = new QGroupBox(tr("Sound mode"), this);
    auto *modeLayout = new QVBoxLayout(modeGroup);
    QFont modeFont = m_currentMode->font();
    modeFont.setBold(true);
    m_currentMode->setFont(modeFont);
    modeLayout->addWidget(m_currentMode);

    auto *modeGrid = new QGridLayout;
    int index = 0;
    for (const char *mode : kSoundModes) {
        const QString name = QString::fromLatin1(mode);
        auto *button = new QPushButton(name, modeGroup);
        connect(button, &QPushButton::clicked, this, [this, name] {
            emit commandRequested(QStringLiteral("MS") + name);
        });
        modeGrid->addWidget(button, index / 4, index % 4);
        ++index;
    }
    modeLayout->addLayout(modeGrid);
    layout->addWidget(modeGroup);

    // Quick selects
    auto *quickGroup = new QGroupBox(tr("Quick Select"), this);
    auto *quickLayout = new QHBoxLayout(quickGroup);
    // Most models have four slots; the fifth button shows only once the
    // receiver mentions a fifth slot.
    for (int slot = 1; slot <= 5; ++slot) {
        auto *button = new QPushButton(QString::number(slot), quickGroup);
        connect(button, &QPushButton::clicked, this, [this, slot] {
            emit commandRequested(QStringLiteral("MSQUICK%1").arg(slot));
        });
        quickLayout->addWidget(button);
        m_quickButtons.append(button);
    }
    quickLayout->addWidget(m_quickSelect, 1);
    layout->addWidget(quickGroup);

    // Tone
    auto *toneGroup = new QGroupBox(tr("Tone"), this);
    auto *toneLayout = new QGridLayout(toneGroup);
    toneLayout->addWidget(m_toneControl, 0, 0, 1, 3);
    m_bass = addToneRow(toneLayout, 1, new QLabel(tr("Bass"), toneGroup), 6,
                        QStringLiteral("PSBAS "), &m_bassValue);
    m_treble = addToneRow(toneLayout, 2, new QLabel(tr("Treble"), toneGroup), 6,
                          QStringLiteral("PSTRE "), &m_trebleValue);
    m_subwooferLabel = new QLabel(tr("Subwoofer"), toneGroup);
    m_subwoofer = addToneRow(toneLayout, 3, m_subwooferLabel, 12, QStringLiteral("PSSWL "),
                             &m_subwooferValue);
    toneLayout->setColumnStretch(1, 1);
    layout->addWidget(toneGroup);

    // Standing options
    auto *optionGroup = new QGroupBox(tr("Options"), this);
    auto *optionLayout = new QGridLayout(optionGroup);
    fillOptions(m_drc, kDrc);
    fillOptions(m_restorer, kRestorer);
    fillOptions(m_eco, kEco);

    m_sleep->addItem(tr("Off"), 0);
    for (int minutes : {10, 15, 30, 45, 60, 90, 120}) {
        m_sleep->addItem(tr("%1 min").arg(minutes), minutes);
    }

    m_drcLabel = new QLabel(tr("Dynamic range"), optionGroup);
    m_restorerLabel = new QLabel(tr("Restorer"), optionGroup);
    m_ecoLabel = new QLabel(tr("ECO mode"), optionGroup);
    optionLayout->addWidget(m_drcLabel, 0, 0);
    optionLayout->addWidget(m_drc, 0, 1);
    optionLayout->addWidget(m_restorerLabel, 1, 0);
    optionLayout->addWidget(m_restorer, 1, 1);
    optionLayout->addWidget(m_ecoLabel, 2, 0);
    optionLayout->addWidget(m_eco, 2, 1);
    optionLayout->addWidget(new QLabel(tr("Sleep timer"), optionGroup), 3, 0);
    optionLayout->addWidget(m_sleep, 3, 1);
    optionLayout->setColumnStretch(1, 1);
    layout->addWidget(optionGroup);
    layout->addStretch(1);

    connect(m_toneControl, &QCheckBox::clicked, this, [this](bool checked) {
        emit commandRequested(QStringLiteral("PSTONE CTRL ")
                              + (checked ? QStringLiteral("ON") : QStringLiteral("OFF")));
    });
    connect(m_drc, &QComboBox::activated, this, [this](int i) {
        emit commandRequested(QLatin1String(kDrc.command) + m_drc->itemData(i).toString());
    });
    connect(m_restorer, &QComboBox::activated, this, [this](int i) {
        emit commandRequested(QLatin1String(kRestorer.command) + m_restorer->itemData(i).toString());
    });
    connect(m_eco, &QComboBox::activated, this, [this](int i) {
        emit commandRequested(QLatin1String(kEco.command) + m_eco->itemData(i).toString());
    });
    connect(m_sleep, &QComboBox::activated, this, [this](int i) {
        const int minutes = m_sleep->itemData(i).toInt();
        emit commandRequested(minutes == 0
                                  ? QStringLiteral("SLPOFF")
                                  : QStringLiteral("SLP%1").arg(minutes, 3, 10, QLatin1Char('0')));
    });

    connect(m_state, &AvrState::soundModeChanged, this, &SoundPanel::refreshSoundMode);
    connect(m_state, &AvrState::quickSelectChanged, this, &SoundPanel::refreshQuickSelect);
    connect(m_state, &AvrState::quickSelectNamesChanged, this, &SoundPanel::refreshQuickSelect);
    connect(m_state, &AvrState::toneChanged, this, &SoundPanel::refreshTone);
    connect(m_state, &AvrState::drcChanged, this, &SoundPanel::refreshOptions);
    connect(m_state, &AvrState::restorerChanged, this, &SoundPanel::refreshOptions);
    connect(m_state, &AvrState::ecoChanged, this, &SoundPanel::refreshOptions);
    connect(m_state, &AvrState::sleepChanged, this, &SoundPanel::refreshOptions);

    refreshSoundMode();
    refreshQuickSelect();
    refreshTone();
    refreshOptions();
}

QSlider *SoundPanel::addToneRow(QGridLayout *layout, int row, QLabel *title, int limit,
                                const QString &command, QLabel **valueLabel)
{
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(-limit, limit);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(limit);

    auto *value = new QLabel(this);
    value->setMinimumWidth(56);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    *valueLabel = value;

    layout->addWidget(title, row, 0);
    layout->addWidget(slider, row, 1);
    layout->addWidget(value, row, 2);

    auto publish = [this, slider, command] {
        // The AVR reports these centred on 50, and expects them the same way.
        emit commandRequested(command + QString::number(slider->value() + 50));
    };

    connect(slider, &QSlider::valueChanged, this, [this, slider, value, publish](int position) {
        value->setText(formatOffset(position));
        if (m_updating || slider->isSliderDown())
            return;
        publish();
    });
    connect(slider, &QSlider::sliderReleased, this, publish);

    value->setText(formatOffset(slider->value()));
    return slider;
}

void SoundPanel::refreshSoundMode()
{
    const QString mode = m_state->soundMode();
    m_currentMode->setText(mode.isEmpty() ? tr("Current: unknown") : tr("Current: %1").arg(mode));
}

void SoundPanel::refreshQuickSelect()
{
    for (int i = 0; i < m_quickButtons.size(); ++i) {
        const int slot = i + 1;
        const QString name = m_state->quickSelectName(slot);
        m_quickButtons[i]->setText(name.isEmpty() ? QString::number(slot) : name);
        m_quickButtons[i]->setVisible(slot <= m_state->quickSelectSlots());
    }

    const int slot = m_state->quickSelect();
    const QString name = m_state->quickSelectName(slot);
    m_quickSelect->setText(slot > 0 ? tr("active: %1").arg(name.isEmpty() ? QString::number(slot) : name)
                                    : QString());
}

void SoundPanel::refreshTone()
{
    m_updating = true;
    m_toneControl->setChecked(m_state->toneControl());
    m_bass->setValue(m_state->bass());
    m_treble->setValue(m_state->treble());
    m_subwoofer->setValue(m_state->subwoofer());
    // setValue() stays silent when the value is unchanged, so the readouts are
    // written here rather than left to the slider's own signal.
    m_bassValue->setText(formatOffset(m_state->bass()));
    m_trebleValue->setText(formatOffset(m_state->treble()));
    m_subwooferValue->setText(formatOffset(m_state->subwoofer()));
    m_updating = false;

    // The receiver drops these without complaint when the matching adjustment
    // is switched off, so they are disabled rather than left looking usable.
    const bool tone = m_state->toneControl();
    m_bass->setEnabled(tone);
    m_treble->setEnabled(tone);
    m_bassValue->setEnabled(tone);
    m_trebleValue->setEnabled(tone);

    const bool sub = m_state->subwooferAdjust();
    m_subwoofer->setEnabled(sub);
    m_subwooferValue->setEnabled(sub);
    m_subwooferLabel->setEnabled(sub);
    m_subwooferLabel->setToolTip(
        sub ? QString()
            : tr("Subwoofer level adjustment is switched off on the receiver. "
                 "Enable it in the receiver's setup menu, under Audio."));
}

void SoundPanel::refreshOptions()
{
    m_updating = true;

    // These rows appear only once the receiver has reported a value: a model
    // without the option ignores the query, and a control that does nothing
    // would be worse than none.
    const struct {
        QComboBox *box;
        QLabel *label;
        QString value;
    } selections[] = {
        {m_drc, m_drcLabel, m_state->drc()},
        {m_restorer, m_restorerLabel, m_state->restorer()},
        {m_eco, m_ecoLabel, m_state->eco()},
    };
    for (const auto &selection : selections) {
        selection.box->setVisible(!selection.value.isEmpty());
        selection.label->setVisible(!selection.value.isEmpty());
        const int index = selection.box->findData(selection.value);
        if (index >= 0)
            selection.box->setCurrentIndex(index);
    }

    const int minutes = m_state->sleepMinutes();
    int index = m_sleep->findData(minutes);
    if (index < 0) {
        // The AVR counts down, so the remaining time rarely matches a preset.
        // One reusable slot holds it, otherwise the list would grow each minute.
        if (m_sleepCustomIndex < 0) {
            m_sleep->addItem(tr("%1 min").arg(minutes), minutes);
            m_sleepCustomIndex = m_sleep->count() - 1;
        } else {
            m_sleep->setItemText(m_sleepCustomIndex, tr("%1 min").arg(minutes));
            m_sleep->setItemData(m_sleepCustomIndex, minutes);
        }
        index = m_sleepCustomIndex;
    }
    m_sleep->setCurrentIndex(index);
    m_updating = false;
}
