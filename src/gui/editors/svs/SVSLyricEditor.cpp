#include "SVSLyricEditor.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "NativePluginTranslation.h"
#include "SVSTrack.h"
namespace lmms::gui {
svs::Pronunciation editorPronunciation(SVSClip* clip, const svs::Note& note, bool candidates)
{
	if (!clip)
		return {};
	auto* track = static_cast<SVSTrack*>(clip->getTrack());
	QVector<svs::Dictionary> project;
	for (const auto& value : clip->projectDictionaryData())
	{
		svs::Dictionary dictionary;
		QString error;
		if (svs::Dictionary::parse(
				QJsonDocument(value.toObject()).toJson(), track->capabilities().phonemeSet, dictionary, error))
			project << dictionary;
	}
	auto input = note;
	if (candidates)
	{
		input.pronunciation.clear();
		input.phonemes = {};
	}
	auto ordered = clip->notes();
	for (auto& other : ordered)
		if (other.id == input.id)
			other = input;
	std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
	svs::Pronunciation previousResult;
	for (int i = 0; i < ordered.size(); ++i)
	{
		auto result = svs::resolvePronunciation(ordered[i], track->capabilities(), track->dictionaries(), project,
			track->language(), i ? &ordered[i - 1] : nullptr, i ? &previousResult : nullptr);
		if (ordered[i].id == note.id)
			return result;
		previousResult = result;
	}
	return {};
}
QStringList SVSLyricEditor::splitLyrics(const QString& text)
{
	// TuneLab's word/ideograph/kana-unit input semantics, with Unicode words retained.
	static const QRegularExpression units(QString::fromUtf8(
		"(?![ー゜])([\\p{Latin}]+|[+-]|[0-9]|[\\p{Han}]|[\\x{3040}-\\x{309F}\\x{30A0}-\\x{30FF}][ャュョゃゅょァィゥェォぁぃぅぇぉ]?|[\\p{L}\\p{M}]+)"));
	QStringList result;
	auto matches = units.globalMatch(text);
	while (matches.hasNext())
		result << matches.next().captured();
	return result;
}
SVSLyricEditor::SVSLyricEditor(SVSClip* clip, const QSet<QString>& selection, QWidget* parent)
	: QDialog(parent)
	, m_clip(clip)
	, m_original(clip->notes())
	, m_selection(selection)
{
	setObjectName("svsBatchLyrics");
	setWindowTitle(QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Batch lyrics"));
	resize(620, 460);
	auto* layout = new QVBoxLayout(this);
	m_text = new QPlainTextEdit(this);
	m_text->setObjectName("svsBatchLyricText");
	layout->addWidget(m_text);
	m_skip = new QCheckBox(
		QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Skip voice continuation notes and tokens"), this);
	m_skip->setObjectName("svsBatchSkipContinuation");
	m_skip->setChecked(true);
	layout->addWidget(m_skip);
	m_table = new QTableWidget(this);
	m_table->setObjectName("svsBatchLyricPreview");
	m_table->setColumnCount(4);
	m_table->setHorizontalHeaderLabels({QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Tick"),
		QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Original"),
		QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Preview"),
		QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Reading / status")});
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	layout->addWidget(m_table, 1);
	m_diagnostic = new QLabel(this);
	m_diagnostic->setWordWrap(true);
	layout->addWidget(m_diagnostic);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &SVSLyricEditor::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	QVector<svs::Note> sorted = m_original;
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
	QStringList initial;
	for (const auto& note : sorted)
		if (m_selection.contains(note.id))
			initial << note.lyric;
	m_text->setPlainText(initial.join(' '));
	m_text->installEventFilter(this);
	connect(m_text, &QPlainTextEdit::textChanged, this, [this] { preview(); });
	connect(m_skip, &QCheckBox::toggled, this, [this] { preview(); });
	preview();
	if (clip)
		connect(clip, &QObject::destroyed, this, &QDialog::reject);
}
void SVSLyricEditor::preview()
{
	if (!m_clip)
		return;
	m_preview = m_original;
	const auto marker = static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().continuation;
	auto tokens = splitLyrics(m_text->toPlainText());
	if (m_skip->isChecked() && !marker.isEmpty())
		tokens.removeAll(marker);
	QVector<int> indices;
	for (int i = 0; i < m_original.size(); ++i)
		if (m_selection.contains(m_original[i].id))
			indices << i;
	std::stable_sort(
		indices.begin(), indices.end(), [this](int a, int b) { return m_original[a].tick < m_original[b].tick; });
	m_table->setRowCount(indices.size());
	int token = 0, row = 0;
	for (int index : indices)
	{
		const auto& before = m_original[index];
		auto& after = m_preview[index];
		const bool skip = m_skip->isChecked() && !marker.isEmpty() && before.lyric == marker;
		if (!skip && token < tokens.size())
			after.lyric = tokens[token++];
		const auto reading = editorPronunciation(m_clip, after);
		QString status
			= skip ? QCoreApplication::translate("lmms::gui::SVSLyricEditor", "Skipped continuation") : reading.text;
		if (!after.phonemes.isEmpty() || !after.pronunciation.isEmpty())
			status += QCoreApplication::translate("lmms::gui::SVSLyricEditor", " — manual override retained");
		if (!reading.diagnostic.isEmpty())
			status += "\n" + nativeTranslation::svsPronunciationDiagnostic(reading.diagnostic);
		const QStringList cells{QString::number(before.tick), before.lyric, after.lyric, status};
		for (int column = 0; column < 4; ++column)
		{
			auto* item = new QTableWidgetItem(cells[column]);
			item->setToolTip(cells[column]);
			m_table->setItem(row, column, item);
		}
		++row;
	}
	m_diagnostic->setText(QCoreApplication::translate("lmms::gui::SVSLyricEditor",
		"%1 tokens assigned; %2 unused. Manual readings and phonemes are preserved. Changes apply together on "
		"confirmation.")
			.arg(token)
			.arg(tokens.size() - token));
}
void SVSLyricEditor::accept()
{
	if (m_composing)
	{
		m_text->setFocus();
		return;
	}
	if (!m_clip)
	{
		reject();
		return;
	}
	if (m_clip->notes() != m_original)
	{
		m_diagnostic->setText(QCoreApplication::translate("lmms::gui::SVSLyricEditor",
			"Notes changed while this preview was open. Reopen batch lyrics to review the current notes."));
		return;
	}
	// QPlainTextEdit keeps IME preedit separate from committed text; confirming a composition never writes a partial lyric.
	preview();
	m_clip->setNotes(m_preview);
	QDialog::accept();
}
bool SVSLyricEditor::eventFilter(QObject* object, QEvent* event)
{
	if (object == m_text && event->type() == QEvent::InputMethod)
		m_composing = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
	return QDialog::eventFilter(object, event);
}
}
