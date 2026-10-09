#pragma once

#include <QColor>
#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <memory>
#include <vector>

#include "AutomatableModel.h"
#include "SVCAudition.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QSlider;
class QTimer;

namespace lmms {
class SVCTrack;
class SVCClip;
namespace gui {
class SVCWaveform : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor svcSourceColor MEMBER m_sourceColor)
	Q_PROPERTY(QColor svcRenderedColor MEMBER m_renderedColor)
	Q_PROPERTY(QColor svcPendingColor MEMBER m_pendingColor)
	Q_PROPERTY(QColor svcErrorColor MEMBER m_errorColor)
	Q_PROPERTY(QColor svcComparePlayheadColor MEMBER m_playheadColor)
public:
	explicit SVCWaveform(QWidget* parent = nullptr);
	void bind(std::shared_ptr<svc::AuditionState> audition);
	QColor sourceColor() const { return m_sourceColor; }
	QColor renderedColor() const { return m_renderedColor; }

signals:
	void seek(uint64_t position);
	void loopRange(uint64_t start, uint64_t end);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;

private:
	uint64_t frameAt(int x) const;
	std::shared_ptr<svc::AuditionState> m_audition;
	QColor m_sourceColor{"#788A9B"};
	QColor m_renderedColor{"#42B8A5"};
	QColor m_pendingColor{"#D5A64A"};
	QColor m_errorColor{"#D66A72"};
	QColor m_playheadColor{"#F2ECFF"};
	uint64_t m_loopAnchor = 0;
	bool m_selecting = false;
};

class SVCWindow : public QDialog
{
	Q_OBJECT
public:
	explicit SVCWindow(SVCTrack* track, QWidget* parent = nullptr);
	~SVCWindow() override;
	void selectClip(SVCClip* clip);
	std::shared_ptr<svc::AuditionState> audition() const { return m_audition; }

private:
	void refreshClips();
	void refreshEngines();
	void refreshModels();
	void refreshChoices();
	void refreshParameters();
	void refreshParameterAvailability();
	void saveSelection();
	void refreshStatus();
	void importAudio();
	void togglePlayback();
	SVCTrack* m_track;
	QPointer<SVCClip> m_clip;
	QComboBox* m_clips;
	QComboBox* m_engines;
	QComboBox* m_models;
	QComboBox* m_weights;
	QComboBox* m_speakers;
	QWidget* m_parameterBody;
	QLabel* m_status;
	QLabel* m_overload;
	QSlider* m_position;
	SVCWaveform* m_waveform;
	QTimer* m_timer;
	std::shared_ptr<svc::AuditionState> m_audition;
	std::vector<std::unique_ptr<FloatModel>> m_modelsOwned;
	std::vector<std::unique_ptr<FloatModel>> m_parametersOwned;
	QJsonObject m_parameters;
	QString m_chunkError;
	bool m_updating = false;
	bool m_handleAdded = false;
};
} // namespace gui
} // namespace lmms
