#ifndef LMMS_SVS_VIEWS_H
#define LMMS_SVS_VIEWS_H
#include "TrackView.h"
#include "ClipView.h"
#include <QDialog>
#include <QPointer>
#include <QColor>
#include <QMap>
namespace lmms { class SVSTrack; class SVSClip;
namespace gui {
class SVSTrackView : public TrackView {
 Q_OBJECT
public:
 SVSTrackView(SVSTrack*,TrackContainerView*);
protected:
 void dragEnterEvent(QDragEnterEvent*) override;
 void dropEvent(QDropEvent*) override;
};
class SVSClipView : public ClipView {
 Q_OBJECT
public:
 SVSClipView(SVSClip*,TrackView*);
protected:
 void paintEvent(QPaintEvent*) override;
 void mouseDoubleClickEvent(QMouseEvent*) override;
private: SVSClip* m_clip;
};
class SVSCanvas;
class SVSPianoRoll : public QDialog {
 Q_OBJECT
 Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setbackgroundColor)
 Q_PROPERTY(QColor gridLineColor READ gridLineColor WRITE setgridLineColor)
 Q_PROPERTY(QColor noteColor READ noteColor WRITE setnoteColor)
 Q_PROPERTY(QColor selectedNoteColor READ selectedNoteColor WRITE setselectedNoteColor)
 Q_PROPERTY(QColor lyricColor READ lyricColor WRITE setlyricColor)
 Q_PROPERTY(QColor pronunciationColor READ pronunciationColor WRITE setpronunciationColor)
 Q_PROPERTY(QColor userPitchColor READ userPitchColor WRITE setuserPitchColor)
 Q_PROPERTY(QColor synthesizedPitchColor READ synthesizedPitchColor WRITE setsynthesizedPitchColor)
 Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setwaveformColor)
 Q_PROPERTY(QColor phonemeColor READ phonemeColor WRITE setphonemeColor)
 Q_PROPERTY(QColor invalidColor READ invalidColor WRITE setinvalidColor)
 Q_PROPERTY(QColor renderingColor READ renderingColor WRITE setrenderingColor)
 Q_PROPERTY(QColor errorColor READ errorColor WRITE seterrorColor)
public:
 explicit SVSPianoRoll(SVSClip*,QWidget* parent=nullptr);
 QColor backgroundColor() const { return m_colors.value(QStringLiteral("backgroundColor")); }
 void setbackgroundColor(const QColor& value) { setThemeColor(QStringLiteral("backgroundColor"),value); }
 QColor gridLineColor() const { return m_colors.value(QStringLiteral("gridLineColor")); }
 void setgridLineColor(const QColor& value) { setThemeColor(QStringLiteral("gridLineColor"),value); }
 QColor noteColor() const { return m_colors.value(QStringLiteral("noteColor")); }
 void setnoteColor(const QColor& value) { setThemeColor(QStringLiteral("noteColor"),value); }
 QColor selectedNoteColor() const { return m_colors.value(QStringLiteral("selectedNoteColor")); }
 void setselectedNoteColor(const QColor& value) { setThemeColor(QStringLiteral("selectedNoteColor"),value); }
 QColor lyricColor() const { return m_colors.value(QStringLiteral("lyricColor")); }
 void setlyricColor(const QColor& value) { setThemeColor(QStringLiteral("lyricColor"),value); }
 QColor pronunciationColor() const { return m_colors.value(QStringLiteral("pronunciationColor")); }
 void setpronunciationColor(const QColor& value) { setThemeColor(QStringLiteral("pronunciationColor"),value); }
 QColor userPitchColor() const { return m_colors.value(QStringLiteral("userPitchColor")); }
 void setuserPitchColor(const QColor& value) { setThemeColor(QStringLiteral("userPitchColor"),value); }
 QColor synthesizedPitchColor() const { return m_colors.value(QStringLiteral("synthesizedPitchColor")); }
 void setsynthesizedPitchColor(const QColor& value) { setThemeColor(QStringLiteral("synthesizedPitchColor"),value); }
 QColor waveformColor() const { return m_colors.value(QStringLiteral("waveformColor")); }
 void setwaveformColor(const QColor& value) { setThemeColor(QStringLiteral("waveformColor"),value); }
 QColor phonemeColor() const { return m_colors.value(QStringLiteral("phonemeColor")); }
 void setphonemeColor(const QColor& value) { setThemeColor(QStringLiteral("phonemeColor"),value); }
 QColor invalidColor() const { return m_colors.value(QStringLiteral("invalidColor")); }
 void setinvalidColor(const QColor& value) { setThemeColor(QStringLiteral("invalidColor"),value); }
 QColor renderingColor() const { return m_colors.value(QStringLiteral("renderingColor")); }
 void setrenderingColor(const QColor& value) { setThemeColor(QStringLiteral("renderingColor"),value); }
 QColor errorColor() const { return m_colors.value(QStringLiteral("errorColor")); }
 void seterrorColor(const QColor& value) { setThemeColor(QStringLiteral("errorColor"),value); }
protected:
 void changeEvent(QEvent*) override;
private:
 void setThemeColor(const QString&,const QColor&);
 QMap<QString,QColor> m_colors;
 SVSCanvas* m_canvas=nullptr;
 bool m_polishingTheme=false;
};
}
}
#endif
