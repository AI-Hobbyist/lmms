#ifndef LMMS_SVS_LYRIC_EDITOR_H
#define LMMS_SVS_LYRIC_EDITOR_H
#include "SVSClip.h"
#include <QDialog>
#include <QSet>
class QPlainTextEdit;
class QCheckBox;
class QTableWidget;
class QLabel;
namespace lmms::gui {
svs::Pronunciation editorPronunciation(SVSClip*, const svs::Note&, bool candidates = false);
class SVSLyricEditor : public QDialog {
public:
 explicit SVSLyricEditor(SVSClip*,const QSet<QString>&,QWidget* parent=nullptr);
 static QStringList splitLyrics(const QString&);
 void accept() override;
protected:
 bool eventFilter(QObject*,QEvent*) override;
private:
 void preview();
 QPointer<SVSClip> m_clip;
 QVector<svs::Note> m_original,m_preview;
 QSet<QString> m_selection;
 QPlainTextEdit* m_text;
 QCheckBox* m_skip;
 QTableWidget* m_table;
 QLabel* m_diagnostic;
 bool m_composing=false;
};
}
#endif
