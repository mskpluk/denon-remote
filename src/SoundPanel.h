#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class AvrState;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// Sound mode, quick selects, tone controls and the standing options
// (dynamic range, Restorer, ECO, sleep timer).
class SoundPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SoundPanel(AvrState *state, QWidget *parent = nullptr);

signals:
    void commandRequested(const QString &command);

private slots:
    void refreshSoundMode();
    void refreshQuickSelect();
    void refreshTone();
    void refreshOptions();

private:
    // Builds a labelled slider whose value is an offset the AVR reports
    // centred on 50.
    QSlider *addToneRow(class QGridLayout *layout, int row, QLabel *title, int limit,
                        const QString &command, QLabel **valueLabel);

    AvrState *m_state;

    QLabel *m_currentMode;
    QCheckBox *m_toneControl;
    QSlider *m_bass;
    QSlider *m_treble;
    QSlider *m_subwoofer;
    QLabel *m_bassValue;
    QLabel *m_trebleValue;
    QLabel *m_subwooferValue;
    QLabel *m_subwooferLabel;
    QComboBox *m_drc;
    QComboBox *m_restorer;
    QComboBox *m_eco;
    QLabel *m_drcLabel;
    QLabel *m_restorerLabel;
    QLabel *m_ecoLabel;
    QComboBox *m_sleep;
    QLabel *m_quickSelect;
    QVector<QPushButton *> m_quickButtons;

    // Slot reused for a sleep value that is not one of the presets.
    int m_sleepCustomIndex = -1;
    bool m_updating = false;
};
