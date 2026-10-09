#include <QtTest>
#include <QJsonDocument>
#include <QDataStream>
#include <QCryptographicHash>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SampleBuffer.h"
#include "PathUtil.h"
#include <QLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QToolButton>
#include <QAction>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <tlhelp32.h>
#endif
#include "SVSParameterPanel.h"
#include "SVSProjectController.h"
#include "SVSProjectExport.h"
#include "SVSProjectOutput.h"
#include "SVSProjectImportDialog.h"
#include "SVSSettingsPage.h"
#include "PluginBrowser.h"
#include "SetupDialog.h"
#include <QTabWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QDomDocument>
#include <QFileInfo>
#include "Mixer.h"
#include <cmath>
#include "Engine.h"
#include "AudioEngine.h"
#include "AudioDummy.h"
#include "ConfigManager.h"
#include <QTemporaryDir>
#include <QJsonDocument>
#include "Song.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSViews.h"
#include "MainWindow.h"
#include "SubWindow.h"
#include "GuiApplication.h"
#include <QDialog>
#include "SVSCanvas.h"
#include "SVSCurve.h"
#include "SVSResultStrip.h"
#include "SVSLyricEditor.h"
#include "SVSImageLoader.h"
#include "SVSNoteOperations.h"
#include "SVSSynthesisScheduler.h"
#include "SVSCache.h"
#include "SVSTimeMapping.h"
#include "SVSTempoSnapshot.h"
#include "SVSTempoSource.h"
#include "AutomationClip.h"
#include "PatternTrack.h"
#include <QSlider>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QInputMethodEvent>
#include <QHelpEvent>
#include <QMimeData>
#include <QClipboard>
#include "ProjectJournal.h"
#include "PluginFactory.h"
#include "Ladspa2LMMS.h"
#include <QScopeGuard>
#include "../../src/gui/editors/svs/operations/SVSCurveGesture.h"
#include "../../src/gui/editors/svs/operations/SVSFeedbackPitch.h"
#include "../../src/gui/editors/svs/SVSPitchRanges.h"
#include "PatternStore.h"
#include "SampleFrame.h"
#include <QSemaphore>
#include <QRunnable>
#include "SVSExportSnapshot.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include <QSignalSpy>
#include <QtEndian>
#include <bit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLibrary>
#include <cstdlib>
#include "svs.hpp"
#include "DataFile.h"
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QStandardPaths>
#include <QDateTime>
using namespace lmms;
// Export restores and starts the previous device. These tests advance audio
// periods themselves, so a restored dummy must not add concurrent periods.
class SVSManualAudioDevice final : public AudioDummy
{
public:
	using AudioDummy::AudioDummy;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};
class SVSIntegrationTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configuration;
	std::unique_ptr<gui::GuiApplication> m_guiApplication;
	static QVector<float> wavePCM(const QString& path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return {};
		const auto bytes = file.readAll();
		QVector<float> samples;
		for (qint64 offset = 12; offset + 8 <= bytes.size();)
		{
			const auto size = qFromLittleEndian<quint32>(bytes.constData() + offset + 4);
			if (offset + 8 + size > bytes.size())
				break;
			if (bytes.mid(offset, 4) == "data")
			{
				for (qint64 at = offset + 8; at + 4 <= offset + 8 + size; at += 4)
					samples.push_back(std::bit_cast<float>(qFromLittleEndian<quint32>(bytes.constData() + at)));
				break;
			}
			offset += 8 + size + (size & 1);
		}
		return samples;
	}
private slots:
	void initTestCase()
	{
		QVERIFY(m_configuration.isValid());
		ConfigManager::inst()->loadConfigFile(m_configuration.filePath("svs-test-config.xml"));
		if (qEnvironmentVariableIsSet("SVS_EMBEDDED_GUI_TEST"))
		{
			if (qEnvironmentVariableIsSet("SVS_TEST_AVATAR_PATH"))
				ConfigManager::inst()->setValue("svs", "testAvatarPath", qEnvironmentVariable("SVS_TEST_AVATAR_PATH"));
			if (qEnvironmentVariableIsSet("SVS_TEST_PORTRAIT_PATH"))
				ConfigManager::inst()->setValue(
					"svs", "testPortraitPath", qEnvironmentVariable("SVS_TEST_PORTRAIT_PATH"));
			ConfigManager::inst()->setWorkingDir(m_configuration.path() + "/");
			ConfigManager::inst()->setValue("app", "configured", "1");
			ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
			m_guiApplication = std::make_unique<gui::GuiApplication>();
		}
		else
			Engine::init(true);
		bool available = false;
		Engine::audioEngine()->setAudioDevice(new SVSManualAudioDevice(available, Engine::audioEngine()), false);
		svs::Registry::instance().voices();
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(), 30000);
	}
	void init()
	{
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(), 30000);
		Engine::projectJournal()->clearJournal();
	}
	void cleanupTestCase()
	{
		if (m_guiApplication)
		{
			delete static_cast<QWidget*>(m_guiApplication->mainWindow());
			m_guiApplication.reset();
		}
		else
			Engine::destroy();
	}
	void nativeLadspaPluginHost()
	{
#ifdef Q_OS_WIN
		if (!m_guiApplication || qEnvironmentVariable("LMMS_PLUGIN_DIR").isEmpty())
			QSKIP("Explicit native development plugin directory and GUI required");
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		// DLLs import lmms.exe. They must resolve the initialized test Engine,
		// rather than loading the development executable as a second PE image.
		QCOMPARE(GetModuleHandleW(L"lmms.exe"), GetModuleHandleW(nullptr));
		QVERIFY(Engine::getLADSPAManager());
		const auto plugin = PluginFactory::instance()->pluginInfo("ladspaeffect");
		QVERIFY(!plugin.isNull());
		QCOMPARE(plugin.file.canonicalFilePath(),
			QFileInfo(qEnvironmentVariable("LMMS_PLUGIN_DIR") + "/ladspaeffect.dll").canonicalFilePath());
		QVERIFY(plugin.descriptor->subPluginFeatures);
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		plugin.descriptor->subPluginFeatures->listSubPluginKeys(plugin.descriptor, keys);
		QVERIFY(!keys.isEmpty());
		auto* window = static_cast<QWidget*>(m_guiApplication->mainWindow());
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		QTest::qWait(700);
		QVERIFY(window->screen()->grabWindow(window->winId()).save("doc/svs/validation/LADSPA-test-native-window.png"));
		qInfo() << "LADSPA keys" << keys.size() << "native plugin host shares the initialized Engine";
#else
		QSKIP("Windows executable import regression");
#endif
	}
	void projectExportFileGroupTransaction()
	{
		QTemporaryDir staging, destination;
		QVERIFY(staging.isValid());
		QVERIFY(destination.isValid());
		QVERIFY(QDir().mkdir(staging.filePath("nested")));
		auto write = [](const QString& path, const QByteArray& bytes) {
			QFile file(path);
			return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
		};
		auto read = [](const QString& path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
		};
		const QStringList names{"a-project.json", "b-audio.wav", "nested/data.bin"};
		QStringList files;
		for (const auto& name : names)
		{
			const auto path = staging.filePath(name);
			QVERIFY(write(path, "new-" + name.toUtf8()));
			files << path;
		}
		QVERIFY(write(destination.filePath(names[0]), "old-project"));
		QVERIFY(write(destination.filePath(names[1]), "old-audio"));
		const auto plan = svs::ProjectOutput::prepare(files, staging.path(), destination.path());
		QVERIFY2(plan.valid(), qPrintable(plan.error));
		QCOMPARE(plan.overwrites.size(), 2);
		QString error;
		std::atomic<bool> cancelled{true};
		QVERIFY(!svs::ProjectOutput::commit(plan, error, &cancelled));
		QCOMPARE(read(destination.filePath(names[0])), QByteArray("old-project"));
#ifdef Q_OS_WIN
		const auto lockedPath = QDir::toNativeSeparators(destination.filePath(names[1]));
		const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()), GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		QVERIFY(locked != INVALID_HANDLE_VALUE);
		auto close = qScopeGuard([&] { CloseHandle(locked); });
		QVERIFY(!svs::ProjectOutput::commit(plan, error));
		CloseHandle(locked);
		close.dismiss();
		QCOMPARE(read(destination.filePath(names[0])), QByteArray("old-project"));
		QCOMPARE(read(destination.filePath(names[1])), QByteArray("old-audio"));
		QVERIFY(!QFileInfo::exists(destination.filePath(names[2])));
#endif
		QVERIFY2(svs::ProjectOutput::commit(plan, error), qPrintable(error));
		for (const auto& name : names)
			QCOMPARE(read(destination.filePath(name)), "new-" + name.toUtf8());
		QVERIFY(QDir(destination.path())
				.entryList({".lmms-svs-export-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
				.isEmpty());
		QVERIFY(write(staging.filePath("new-name.txt"), "new"));
		const auto changed
			= svs::ProjectOutput::prepare({staging.filePath("new-name.txt")}, staging.path(), destination.path());
		QVERIFY(changed.valid());
		QVERIFY(write(destination.filePath("new-name.txt"), "appeared-after-confirmation"));
		QVERIFY(!svs::ProjectOutput::commit(changed, error));
		QCOMPARE(read(destination.filePath("new-name.txt")), QByteArray("appeared-after-confirmation"));
		QTemporaryDir outside;
		QVERIFY(write(outside.filePath("outside.txt"), "outside"));
		QVERIFY(!svs::ProjectOutput::prepare({outside.filePath("outside.txt")}, staging.path(), destination.path())
				.valid());
	}
	void projectExportMappingAndAudio()
	{
		svs::ProjectExportSnapshot snapshot;
		snapshot.lastTick = 192;
		auto tempo = std::make_shared<svs::TempoSnapshot>();
		tempo->baseTempo = 120;
		svs::TempoSnapshot::Layer layer;
		layer.length = 192;
		layer.nodes = {{0, 120, 120}, {48, 240, 240}};
		tempo->layers.push_back(layer);
		snapshot.tempo = tempo;
		svs::ProjectExportTrack track;
		track.name = QStringLiteral("歌声-日本語-한국어");
		track.singing = true;
		track.muted = true;
		track.solo = true;
		track.volume = .5;
		track.pan = -.25;
		svs::ProjectExportClip clip;
		clip.name = "cropped";
		clip.position = 96;
		clip.contentOffset = 12;
		clip.length = 48;
		svs::Note first;
		first.id = "first";
		first.tick = 0;
		first.duration = 24;
		first.pitch = 60;
		first.lyric = QStringLiteral("你");
		first.pronunciation = "ni";
		svs::Note last = first;
		last.id = "last";
		last.tick = 36;
		last.duration = 48;
		last.pitch = 64;
		clip.notes = {first, last};
		svs::Curve pitch;
		pitch.id = "svs.pitch";
		pitch.mode = "absolute";
		pitch.unit = "semitone";
		pitch.evaluator.points = {{12, 60}, {24, 61}, {36, 60, 0, 0, true, true}, {48, 64}, {60, 64.5}};
		clip.curves["svs.pitch"] = pitch;
		track.clips.push_back(clip);
		svs::ProjectExportClip overlapping;
		overlapping.name = "overlap";
		overlapping.position = 100;
		overlapping.length = 24;
		first.tick = 0;
		first.duration = 24;
		first.lyric = QStringLiteral("あ");
		overlapping.notes = {first};
		track.clips.push_back(overlapping);
		snapshot.tracks.push_back(track);
		svs::ProjectExportTrack audioTrack;
		audioTrack.name = QStringLiteral("伴奏");
		std::vector<SampleFrame> frames(96000);
		for (size_t i = 0; i < frames.size(); ++i)
		{
			frames[i][0] = float(i) / 96000;
			frames[i][1] = -frames[i][0];
		}
		auto buffer = std::make_shared<SampleBuffer>(std::move(frames), 48000);
		svs::ProjectExportAudio audio;
		audio.name = "reversed-crop";
		audio.position = 48;
		audio.startOffset = -24;
		audio.length = 48;
		audio.reversed = true;
		audio.buffer = buffer;
		audioTrack.audio.push_back(audio);
		audio.name = "delayed";
		audio.position = 0;
		audio.startOffset = 12;
		audio.length = 36;
		audio.reversed = false;
		audioTrack.audio.push_back(audio);
		snapshot.tracks.push_back(audioTrack);
		const auto prepared = svs::ProjectExport::build(snapshot);
		QVERIFY2(prepared.valid(), qPrintable(prepared.error));
		const auto tracks = prepared.project["track_list"].toArray();
		QCOMPARE(tracks.size(), 3);
		const auto singing = tracks[0].toObject();
		QVERIFY(singing["mute"].toBool());
		QVERIFY(singing["solo"].toBool());
		QCOMPARE(singing["volume"].toDouble(), .5);
		QCOMPARE(singing["pan"].toDouble(), -.25);
		const auto notes = singing["note_list"].toArray();
		QCOMPARE(notes.size(), 3);
		QCOMPARE(notes[0].toObject()["start_pos"].toInt(), 960);
		QCOMPARE(notes[0].toObject()["length"].toInt(), 120);
		QCOMPARE(notes[0].toObject()["pronunciation"].toString(), QString("ni"));
		QCOMPARE(notes[1].toObject()["start_pos"].toInt(), 1000);
		QCOMPARE(notes[2].toObject()["start_pos"].toInt(), 1200);
		QCOMPARE(notes[2].toObject()["length"].toInt(), 240);
		const auto tempos = prepared.project["song_tempo_list"].toArray();
		QCOMPARE(tempos.size(), 2);
		QCOMPARE(tempos[1].toObject()["position"].toInt(), 480);
		QCOMPARE(tempos[1].toObject()["bpm"].toInt(), 240);
		const auto imported = svs::ProjectMapper::prepareImport(
			prepared.project, {"missing-engine", "missing-voice", "test", "zh"}, {});
		QVERIFY2(imported.valid(), qPrintable(imported.error));
		const auto savedCurve = QJsonDocument::fromJson(
			imported.document.elementsByTagName("svsclip").at(0).toElement().attribute("curves").toUtf8())
									.object()["svs.pitch"]
									.toObject();
		svs::Curve reread;
		QString error;
		QVERIFY(svs::Curve::fromJson(savedCurve, reread, error));
		QCOMPARE(reread.valueAt(108)->toDouble(), 61.);
		QVERIFY(!reread.valueAt(126));
		QCOMPARE(reread.valueAt(144)->toDouble(), 64.5);
		QCOMPARE(prepared.audioFiles.size(), 2);
		QCOMPARE(prepared.audioFiles[0].firstFrame, qint64(12000));
		QCOMPARE(prepared.audioFiles[0].frameCount, qint64(12000));
		QCOMPARE(prepared.audioFiles[1].firstFrame, qint64(0));
		QCOMPARE(prepared.audioFiles[1].frameCount, qint64(12000));
		QCOMPARE(tracks[1].toObject()["offset"].toInt(), 480);
		QCOMPARE(tracks[2].toObject()["offset"].toInt(), 120);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QVERIFY2(svs::ProjectExport::writeAudioFiles(prepared, directory.path(), error), qPrintable(error));
		const auto reversed = wavePCM(directory.filePath(prepared.audioFiles[0].fileName));
		QCOMPARE(reversed.size(), 24000);
		QCOMPARE(reversed[0], buffer->data()[83999][0]);
		QCOMPARE(reversed[1], buffer->data()[83999][1]);
		QCOMPARE(reversed[23998], buffer->data()[72000][0]);
		std::atomic<bool> cancelled{true};
		QVERIFY(!svs::ProjectExport::build(snapshot, &cancelled).valid());
		QTemporaryDir cancelledDirectory;
		QVERIFY(!svs::ProjectExport::writeAudioFiles(prepared, cancelledDirectory.path(), error, &cancelled));
		QVERIFY(QDir(cancelledDirectory.path()).entryList(QDir::Files).isEmpty());
		svs::ProjectBridge bridge(nullptr, QStringLiteral(LMMS_SVS_PROJECT_RUNTIME));
		QSignalSpy converted(&bridge, &svs::ProjectBridge::finished);
		QJsonArray assets;
		for (const auto& file : prepared.audioFiles)
			assets.append(QJsonObject{{"source", directory.filePath(file.fileName)}, {"name", file.fileName}});
		const auto exportedName = QStringLiteral("导出-日本語-한국어.json");
		QVERIFY(bridge.start({{"operation", "exportProject"}, {"formatId", "json"}, {"path", exportedName},
			{"project", prepared.project}, {"assets", assets}}));
		QTRY_COMPARE_WITH_TIMEOUT(converted.size(), 1, 30000);
		const auto response = converted[0][0].toJsonObject();
		QVERIFY2(response["status"].toString() == "success", qPrintable(QJsonDocument(response).toJson()));
		QCOMPARE(response["files"].toArray().size(), 3);
		QTemporaryDir delivered;
		QVERIFY(delivered.isValid());
		for (const auto& entry : response["files"].toArray())
			QVERIFY(QFile::copy(entry.toString(), delivered.filePath(QFileInfo(entry.toString()).fileName())));
		bridge.releaseTask();
		converted.clear();
		QVERIFY(bridge.start(
			{{"operation", "importProject"}, {"formatId", "json"}, {"path", delivered.filePath(exportedName)}}));
		QTRY_COMPARE_WITH_TIMEOUT(converted.size(), 1, 30000);
		const auto importedResponse = converted[0][0].toJsonObject();
		QVERIFY2(
			importedResponse["status"].toString() == "success", qPrintable(QJsonDocument(importedResponse).toJson()));
		const auto importedTracks = importedResponse["project"].toObject()["track_list"].toArray();
		QCOMPARE(importedTracks.size(), 3);
		const auto readNotes = importedTracks[0].toObject()["note_list"].toArray();
		QCOMPARE(readNotes.size(), notes.size());
		for (int index = 0; index < notes.size(); ++index)
		{
			const auto expected = notes[index].toObject(), actual = readNotes[index].toObject();
			for (auto field = expected.begin(); field != expected.end(); ++field)
				QCOMPARE(actual[field.key()], field.value());
		}
		QCOMPARE(importedTracks[1].toObject()["audio_file_path"].toString(),
			QDir::toNativeSeparators(delivered.filePath("audio-1.wav")));
		bridge.releaseTask();
	}
	void projectExportSnapshotIsReadOnly()
	{
		auto* song = Engine::getSong();
		song->clearProject();
		auto* track = new SVSTrack(song);
		track->setName(QStringLiteral("只读快照"));
		track->bindVoice("missing-engine", "missing-voice");
		track->setMuted(true);
		auto* clip = static_cast<SVSClip*>(track->createClip(96));
		svs::Note note;
		note.id = "snapshot-note";
		note.tick = 12.25;
		note.duration = 36.5;
		note.pitch = 60;
		note.lyric = "original";
		clip->setNotes({note});
		clip->setAutoResize(false);
		clip->changeLength(48);
		clip->setStartTimeOffset(-12);
		auto notes = clip->notes();
		notes[0].lyric = "edited";
		clip->setEditorData(notes, clip->curves());
		auto* audioTrack = new SampleTrack(song);
		auto* audio = static_cast<SampleClip*>(audioTrack->createClip(192));
		std::vector<SampleFrame> frames(480);
		frames[0][0] = .25f;
		audio->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(frames), 48000));
		audio->setAutoResize(false);
		audio->changeLength(24);
		audio->setStartTimeOffset(-6);
		audio->setReversed(true);
		Track::create(Track::Type::Automation, song);
		song->setModified(true);
		const auto filename = song->projectFileName();
		DataFile before(DataFile::Type::JournalData);
		song->saveProjectState(before);
		const auto snapshot = svs::ProjectExport::capture(*song);
		QVERIFY2(snapshot.valid(), qPrintable(snapshot.error));
		QCOMPARE(snapshot.tracks.size(), 2);
		QVERIFY(snapshot.tracks[0].muted);
		QVERIFY(snapshot.tracks[0].singing);
		QCOMPARE(snapshot.tracks[0].clips[0].position, 96.);
		QCOMPARE(snapshot.tracks[0].clips[0].contentOffset, 12.);
		QCOMPARE(snapshot.tracks[0].clips[0].notes[0].tick, 12.25);
		QCOMPARE(snapshot.tracks[0].clips[0].notes[0].lyric, QString("edited"));
		QVERIFY(!clip->audio());
		QCOMPARE(snapshot.tracks[1].audio[0].startOffset, -6.);
		QVERIFY(snapshot.tracks[1].audio[0].reversed);
		QCOMPARE(snapshot.tracks[1].audio[0].buffer->size(), size_t(480));
		QCOMPARE(snapshot.tracks[1].audio[0].buffer->data()[0][0], .25f);
		DataFile after(DataFile::Type::JournalData);
		song->saveProjectState(after);
		QCOMPARE(after.toByteArray(), before.toByteArray());
		QCOMPARE(song->projectFileName(), filename);
		QVERIFY(song->isModified());
		Engine::projectJournal()->undo();
		QCOMPARE(clip->notes()[0].lyric, QString("original"));
		QCOMPARE(snapshot.tracks[0].clips[0].notes[0].lyric, QString("edited"));
		audio->setSampleBuffer(SampleBuffer::emptyBuffer());
		QCOMPARE(snapshot.tracks[1].audio[0].buffer->size(), size_t(480));
		song->clearProject();
	}
	void projectImportPersistenceAndRollback()
	{
		auto* song = Engine::getSong();
		song->clearProject();
		svs::Voice voice;
		for (const auto& candidate : svs::Registry::instance().voices())
			if (candidate.id == "full")
			{
				voice = candidate;
				break;
			}
		QVERIFY(!voice.id.isEmpty());
		const auto project = QJsonDocument::fromJson(
			R"({"song_tempo_list":[{"position":0,"bpm":120},{"position":960,"bpm":150}],"time_signature_list":[{"bar_index":0,"numerator":4,"denominator":4}],"track_list":[{"type_":"Singing","title":"中文歌声","note_list":[{"start_pos":961,"length":479,"key_number":60,"lyric":"你","pronunciation":"ni"}],"edited_params":{"pitch":{"points":[[-192000,-100],[2881,-100],[2881,6000],[3121,6050],[3181,5980],[3241,6020],[3301,5980],[3361,5980],[3361,-100],[3600,6400],[3840,6420],[3840,-100],[1073741823,-100]]}}},{"type_":"Singing","title":"日本語 한국어","note_list":[{"start_pos":480,"length":480,"key_number":64,"lyric":"あ"}],"edited_params":{}}]})")
								 .object();
		const svs::ProjectVoice binding{voice.pluginId, voice.id, voice.name, voice.language};
		const auto imported = svs::ProjectMapper::prepareImport(project, binding, {});
		QVERIFY2(imported.valid(), qPrintable(imported.error));
		QString error;
		QVERIFY2(gui::SVSProjectController::commitImport(imported, *song, error), qPrintable(error));
		QCOMPARE(song->tracks().size(), size_t(2));
		QVERIFY(song->isModified());
		QVERIFY(song->projectFileName().isEmpty());
		for (auto* base : song->tracks())
		{
			auto* track = dynamic_cast<SVSTrack*>(base);
			QVERIFY(track);
			QCOMPARE(track->voice().pluginId, voice.pluginId);
			QCOMPARE(track->voice().id, voice.id);
		}
		auto* track = static_cast<SVSTrack*>(song->tracks().front());
		auto* clip = static_cast<SVSClip*>(track->getClip(0));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(std::abs(clip->notes().first().tick - 96.1) < 1e-9);
		QCOMPARE(clip->notes().first().pronunciation, QString("ni"));
		const auto curve = clip->curves().value("svs.pitch");
		QVERIFY(std::abs(curve.valueAt(108.1)->toDouble() - 60.25) < 1e-6);
		QVERIFY(!curve.valueAt(155));
		QVERIFY(std::abs(curve.valueAt(126.1)->toDouble() - 59.8) < 1e-6);
		QVERIFY(std::abs(curve.valueAt(132.1)->toDouble() - 60.2) < 1e-6);
		const auto input = clip->captureInput(48000);
		QCOMPARE(input.document["curves"].toObject()["svs.pitch"].toObject(), curve.toJson());
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto tempo = svs::TempoSnapshot::capture(*song, song->getTempo());
		QCOMPARE(tempo->tempoAt(0), 120);
		QCOMPARE(tempo->tempoAt(95), 120);
		QCOMPARE(tempo->tempoAt(96), 150);
		const auto native = m_configuration.filePath(QStringLiteral("导入-日本語-한국어.mmp"));
		QVERIFY(song->saveProjectFile(native));
		song->loadProject(native);
		QVERIFY(!song->hasErrors());
		track = static_cast<SVSTrack*>(song->tracks().front());
		clip = static_cast<SVSClip*>(track->getClip(0));
		QCOMPARE(clip->curves().value("svs.pitch").toJson(), curve.toJson());
		QVERIFY(std::abs(clip->notes().first().tick - 96.1) < 1e-9);
		QCOMPARE(clip->notes().first().lyric, QStringLiteral("你"));
		auto changed = clip->curves();
		changed["svs.pitch"].insert(108.1, 61.);
		clip->setEditorData(clip->notes(), changed);
		QVERIFY(std::abs(clip->curves()["svs.pitch"].valueAt(108.1)->toDouble() - 61) < 1e-6);
		Engine::projectJournal()->undo();
		QCOMPARE(clip->curves().value("svs.pitch").toJson(), curve.toJson());
		song->setModified(true);
		const auto fileName = song->projectFileName();
		const auto beforeName = track->name();
		auto badProject = project;
		auto tracks = project["track_list"].toArray();
		tracks.append(QJsonObject{
			{"type_", "Instrumental"}, {"title", "missing"}, {"audio_file_path", "disappeared.wav"}, {"offset", 0}});
		badProject["track_list"] = tracks;
		const auto failed = svs::ProjectMapper::prepareImport(
			badProject, binding, {{"disappeared.wav", {m_configuration.filePath("disappeared.wav"), 96}}});
		QVERIFY(failed.valid());
		QVERIFY(!gui::SVSProjectController::commitImport(failed, *song, error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(song->tracks().size(), size_t(2));
		QCOMPARE(song->projectFileName(), fileName);
		QVERIFY(song->isModified());
		QCOMPARE(song->tracks().front()->name(), beforeName);
		QCOMPARE(static_cast<SVSClip*>(song->tracks().front()->getClip(0))->curves().value("svs.pitch").toJson(),
			curve.toJson());
		song->clearProject();
	}
	void projectImportNativeWindows()
	{
		if (!m_guiApplication)
		{
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert("SVS_EMBEDDED_GUI_TEST", "1");
			environment.insert("QT_QPA_PLATFORM", "windows");
			const auto report = m_configuration.filePath("project-native-child.txt");
			QProcess child;
			child.setProcessEnvironment(environment);
			child.start(QCoreApplication::applicationFilePath(),
				{"projectImportNativeWindows", "-o", report + ",txt", "-o", "-,txt"});
			QVERIFY(child.waitForStarted(5000));
			QVERIFY(child.waitForFinished(30000));
			QFile result(report);
			QVERIFY(result.open(QIODevice::ReadOnly));
			const auto output = result.readAll() + child.readAllStandardError();
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
			return;
		}
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		auto* window = m_guiApplication->mainWindow();
		window->resize(1280, 800);
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		auto* menu = window->findChild<QMenu*>("svsProjectMenu");
		QVERIFY(menu);
		QCOMPARE(menu->title(), QStringLiteral("SVS 工程"));
		QCOMPARE(menu->actions().size(), 2);
		QCOMPARE(menu->actions()[0]->text(), QStringLiteral("导入SVS工程"));
		QCOMPARE(menu->actions()[1]->text(), QStringLiteral("导出SVS工程"));
		menu->popup(window->mapToGlobal(QPoint(100, 80)));
		QTest::qWait(600);
		QVERIFY(menu->isVisible());
		QVERIFY(menu->screen()->grabWindow(menu->winId()).save("doc/svs/project/M2-native-menu.png"));
		menu->hide();
		const QJsonObject format{{"id", "svp"}, {"name", "Synthesizer V"},
			{"inputDefaults", QJsonObject{{"import_pitch", true}, {"pitch", "full"}}},
			{"inputSchema",
				QJsonObject{{"properties",
					QJsonObject{{"import_pitch", QJsonObject{{"title", "Import pitch"}, {"type", "boolean"}}},
						{"pitch", QJsonObject{{"title", "Pitch mode"}, {"enum", QJsonArray{"plain", "full"}}}}}}}}};
		auto* song = Engine::getSong();
		const bool modified = song->isModified();
		const auto fileName = song->projectFileName();
		const auto count = song->tracks().size();
		gui::SVSProjectImportDialog dialog(format, window);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		auto* choices = dialog.findChild<QComboBox*>("svsProjectDefaultVoice");
		QVERIFY(choices);
		QVERIFY(choices->count() > 0);
		const auto selected = choices->currentData().toList();
		QCOMPARE(selected.size(), 2);
		QVERIFY(!choices->currentText().isEmpty());
		QVERIFY(dialog.options()["import_pitch"].toBool());
		QCOMPARE(dialog.options()["pitch"].toString(), QString("full"));
		QTest::qWait(600);
		QVERIFY(dialog.screen()->grabWindow(dialog.winId()).save("doc/svs/project/M2-native-voice-dialog.png"));
		dialog.reject();
		QCOMPARE(song->tracks().size(), count);
		QCOMPARE(song->projectFileName(), fileName);
		QCOMPARE(song->isModified(), modified);
		// Confirming the dialog only selects a stable identity; it never imports tracks.
		dialog.accept();
		QCOMPARE(dialog.result(), int(QDialog::Accepted));
		QCOMPARE(dialog.selectedVoice().pluginId, selected[0].toString());
		QCOMPARE(dialog.selectedVoice().voiceId, selected[1].toString());
		QCOMPARE(song->tracks().size(), count);
		const auto project = QJsonDocument::fromJson(
			R"({"song_tempo_list":[{"position":0,"bpm":120}],"time_signature_list":[{"bar_index":0,"numerator":4,"denominator":4}],"track_list":[{"type_":"Singing","title":"原工程音高线","note_list":[{"start_pos":480,"length":480,"key_number":60,"lyric":"你好"}],"edited_params":{"pitch":{"points":[[-192000,-100],[2400,6000],[2520,6050],[2640,5980],[2760,6030],[2880,6000],[2880,-100],[3360,6200],[3600,6250],[3600,-100],[1073741823,-100]]}}}]})")
								 .object();
		QString error;
		const auto prepared = svs::ProjectMapper::prepareImport(project, dialog.selectedVoice(), {});
		QVERIFY2(gui::SVSProjectController::commitImport(prepared, *song, error), qPrintable(error));
		auto* track = static_cast<SVSTrack*>(song->tracks().front());
		auto* clip = static_cast<SVSClip*>(track->getClip(0));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		// Exercise the imported curve at a nonzero clip position through production UI.
		clip->movePosition(192);
		const auto input = clip->captureInput(48000);
		QCOMPARE(input.document["position"].toDouble(), 192.);
		const auto original = clip->curves();
		gui::SVSPianoRoll editor(clip);
		editor.resize(1100, 660);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		QTest::qWait(600);
		QVERIFY(editor.screen()->grabWindow(editor.winId()).save("doc/svs/project/M2-native-imported-pitch.png"));
		auto changed = original;
		changed["svs.pitch"].insert(60, 61.);
		clip->setEditorData(clip->notes(), changed);
		QVERIFY(std::abs(clip->curves()["svs.pitch"].valueAt(60)->toDouble() - 61) < 1e-6);
		Engine::projectJournal()->undo();
		QCOMPARE(clip->curves(), original);
		editor.close();
		song->clearProject();
		window->hide();
	}
	void projectImportNoVoices()
	{
		if (!qEnvironmentVariableIsSet("SVS_PROJECT_NO_VOICES_TEST"))
		{
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert("SVS_EMBEDDED_GUI_TEST", "1");
			environment.insert("SVS_PROJECT_NO_VOICES_TEST", "1");
			environment.insert("QT_QPA_PLATFORM", "windows");
			environment.insert("LMMS_SVS_PLUGIN_DIR", m_configuration.filePath("empty-catalog"));
			const auto report = m_configuration.filePath("project-empty-voices-child.txt");
			QProcess child;
			child.setProcessEnvironment(environment);
			child.start(QCoreApplication::applicationFilePath(),
				{"projectImportNoVoices", "-o", report + ",txt", "-o", "-,txt"});
			QVERIFY(child.waitForStarted(5000));
			QVERIFY(child.waitForFinished(30000));
			QFile result(report);
			QVERIFY(result.open(QIODevice::ReadOnly));
			const auto output = result.readAll() + child.readAllStandardError();
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
			return;
		}
		QVERIFY(m_guiApplication);
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		QVERIFY(svs::Registry::instance().voices().isEmpty());
		auto* song = Engine::getSong();
		const auto count = song->tracks().size();
		const auto modified = song->isModified();
		const auto fileName = song->projectFileName();
		gui::SVSProjectImportDialog dialog(QJsonObject{{"name", "JSON"}}, m_guiApplication->mainWindow());
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		auto* buttons = dialog.findChild<QDialogButtonBox*>();
		QVERIFY(buttons);
		QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
		QVERIFY(dialog.findChild<QLabel*>("svsProjectVoiceStatus")->text().contains(QStringLiteral("没有可用声库")));
		dialog.accept();
		QCOMPARE(dialog.result(), int(QDialog::Rejected));
		dialog.reject();
		QCOMPARE(song->tracks().size(), count);
		QCOMPARE(song->isModified(), modified);
		QCOMPARE(song->projectFileName(), fileName);
	}
	void projectImportControllerFlow()
	{
		if (!m_guiApplication)
		{
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert("SVS_EMBEDDED_GUI_TEST", "1");
			environment.insert("QT_QPA_PLATFORM", "windows");
			const auto report = m_configuration.filePath("project-controller-child.txt");
			QProcess child;
			child.setProcessEnvironment(environment);
			child.start(QCoreApplication::applicationFilePath(),
				{"projectImportControllerFlow", "-o", report + ",txt", "-o", "-,txt"});
			QVERIFY(child.waitForStarted(5000));
			QVERIFY(child.waitForFinished(120000));
			QFile result(report);
			QVERIFY(result.open(QIODevice::ReadOnly));
			const auto output = result.readAll() + child.readAllStandardError();
			QFile archived("doc/svs/project/M2-controller-child-latest.txt");
			if (archived.open(QIODevice::WriteOnly))
				archived.write(output);
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
			return;
		}
		auto* window = m_guiApplication->mainWindow();
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		auto* song = Engine::getSong();
		song->clearProject();
		const auto wave = m_configuration.filePath(QStringLiteral("伴奏-日本語-한국어.wav"));
		QFile audio(wave);
		QVERIFY(audio.open(QIODevice::WriteOnly));
		QDataStream stream(&audio);
		stream.setByteOrder(QDataStream::LittleEndian);
		stream.writeRawData("RIFF", 4);
		stream << quint32(96036);
		stream.writeRawData("WAVEfmt ", 8);
		stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2)
			   << quint16(16);
		stream.writeRawData("data", 4);
		stream << quint32(96000);
		for (int i = 0; i < 48000; ++i)
			stream << qint16(1000 * std::sin(i * .03));
		audio.close();
		auto project = QJsonDocument::fromJson(
			R"({"song_tempo_list":[{"position":0,"bpm":120},{"position":960,"bpm":150}],"time_signature_list":[{"bar_index":0,"numerator":4,"denominator":4}],"track_list":[{"type_":"Singing","title":"完整导入","note_list":[{"start_pos":480,"length":480,"key_number":60,"lyric":"你好"}],"edited_params":{"pitch":{"points":[[-192000,-100],[2400,6000],[2640,6050],[2880,6000],[2880,-100],[1073741823,-100]]}}}]})")
						   .object();
		auto tracks = project["track_list"].toArray();
		for (const auto offset : {0, 960})
			tracks.append(QJsonObject{{"type_", "Instrumental"}, {"title", QString("audio-%1").arg(offset)},
				{"audio_file_path", wave}, {"offset", offset}});
		project["track_list"] = tracks;
		const auto source = m_configuration.filePath(QStringLiteral("工程-日本語-한국어.json"));
		QFile file(source);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const auto original = QJsonDocument(project).toJson();
		file.write(original);
		file.close();
		auto* old = new SVSTrack(song);
		old->setName("original-project");
		auto* oldClip = static_cast<SVSClip*>(old->createClip(0));
		svs::Note note;
		note.id = "original-note";
		oldClip->setNotes({note});
		song->setModified(true);
		const QDir resources(ConfigManager::inst()->userSamplesDir() + "/svs-project");
		const auto resourceBaseline = resources.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		gui::SVSProjectController controller(window, QStringLiteral(LMMS_SVS_PROJECT_RUNTIME));
		QString choice = "cancel-unsaved";
		QString selectedPath = source;
		int filesShown = 0, voicesShown = 0, errorsShown = 0, unsavedShown = 0, lossesShown = 0;
		QTimer automation;
		QElapsedTimer elapsed;
		elapsed.start();
		QString previousModal;
		connect(&automation, &QTimer::timeout, this, [&] {
			auto* modal = QApplication::activeModalWidget();
			if (!modal)
				return;
			const auto identity = QString::number(quintptr(modal)) + ":" + modal->metaObject()->className() + ":"
				+ modal->objectName() + ":" + modal->windowTitle();
			if (identity != previousModal)
			{
				previousModal = identity;
				QFile trace("doc/svs/project/M2-controller-flow-events.txt");
				if (trace.open(QIODevice::WriteOnly | QIODevice::Append))
					trace.write((choice + " " + identity + "\n").toUtf8());
			}
			if (elapsed.elapsed() > 25000)
			{
				if (auto* dialog = qobject_cast<QDialog*>(modal))
					dialog->reject();
				return;
			}
			if (auto* picker = qobject_cast<QFileDialog*>(modal))
			{
				if (!picker->property("projectHandled").toBool())
				{
					picker->setProperty("projectHandled", true);
					++filesShown;
					QCOMPARE(picker->selectedNameFilter(), picker->nameFilters().first());
					QVERIFY(picker->selectedNameFilter().startsWith(QStringLiteral("所有支持格式 (")));
					QVERIFY(picker->selectedNameFilter().contains("*.json"));
					QVERIFY(picker->selectedNameFilter().contains("*.mxl"));
					QVERIFY(picker->selectedNameFilter().contains("*.vshp"));
					picker->setProperty("filterCapturePending", true);
					QTimer::singleShot(600, picker, [picker] {
						picker->setProperty("filterCapturePending", false);
						QVERIFY(picker->screen()->grabWindow(picker->winId())
							.save("doc/svs/project/supported-formats-native-import.png"));
					});
					picker->setDirectory(QFileInfo(selectedPath).absolutePath());
				}
				picker->selectFile(QFileInfo(selectedPath).fileName());
				auto* fileName = picker->findChild<QLineEdit*>("fileNameEdit");
				if (fileName)
					fileName->setText(QFileInfo(selectedPath).fileName());
				if (!picker->property("selectionLogged").toBool())
				{
					picker->setProperty("selectionLogged", true);
					QFile trace("doc/svs/project/M2-controller-flow-events.txt");
					if (trace.open(QIODevice::WriteOnly | QIODevice::Append))
						trace.write((picker->directory().path() + " | " + picker->selectedNameFilter() + " | "
							+ picker->selectedFiles().join('|') + "\n")
								.toUtf8());
				}
				if (!picker->property("filterCapturePending").toBool())
					QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
			}
			else if (modal->objectName() == "svsProjectImportDialog")
			{
				if (modal->property("projectHandled").toBool())
					return;
				modal->setProperty("projectHandled", true);
				++voicesShown;
				auto* buttons = modal->findChild<QDialogButtonBox*>();
				QVERIFY(buttons);
				if (choice == "cancel-voice")
					buttons->button(QDialogButtonBox::Cancel)->click();
				else
					buttons->button(QDialogButtonBox::Ok)->click();
			}
			else if (auto* message = qobject_cast<QMessageBox*>(modal))
			{
				if (message->standardButtons().testFlag(QMessageBox::Save))
				{
					++unsavedShown;
					message
						->button(choice == "cancel-unsaved" ? QMessageBox::Cancel
								: choice == "save-failure"	? QMessageBox::Save
															: QMessageBox::Discard)
						->click();
				}
				else if (message->standardButtons().testFlag(QMessageBox::Yes))
				{
					++lossesShown;
					QVERIFY(message->text().contains("pitch"));
					QVERIFY(message->text().contains("json"));
					QVERIFY(message->text().contains(QStringLiteral("完整导入")));
					message->button(QMessageBox::Cancel)->click();
				}
				else
				{
					++errorsShown;
					message->button(QMessageBox::Ok) ? message->button(QMessageBox::Ok)->click() : message->reject();
				}
			}
			else if (choice == "cancel-process")
			{
				if (auto* progress = qobject_cast<QProgressDialog*>(modal))
					QMetaObject::invokeMethod(progress, "canceled", Qt::QueuedConnection);
			}
		});
		automation.start(50);
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(filesShown, 1);
		QCOMPARE(voicesShown, 1);
		QCOMPARE(unsavedShown, 1);
		QCOMPARE(song->tracks().size(), size_t(1));
		QCOMPARE(song->tracks().front()->name(), QString("original-project"));
		QVERIFY(song->isModified());
		choice = "cancel-voice";
		elapsed.restart();
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(unsavedShown, 1);
		QCOMPARE(song->tracks().size(), size_t(1));
		QVERIFY(song->isModified());
		QCOMPARE(resources.entryList(QDir::Dirs | QDir::NoDotAndDotDot), resourceBaseline);
		auto noPitch = project;
		auto noPitchTracks = noPitch["track_list"].toArray();
		auto singing = noPitchTracks[0].toObject();
		singing.remove("edited_params");
		noPitchTracks[0] = singing;
		noPitch["track_list"] = noPitchTracks;
		selectedPath = m_configuration.filePath("missing-pitch.json");
		QFile noPitchFile(selectedPath);
		QVERIFY(noPitchFile.open(QIODevice::WriteOnly));
		noPitchFile.write(QJsonDocument(noPitch).toJson());
		noPitchFile.close();
		choice = "loss-cancel";
		elapsed.restart();
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(lossesShown, 1);
		QCOMPARE(song->tracks().size(), size_t(1));
		QVERIFY(song->isModified());
		QCOMPARE(resources.entryList(QDir::Dirs | QDir::NoDotAndDotDot), resourceBaseline);
		selectedPath = source;
		choice = "cancel-process";
		elapsed.restart();
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(song->tracks().size(), size_t(1));
		QVERIFY(song->isModified());
		QCOMPARE(resources.entryList(QDir::Dirs | QDir::NoDotAndDotDot), resourceBaseline);
		const auto blockedDirectory = m_configuration.filePath("blocked-save");
		QVERIFY(QDir().mkdir(blockedDirectory));
		const auto blockedSave = blockedDirectory + "/project.mmp";
		QVERIFY(song->guiSaveProjectAs(blockedSave));
		QVERIFY(QDir().rename(blockedDirectory, blockedDirectory + ".original"));
		QFile blockedParent(blockedDirectory);
		QVERIFY(blockedParent.open(QIODevice::WriteOnly));
		blockedParent.write("not a directory");
		blockedParent.close();
		song->setModified(true);
		const auto unsavedBeforeSave = unsavedShown;
		choice = "save-failure";
		elapsed.restart();
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(unsavedShown, unsavedBeforeSave + 1);
		QCOMPARE(song->tracks().size(), size_t(1));
		QCOMPARE(song->tracks().front()->name(), QString("original-project"));
		QCOMPARE(song->projectFileName(), blockedSave);
		QVERIFY(song->isModified());
		QCOMPARE(resources.entryList(QDir::Dirs | QDir::NoDotAndDotDot), resourceBaseline);
		QVERIFY(QFile::remove(blockedDirectory));
		QVERIFY(QDir().rename(blockedDirectory + ".original", blockedDirectory));
		choice = "corrupt";
		elapsed.restart();
		const auto corrupt = m_configuration.filePath("corrupt.json");
		QFile broken(corrupt);
		QVERIFY(broken.open(QIODevice::WriteOnly));
		broken.write("{broken}");
		broken.close();
		selectedPath = corrupt;
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QVERIFY(errorsShown > 0);
		QCOMPARE(song->tracks().size(), size_t(1));
		QCOMPARE(song->tracks().front()->name(), QString("original-project"));
		QVERIFY(song->isModified());
		selectedPath = source;
		choice = "replace";
		elapsed.restart();
		controller.importProject();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
		QCOMPARE(song->tracks().size(), size_t(3));
		QCOMPARE(song->tracks().front()->name(), QStringLiteral("完整导入"));
		QVERIFY(song->projectFileName().isEmpty());
		QVERIFY(song->isModified());
		auto* first = static_cast<SampleClip*>(song->tracks()[1]->getClip(0));
		auto* second = static_cast<SampleClip*>(song->tracks()[2]->getClip(0));
		QCOMPARE(int(first->length()), 96);
		QCOMPARE(int(second->length()), 120);
		QCOMPARE(int(second->startPosition()), 96);
		QCOMPARE(first->sampleFile(), second->sampleFile());
		QVERIFY(QFileInfo::exists(PathUtil::toAbsolute(first->sampleFile())));
		QVERIFY(first->sampleFile() != wave);
		QCOMPARE(first->sample().sampleSize(), f_cnt_t(48000));
		const auto native = m_configuration.filePath("durable-audio.mmp");
		QVERIFY(song->saveProjectFile(native));
		song->loadProject(native);
		QVERIFY(!song->hasErrors());
		first = static_cast<SampleClip*>(song->tracks()[1]->getClip(0));
		QVERIFY(QFileInfo::exists(PathUtil::toAbsolute(first->sampleFile())));
		QCOMPARE(first->sample().sampleSize(), f_cnt_t(48000));
		QFile unchanged(source);
		QVERIFY(unchanged.open(QIODevice::ReadOnly));
		QCOMPARE(unchanged.readAll(), original);
		automation.stop();
		song->clearProject();
		window->hide();
	}
	void projectExportNativeFlow()
	{
		if (!m_guiApplication)
		{
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert("SVS_EMBEDDED_GUI_TEST", "1");
			environment.insert("QT_QPA_PLATFORM", "windows");
			const auto report = m_configuration.filePath("project-export-child.txt");
			QProcess child;
			child.setProcessEnvironment(environment);
			child.start(QCoreApplication::applicationFilePath(),
				{"projectExportNativeFlow", "-o", report + ",txt", "-o", "-,txt"});
			QVERIFY(child.waitForStarted(5000));
			QVERIFY(child.waitForFinished(90000));
			QFile result(report);
			QVERIFY(result.open(QIODevice::ReadOnly));
			const auto output = result.readAll() + child.readAllStandardError();
			QFile archived("doc/svs/project/M3-export-native-child.txt");
			if (archived.open(QIODevice::WriteOnly))
				archived.write(output);
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
			return;
		}
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		auto* window = m_guiApplication->mainWindow();
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		auto* song = Engine::getSong();
		song->clearProject();
		SVSClip* first = nullptr;
		for (int index = 0; index < 2; ++index)
		{
			auto* track = new SVSTrack(song);
			track->setName(index ? QStringLiteral("日本語・한국어") : QStringLiteral("未渲染中文歌声"));
			track->bindVoice("missing-engine", "missing-voice");
			auto* clip = static_cast<SVSClip*>(track->createClip(index * 48));
			svs::Note note;
			note.id = QString::number(index);
			note.tick = 0;
			note.duration = 48;
			note.pitch = 60 + index;
			note.lyric = QStringLiteral("你好あ안녕");
			clip->setNotes({note});
			if (!index)
				first = clip;
			QVERIFY(!clip->audio());
		}
		song->setModified(true);
		const auto state = [&] {
			DataFile file(DataFile::Type::JournalData);
			song->saveProjectState(file);
			return file.toByteArray();
		};
		const auto original = state();
		const auto originalName = song->projectFileName();
		const auto output = m_configuration.filePath(QStringLiteral("导出-日本語-한국어.json"));
		const auto lyrics = m_configuration.filePath(QStringLiteral("歌词.lrc"));
		QFile old(output);
		QVERIFY(old.open(QIODevice::WriteOnly));
		old.write("original-output");
		old.close();
		gui::SVSProjectController controller(window, QStringLiteral(LMMS_SVS_PROJECT_RUNTIME));
		QString choice = "file-cancel";
		int errors = 0, losses = 0, overwrites = 0, completed = 0;
		bool timedOut = false;
		QElapsedTimer elapsed;
		elapsed.start();
		QTimer automation;
		connect(&automation, &QTimer::timeout, this, [&] {
			auto* modal = QApplication::activeModalWidget();
			if (!modal)
				return;
			if (elapsed.elapsed() > 20000)
			{
				timedOut = true;
				if (auto* dialog = qobject_cast<QDialog*>(modal))
					dialog->reject();
				return;
			}
			if (auto* picker = qobject_cast<QFileDialog*>(modal))
			{
				if (choice == "file-cancel")
				{
					picker->reject();
					return;
				}
				if (!picker->property("exportHandled").toBool())
				{
					picker->setProperty("exportHandled", true);
					QCOMPARE(picker->selectedNameFilter(), picker->nameFilters().first());
					QVERIFY(picker->selectedNameFilter().startsWith(QStringLiteral("所有支持格式 (")));
					QVERIFY(picker->selectedNameFilter().contains("*.json"));
					QVERIFY(picker->selectedNameFilter().contains("*.mxl"));
					QVERIFY(!picker->selectedNameFilter().contains("*.vshp"));
					picker->setProperty("filterCapturePending", true);
					QTimer::singleShot(600, picker, [picker] {
						picker->setProperty("filterCapturePending", false);
						QVERIFY(picker->screen()->grabWindow(picker->winId())
							.save("doc/svs/project/supported-formats-native-export.png"));
					});
					if (choice == "loss-cancel")
						for (const auto& filter : picker->nameFilters())
							if (filter.contains("[lrc]"))
							{
								picker->selectNameFilter(filter);
								break;
							}
					picker->setDirectory(QFileInfo(output).absolutePath());
				}
				const auto path = choice == "loss-cancel" ? lyrics : output;
				picker->selectFile(QFileInfo(path).fileName());
				if (auto* edit = picker->findChild<QLineEdit*>("fileNameEdit"))
					edit->setText(QFileInfo(path).fileName());
				if (!picker->property("filterCapturePending").toBool())
					QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
			}
			else if (modal->objectName() == "svsProjectExportDialog")
			{
				if (modal->property("exportHandled").toBool())
					return;
				modal->setProperty("exportHandled", true);
				auto* buttons = modal->findChild<QDialogButtonBox*>();
				QVERIFY(buttons);
				if (choice == "options-cancel")
					buttons->button(QDialogButtonBox::Cancel)->click();
				else
				{
					if (choice == "loss-cancel")
					{
						auto* singing = modal->findChild<QComboBox*>("svsProjectExportSingingTrack");
						QVERIFY(singing);
						QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
						singing->setCurrentIndex(2);
						QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
						QTest::qWait(700);
						QVERIFY(modal->screen()
								->grabWindow(modal->winId())
								.save("doc/svs/project/M3-native-export-options.png"));
					}
					buttons->button(QDialogButtonBox::Ok)->click();
				}
			}
			else if (auto* message = qobject_cast<QMessageBox*>(modal))
			{
				if (message->standardButtons().testFlag(QMessageBox::Yes))
				{
					const bool overwrite = message->windowTitle().contains(QStringLiteral("覆盖"));
					if (overwrite)
						++overwrites;
					else
						++losses;
					if (choice == "loss-cancel")
					{
						QVERIFY(message->text().contains(QStringLiteral("未渲染中文歌声")));
						QVERIFY(message->text().contains("lrc"));
					}
					message
						->button((choice == "loss-cancel" || (choice == "overwrite-cancel" && overwrite))
								? QMessageBox::Cancel
								: QMessageBox::Yes)
						->click();
				}
				else
				{
					if (message->windowTitle().contains(QStringLiteral("导出完成")))
						++completed;
					else
						++errors;
					if (auto* ok = message->button(QMessageBox::Ok))
						ok->click();
					else
						message->reject();
				}
			}
			else if (choice == "process-cancel")
			{
				if (auto* progress = qobject_cast<QProgressDialog*>(modal))
					if (progress->labelText() == QStringLiteral("正在转换工程…"))
						QMetaObject::invokeMethod(progress, "canceled", Qt::QueuedConnection);
			}
		});
		automation.start(50);
		const auto unchanged = [&] {
			QCOMPARE(state(), original);
			QCOMPARE(song->projectFileName(), originalName);
			QVERIFY(song->isModified());
			QFile file(output);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QCOMPARE(file.readAll(), QByteArray("original-output"));
		};
		for (const auto& action :
			QStringList{"file-cancel", "options-cancel", "loss-cancel", "overwrite-cancel", "process-cancel"})
		{
			choice = action;
			elapsed.restart();
			controller.exportProject();
			QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 25000);
			QVERIFY(!timedOut);
			unchanged();
			QVERIFY(!QFileInfo::exists(lyrics));
		}
		QVERIFY(losses > 0);
		QVERIFY(overwrites > 0);
		QCOMPARE(errors, 0);
		QCOMPARE(completed, 0);
		choice = "success";
		elapsed.restart();
		controller.exportProject();
		auto notes = first->notes();
		notes[0].lyric = QStringLiteral("导出之后修改");
		first->setEditorData(notes, first->curves());
		const auto edited = state();
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 25000);
		QVERIFY(!timedOut);
		QCOMPARE(completed, 1);
		QCOMPARE(errors, 0);
		QCOMPARE(state(), edited);
		QCOMPARE(song->projectFileName(), originalName);
		QVERIFY(song->isModified());
		QVERIFY(!first->audio());
		automation.stop();
		svs::ProjectBridge bridge(nullptr, QStringLiteral(LMMS_SVS_PROJECT_RUNTIME));
		QSignalSpy converted(&bridge, &svs::ProjectBridge::finished);
		QVERIFY(bridge.start({{"operation", "importProject"}, {"formatId", "json"}, {"path", output}}));
		QTRY_COMPARE_WITH_TIMEOUT(converted.size(), 1, 30000);
		const auto response = converted[0][0].toJsonObject();
		QVERIFY2(response["status"].toString() == "success", qPrintable(QJsonDocument(response).toJson()));
		const auto project = response["project"].toObject();
		QCOMPARE(project["track_list"].toArray().size(), 2);
		QCOMPARE(project["track_list"].toArray()[0].toObject()["note_list"].toArray()[0].toObject()["lyric"].toString(),
			QStringLiteral("你好あ안녕"));
		bridge.releaseTask();
		Engine::projectJournal()->undo();
		QCOMPARE(first->notes()[0].lyric, QStringLiteral("你好あ안녕"));
		song->clearProject();
		window->hide();
	}
	void embeddedWindowLifecycle()
	{
		if (!m_guiApplication)
		{
			auto environment = QProcessEnvironment::systemEnvironment();
			environment.insert("SVS_EMBEDDED_GUI_TEST", "1");
			const auto report = m_configuration.filePath("embedded-window-child.txt");
			QProcess process;
			process.setProcessEnvironment(environment);
			process.start(QCoreApplication::applicationFilePath(),
				{"embeddedWindowLifecycle", "-o", report + ",txt", "-o", "-,txt"});
			QVERIFY(process.waitForStarted(5000));
			QVERIFY(process.waitForFinished(30000));
			QFile result(report);
			QVERIFY(result.open(QIODevice::ReadOnly));
			const auto output = result.readAll() + process.readAllStandardOutput() + process.readAllStandardError();
			QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, output.constData());
			QVERIFY(output.contains("3 passed, 0 failed"));
			return;
		}
		const auto previous = ConfigManager::inst()->value("ui", "detachbehavior", "show");
		auto restore = qScopeGuard([&] { ConfigManager::inst()->setValue("ui", "detachbehavior", previous); });
		ConfigManager::inst()->setValue("ui", "detachbehavior", "show");
		auto* mainWindow = m_guiApplication->mainWindow();
		mainWindow->resize(1400, 1000);
		mainWindow->show();
		auto* addSVS = mainWindow->findChild<QAction*>("svsAddTrackAction");
		QVERIFY(addSVS);
		QCOMPARE(addSVS->icon().pixmap(24, 24).toImage(), QIcon("resources:svs_track.svg").pixmap(24, 24).toImage());
		const auto existingWindows = mainWindow->workspace()->subWindowList().size();
		if (qEnvironmentVariableIsSet("SVS_SETTINGS_TEST"))
		{
			auto* config = ConfigManager::inst();
			const auto backend = config->value("svs", "computeBackend");
			const auto device = config->value("svs", "computeDevice");
			const auto voice = svs::Registry::instance().voices().first();
			const auto key = "engine_" + QString::fromLatin1(voice.pluginId.toUtf8().toHex());
			const auto original = config->value("svsEngineSettings", key);
			const auto oldSteps = config->value("svs", "aiExampleRenderSteps");
			config->setValue("svs", "aiExampleRenderSteps", "20");
			auto restoreSettings = qScopeGuard([&] {
				config->setValue("svs", "computeBackend", backend);
				config->setValue("svs", "computeDevice", device);
				config->setValue("svsEngineSettings", key, original);
				config->setValue("svs", "aiExampleRenderSteps", oldSteps);
			});
			config->setValue("svs", "computeBackend", "cpu");
			config->setValue("svs", "computeDevice", "cpu");
			gui::SetupDialog settings(gui::SetupDialog::ConfigTab::SvsSettings);
			auto* page = static_cast<gui::SVSSettingsPage*>(settings.findChild<QWidget*>("svsSettingsPage"));
			QVERIFY(page);
			auto* backendBox = page->findChild<QComboBox*>("svsComputeBackend");
			auto* deviceBox = page->findChild<QComboBox*>("svsComputeDevice");
			QVERIFY(backendBox);
			QVERIFY(deviceBox);
			QCOMPARE(backendBox->count(), 4);
			QCOMPARE(backendBox->currentText(), QString("CPU"));
			QCOMPARE(deviceBox->itemText(0), QString("CPU"));
			QVERIFY(!deviceBox->isEnabled());
			for (int index = 1; index < 4; ++index)
			{
				backendBox->setCurrentIndex(index);
				QVERIFY(deviceBox->isEnabled());
				QVERIFY(backendBox->currentText().contains("Coming soon"));
				backendBox->setCurrentIndex(0);
				QVERIFY(!deviceBox->isEnabled());
				QCOMPARE(deviceBox->currentText(), QString("CPU"));
			}
			QJsonArray deviceList;
			for (int index = 0; index < deviceBox->count(); ++index)
				deviceList.append(
					QJsonObject{{"name", deviceBox->itemText(index)}, {"id", deviceBox->itemData(index).toString()}});
			QFile devicesFile("doc/svs/validation/SVS-settings-devices.json");
			QVERIFY(devicesFile.open(QIODevice::WriteOnly));
			devicesFile.write(QJsonDocument(deviceList).toJson());
			devicesFile.close();
			auto* engineTabs = page->findChild<QTabWidget*>("svsEngineTabs");
			QVERIFY(engineTabs);
			QVERIFY(engineTabs->count() > 0);
			auto* examplePage = page->findChild<QWidget*>("svsEnginePage." + voice.pluginId);
			QVERIFY(examplePage);
			engineTabs->setCurrentWidget(examplePage);
			QCOMPARE(engineTabs->tabText(engineTabs->currentIndex()), gui::SVSSettingsPage::engineLabel(voice));
			QVERIFY(page->findChild<QLabel*>("svsComputeHint")->text().contains("only to AI voicebanks"));
			auto ai = voice;
			ai.metadata["engineType"] = "ai";
			QVERIFY(gui::SVSSettingsPage::engineLabel(ai).endsWith("(AI)"));
			ai.metadata["engineType"] = "concatenative";
			QVERIFY(gui::SVSSettingsPage::engineLabel(ai).endsWith("(Traditional concatenation)"));
			auto* steps = page->findChild<QSlider*>("svsAiExampleRenderSteps");
			QVERIFY(steps);
			QCOMPARE(steps->minimum(), 1);
			QCOMPARE(steps->maximum(), 100);
			QCOMPARE(steps->value(), 20);
			QTRY_VERIFY(page->findChild<QDoubleSpinBox*>("svsParameter.track.example.outputGain"));
			auto* outputGain = page->findChild<QDoubleSpinBox*>("svsParameter.track.example.outputGain");
			outputGain->setValue(.5);
			QTRY_COMPARE(page->findChild<QDoubleSpinBox*>("svsParameter.track.example.outputGain")->value(), .5);
			backendBox->setCurrentIndex(1);
			if (deviceBox->count() > 1)
				deviceBox->setCurrentIndex(1);
			const auto selectedDevice = deviceBox->currentData();
			page->save();
			QCOMPARE(config->value("svs", "computeBackend"), QString("directml"));
			{
				gui::SVSSettingsPage reopened;
				reopened.findChild<QTabWidget*>("svsEngineTabs")
					->setCurrentWidget(reopened.findChild<QWidget*>("svsEnginePage." + voice.pluginId));
				QCOMPARE(
					reopened.findChild<QComboBox*>("svsComputeBackend")->currentData().toString(), QString("directml"));
				QVERIFY(reopened.findChild<QComboBox*>("svsComputeDevice")->isEnabled());
				QCOMPARE(reopened.findChild<QComboBox*>("svsComputeDevice")->currentData(), selectedDevice);
				QTRY_VERIFY(reopened.findChild<QDoubleSpinBox*>("svsParameter.track.example.outputGain"));
				QCOMPARE(reopened.findChild<QDoubleSpinBox*>("svsParameter.track.example.outputGain")->value(), .5);
			}
			auto* vstPage = settings.findChild<QWidget*>("vstSettingsPage");
			auto* pathsPage = settings.findChild<QWidget*>("pathsSettingsPage");
			QVERIFY(vstPage);
			QVERIFY(pathsPage);
			QVERIFY(vstPage->findChild<QComboBox*>("vstEmbeddingMethod"));
			QVERIFY(vstPage->findChild<QTreeWidget*>("vstCatalogCategories"));
			QVERIFY(!pathsPage->findChild<QTreeWidget*>("vstCatalogCategories"));
			settings.show();
			QVERIFY(QTest::qWaitForWindowExposed(&settings));
			settings.raise();
			settings.activateWindow();
			QTest::qWait(300);
			const auto image = settings.screen()->grabWindow(settings.winId());
			QVERIFY(image.save(
				qEnvironmentVariable("SVS_SETTINGS_CAPTURE_PATH", "doc/svs/validation/SVS-settings-window.png")));
			engineTabs->setCurrentIndex(engineTabs->count() - 1);
			QCOMPARE(engineTabs->currentWidget()->objectName(), QString("svsEnginePage.aiExample"));
			QVERIFY(engineTabs->currentWidget()->findChildren<QDoubleSpinBox*>().isEmpty());
			QTest::qWait(200);
			QVERIFY(
				settings.screen()->grabWindow(settings.winId()).save("doc/svs/validation/SVS-settings-ai-example.png"));
			steps->setFocus();
			QTest::keyClick(steps, Qt::Key_Right);
			QCOMPARE(steps->value(), 21);
			QCOMPARE(page->findChild<QLabel*>("svsAiExampleRenderStepsValue")->text(), QString("21"));
			page->save();
			{
				gui::SVSSettingsPage reopened;
				QCOMPARE(reopened.findChild<QSlider*>("svsAiExampleRenderSteps")->value(), 21);
			}
			settings.reject();
			{
				gui::SetupDialog vstSettings(gui::SetupDialog::ConfigTab::VstSettings);
				vstSettings.show();
				QVERIFY(QTest::qWaitForWindowExposed(&vstSettings));
				vstSettings.raise();
				QTest::qWait(200);
				QVERIFY(vstSettings.screen()
						->grabWindow(vstSettings.winId())
						.save("doc/svs/validation/SVS-settings-vst-window.png"));
				vstSettings.reject();
			}
			auto* sampleTrack = new SVSTrack(Engine::getSong());
			sampleTrack->bindVoice(voice.pluginId, voice.id);
			QTRY_VERIFY(sampleTrack->capabilitiesReady());
			auto* sampleClip = static_cast<SVSClip*>(sampleTrack->createClip(0));
			svs::Note sampleNote;
			sampleNote.id = "engine-settings-note";
			sampleNote.duration = 48;
			sampleClip->setNotes({sampleNote});
			auto input = sampleClip->captureInput(48000);
			QCOMPARE(input.document["computeBackend"].toString(), QString("cpu"));
			QCOMPARE(input.document["engineSettings"].toObject()["example.outputGain"].toDouble(), .5);
			QString error;
			const auto plugin = svs::Registry::instance().plugin(voice.pluginId);
			auto quieter = plugin->render(input, error);
			QVERIFY2(quieter, qPrintable(error));
			input.document["engineSettings"] = QJsonObject{{"example.outputGain", 1.}};
			auto louder = plugin->render(input, error);
			QVERIFY2(louder, qPrintable(error));
			QCOMPARE(quieter->samples.size(), louder->samples.size());
			bool audible = false;
			for (size_t i = 0; i < louder->samples.size(); ++i)
			{
				audible |= std::abs(louder->samples[i]) > .001f;
				QVERIFY(std::abs(quieter->samples[i] * 2 - louder->samples[i]) < 1e-6f);
			}
			QVERIFY(audible);
			delete sampleTrack;
		}
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(TimePos(0)));
		QTRY_VERIFY(!mainWindow->findChildren<gui::SVSTrackView*>().isEmpty());
		auto* trackView = mainWindow->findChild<gui::SVSTrackView*>();
		QVERIFY(trackView);
		QVERIFY(!trackView->findChild<QComboBox*>("svsVoiceSelector"));
		auto* pluginSettings = trackView->findChild<QDialog*>("svsPluginSettings");
		QVERIFY(pluginSettings);
		auto* avatarButton = trackView->findChild<QToolButton*>("svsTrackAvatar");
		QVERIFY(avatarButton);
		QTest::mouseClick(avatarButton, Qt::LeftButton);
		QVERIFY(pluginSettings->isVisible());
		pluginSettings->close();
		QTRY_VERIFY(!mainWindow->findChildren<gui::SVSClipView*>().isEmpty());
		auto* view = mainWindow->findChild<gui::SVSClipView*>();
		QTest::mouseDClick(view, Qt::LeftButton);
		QCoreApplication::processEvents();
		QPointer<gui::SVSPianoRoll> editor = mainWindow->workspace()->findChild<gui::SVSPianoRoll*>();
		QVERIFY(editor);
		auto* frame = qobject_cast<gui::SubWindow*>(editor->parentWidget());
		QVERIFY(frame);
		QPointer<gui::SubWindow> lifetime = frame;
		QCOMPARE(frame->mdiArea(), mainWindow->workspace());
		QVERIFY(!editor->isWindow());
		QVERIFY(!frame->isDetached());
		QVERIFY(frame->isVisible());
		QCOMPARE(mainWindow->workspace()->subWindowList().size(), existingWindows + 1);
		auto* canvas = editor->findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas);
		svs::Note note;
		note.id = "embedded-note";
		note.tick = 12;
		note.duration = 12;
		note.pitch = 60;
		clip->setNotes({note});
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->noteRect(note).center().toPoint());
		QVERIFY(canvas->selectedNotes().contains(note.id));
		frame->detach();
		QCoreApplication::processEvents();
		QVERIFY(frame->isDetached());
		QVERIFY(editor->isWindow());
		QVERIFY(editor->isVisible());
		QVERIFY2(editor->windowFlags().testFlag(Qt::WindowCloseButtonHint),
			"Detached SVS editor must expose an enabled native close button for reattachment");
#ifdef Q_OS_WIN
		const auto closeState
			= GetMenuState(GetSystemMenu(reinterpret_cast<HWND>(editor->winId()), FALSE), SC_CLOSE, MF_BYCOMMAND);
		QVERIFY(closeState != UINT(-1));
		QVERIFY(!(closeState & (MF_DISABLED | MF_GRAYED)));
#endif
		if (qEnvironmentVariableIsSet("SVS_PARAMETER_WINDOW_CAPTURE"))
		{
			QCOMPARE(QGuiApplication::platformName(), QString("windows"));
			const auto voice = svs::Registry::instance().voices().first();
			track->bindVoice(voice.pluginId, "full");
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			if (qEnvironmentVariableIsSet("SVS_PLUGIN_WINDOW_CAPTURE_PATH"))
			{
				QTest::mouseClick(avatarButton, Qt::LeftButton);
				QVERIFY(QTest::qWaitForWindowExposed(pluginSettings));
				pluginSettings->raise();
				pluginSettings->activateWindow();
				QTest::qWait(300);
				const auto capture = pluginSettings->screen()->grabWindow(pluginSettings->winId());
				QVERIFY(!capture.isNull());
				QVERIFY(capture.save(qEnvironmentVariable("SVS_PLUGIN_WINDOW_CAPTURE_PATH")));
				pluginSettings->close();
			}
			QVERIFY(track->setParameter("example.mode", "advanced"));
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			svs::Curve tension;
			tension.id = "example.tension";
			tension.type = "float";
			tension.interpolation = "linear";
			tension.evaluator.interpolation = svs_sdk::Interpolation::Linear;
			tension.insert(0, .15);
			tension.insert(160, .85);
			tension.insert(320, .4);
			svs::Curve gender;
			gender.id = "example.gender";
			gender.type = "float";
			gender.interpolation = "linear";
			gender.evaluator.interpolation = svs_sdk::Interpolation::Linear;
			gender.insert(0, -.75);
			gender.insert(160, .5);
			gender.insert(320, -.25);
			note.duration = 96;
			clip->setEditorData({note}, {{tension.id, tension}, {gender.id, gender}});
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			QVERIFY(clip->setGlobalParameter("example.tension", .35));
			auto* tab = editor->findChild<QToolButton*>("svsParameterTab.input:example.tension");
			QVERIFY(tab);
			QTest::mouseClick(tab, Qt::LeftButton);
			canvas->setScroll(0, 64);
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
			if (qEnvironmentVariableIsSet("SVS_SMOOTH_WINDOW_CAPTURE"))
			{
				note.tick = 0;
				note.duration = 320;
				clip->setNotes({note});
				canvas->setScroll(0, 66);
				canvas->setTool(gui::SVSCanvas::Tool::Freehand);
				const QVector<QPointF> pitchPoints{
					{0, 60}, {40, 64}, {80, 60}, {140, 64}, {180, 60}, {240, 63}, {300, 61}};
				QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, canvas->curvePointAt(0, 60).toPoint());
				for (const auto& p : pitchPoints)
					QTest::mouseMove(canvas, canvas->curvePointAt(p.x(), p.y()).toPoint());
				QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->curvePointAt(300, 61).toPoint());
				auto* lane = editor->findChild<gui::SVSCanvas*>("svsParameterLane.example.tension.input");
				QVERIFY(lane);
				lane->setTool(gui::SVSCanvas::Tool::Pencil);
				QTest::mousePress(lane, Qt::LeftButton, Qt::NoModifier, lane->curvePointAt(0, .2).toPoint());
				for (const auto& p : QVector<QPointF>{{50, .85}, {110, .2}, {170, .8}, {230, .3}, {300, .65}})
					QTest::mouseMove(lane, lane->curvePointAt(p.x(), p.y()).toPoint());
				QTest::mouseRelease(lane, Qt::LeftButton, Qt::NoModifier, lane->curvePointAt(300, .65).toPoint());
				canvas->setTool(gui::SVSCanvas::Tool::Freehand);
				QTRY_VERIFY2_WITH_TIMEOUT(clip->audio() != nullptr, qPrintable(clip->status()), 10000);
			}
			const auto available = editor->screen()->availableGeometry();
			editor->move(available.topLeft() + QPoint(20, 20));
			editor->resize(std::min(1100, available.width() - 40), std::min(740, available.height() - 80));
			editor->raise();
			editor->activateWindow();
			QVERIFY(QTest::qWaitForWindowExposed(editor));
			QTest::qWait(300);
			const auto capture = editor->screen()->grabWindow(editor->winId());
			QVERIFY(!capture.isNull());
			QVERIFY(capture.save(qEnvironmentVariable(
				"SVS_PARAMETER_WINDOW_CAPTURE_PATH", "doc/svs/validation/SVS-parameter-layout-native-window.png")));
			if (qEnvironmentVariableIsSet("SVS_WINDOW_CHROME_CAPTURE_PATH"))
			{
				const auto rect = editor->frameGeometry();
				const auto chrome = editor->screen()->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height());
				QVERIFY(!chrome.isNull());
				QVERIFY(chrome.save(qEnvironmentVariable("SVS_WINDOW_CHROME_CAPTURE_PATH")));
			}
		}
		QTest::mouseDClick(view, Qt::LeftButton);
		QCOMPARE(mainWindow->workspace()->subWindowList().size(), existingWindows + 1);
		QVERIFY(frame->isDetached());
#ifdef Q_OS_WIN
		SendMessageW(reinterpret_cast<HWND>(editor->winId()), WM_SYSCOMMAND, SC_CLOSE, 0);
#else
		editor->close();
#endif
		QCoreApplication::processEvents();
		QVERIFY(!frame->isDetached());
		QVERIFY(frame->isVisible());
		QVERIFY(!editor->isWindow());
		QCOMPARE(editor->findChild<gui::SVSCanvas*>("svsNoteCanvas"), canvas);
		QVERIFY(canvas->selectedNotes().contains(note.id));
		for (const auto& behavior : QStringList{"show", "hide", "detached"})
		{
			ConfigManager::inst()->setValue("ui", "detachbehavior", behavior);
			frame->detach();
			QVERIFY(frame->isDetached());
			editor->close();
			QCoreApplication::processEvents();
			QVERIFY(!frame->isDetached());
			QVERIFY(frame->isVisible());
			QVERIFY(editor->isVisible());
			QVERIFY(canvas->selectedNotes().contains(note.id));
		}
		ConfigManager::inst()->setValue("ui", "detachbehavior", "show");
		frame->close();
		QVERIFY(!frame->isVisible());
		QTest::mouseDClick(view, Qt::LeftButton);
		QVERIFY(frame->isVisible());
		frame->detach();
		delete track;
		QTRY_VERIFY(editor.isNull());
		QTRY_VERIFY(lifetime.isNull());
		QCOMPARE(mainWindow->workspace()->subWindowList().size(), existingWindows);
	}
	void sdkResourcesAndServices()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString mime, error;
		for (const auto& id : QStringList{"avatar.svg", "portrait.svg", "avatar-lite.svg", "portrait-lite.svg"})
		{
			const auto bytes = plugin->resource(id, mime, error);
			QVERIFY2(error.isEmpty(), qPrintable(error));
			QVERIFY(bytes.contains("<svg"));
			QCOMPARE(mime, QString("image/svg+xml"));
		}
		QVERIFY(plugin->resource("../avatar.svg", mime, error).isEmpty());
		QVERIFY(!error.isEmpty());
		QVERIFY(plugin->resource("undeclared", mime, error).isEmpty());
		QFile manifest(QDir(voice.package).filePath("manifest.json"));
		QVERIFY(manifest.open(QIODevice::ReadOnly));
		const auto entry = QJsonDocument::fromJson(manifest.readAll()).object()["entry"].toString();
		QLibrary library(QDir(voice.package).filePath(entry));
		auto get = reinterpret_cast<svs_get_api_fn>(library.resolve("svs_get_api"));
		QVERIFY(get);
		svs_api api{};
		QCOMPARE(get(SVS_ABI_MAJOR + 1, 0, sizeof(api), &api), svs_status(SVS_BAD_ABI));
		QCOMPARE(get(SVS_ABI_MAJOR, 0, SVS_API_REQUIRED_SIZE, &api), svs_status(SVS_OK));
		QCOMPARE(api.size, SVS_API_REQUIRED_SIZE);
		QVERIFY(!api.pronunciation && !api.open_resource);
		QCOMPARE(get(SVS_ABI_MAJOR, 0, uint32_t(offsetof(svs_api, open_resource)), &api), svs_status(SVS_OK));
		QVERIFY(api.pronunciation);
		QVERIFY(!api.open_resource);
		QCOMPARE(get(SVS_ABI_MAJOR, SVS_ABI_MINOR, sizeof(api), &api), svs_status(SVS_OK));
		QVERIFY(SVS_HAS_FIELD(api, svs_api, query_ranges) && api.query_ranges);
		struct Services
		{
			int allocations = 0, releases = 0, completions = 0, progress = 0;
			uint64_t request = 0;
			svs_status status = SVS_FAILED;
		} services;
		svs_host host{};
		host.size = sizeof(host);
		host.context = &services;
		host.allocate_buffer = [](void* context, uint32_t, uint64_t count, svs_buffer* out) -> svs_status {
			if (count > 128u * 1024 * 1024 || !out || out->size < sizeof(*out))
				return SVS_INVALID_INPUT;
			auto* pointer = std::malloc(size_t(std::max(uint64_t(1), count)));
			if (!pointer)
				return SVS_FAILED;
			++static_cast<Services*>(context)->allocations;
			*out = {sizeof(svs_buffer), pointer, count, context};
			return SVS_OK;
		};
		host.release_buffer = [](void* context, svs_buffer* buffer) {
			if (!buffer || !buffer->data || buffer->owner != context)
				return;
			++static_cast<Services*>(context)->releases;
			std::free(buffer->data);
			*buffer = {sizeof(svs_buffer)};
		};
		host.progress
			= [](void* context, uint64_t, double, const char*) { ++static_cast<Services*>(context)->progress; };
		host.completed = [](void* context, uint64_t request, svs_status status, const char*) {
			auto* state = static_cast<Services*>(context);
			++state->completions;
			state->request = request;
			state->status = status;
		};
		svs_engine engine = nullptr;
		QCOMPARE(api.create_engine(&host, &engine), svs_status(SVS_OK));
		auto destroyEngine = qScopeGuard([&] { api.destroy_engine(engine); });
		svs_session session = nullptr;
		QCOMPARE(api.create_session(engine, "full", &session), svs_status(SVS_OK));
		auto destroySession = qScopeGuard([&] { api.destroy_session(session); });
		const svs_note note{sizeof(svs_note), "sdk-note", 0, 48, 0, .5, 60, "la", "en", "", "{}", "{}"};
		const svs_snapshot snapshot{sizeof(svs_snapshot), "sdk-clip", 1, 2, 77, "full", 32000, &note, 1, .5,
			"{\"secondsPerTick\":0.010416666666666666,\"position\":0,\"curves\":{}}"};
		QCOMPARE(api.submit(session, &snapshot), svs_status(SVS_OK));
		const char* ranges = nullptr;
		QCOMPARE(api.query_ranges(session, &ranges), svs_status(SVS_OK));
		QVERIFY(ranges);
		const auto document = QJsonDocument::fromJson(ranges).object();
		api.release_string(engine, ranges);
		QCOMPARE(document["ranges"].toArray()[0].toObject()["endTick"].toDouble(), 48.);
		svs_result result{};
		result.size = sizeof(result);
		QCOMPARE(api.render(session, &result), svs_status(SVS_OK));
		auto release = qScopeGuard([&] { api.release_result(session, &result); });
		QVERIFY(result.audio && result.frame_count > 0);
		QCOMPARE(services.allocations, 1);
		QCOMPARE(services.releases, 0);
		QCOMPARE(services.completions, 1);
		QCOMPARE(services.request, uint64_t(77));
		QCOMPARE(services.status, svs_status(SVS_OK));
		QVERIFY(services.progress >= 2);
		api.release_result(session, &result);
		QCOMPARE(services.releases, 1);
		release.dismiss();
		svs_host prefix = host;
		prefix.size = uint32_t(offsetof(svs_host, allocate_buffer));
		svs_engine legacy = nullptr;
		QCOMPARE(api.create_engine(&prefix, &legacy), svs_status(SVS_OK));
		api.destroy_engine(legacy);
		QCOMPARE(api.submit(session, &snapshot), svs_status(SVS_OK));
		api.cancel(session);
		result.size = sizeof(result);
		QCOMPARE(api.render(session, &result), svs_status(SVS_CANCELLED));
		QVERIFY(result.error_json);
		const auto failure = QJsonDocument::fromJson(result.error_json).object();
		QCOMPARE(failure["clipId"].toString(), QString("sdk-clip"));
		QCOMPARE(failure["code"].toInt(), int(SVS_CANCELLED));
		api.release_result(session, &result);
		svs_sdk::Engine wrappedOwner(get, &host);
		QVERIFY(wrappedOwner.catalog().copy().find("minimal") != std::string::npos);
		QVERIFY(wrappedOwner.hasEngineSettings());
		const auto settingsJson = wrappedOwner.engineSettings().copy();
		const auto settingsObject = QJsonDocument::fromJson(QByteArray::fromStdString(settingsJson)).object();
		QCOMPARE(settingsObject["engineType"].toString(), QString("example"));
		QCOMPARE(settingsObject["engineSettings"].toArray().size(), 1);
		const char* invalidSettings = nullptr;
		QCOMPARE(api.query_engine_settings(engine, "[]", &invalidSettings), svs_status(SVS_INVALID_INPUT));
		QVERIFY(!invalidSettings);
		svs_api oldTable{};
		QCOMPARE(
			get(SVS_ABI_MAJOR, 1, uint32_t(offsetof(svs_api, query_engine_settings)), &oldTable), svs_status(SVS_OK));
		QVERIFY(!SVS_HAS_FIELD(oldTable, svs_api, query_engine_settings));
		QVERIFY(!oldTable.query_engine_settings);
		auto resource = [&] {
			svs_sdk::Engine owner(get, &host);
			return owner.resource("portrait-lite.svg");
		}();
		const auto resourceBytes = resource.read();
		QCOMPARE(uint64_t(resourceBytes.size()), resource.info().byte_count);
		QVERIFY(!resourceBytes.empty());
		auto wrappedSession = [&] {
			svs_sdk::Engine owner(get, &host);
			return owner.session("minimal");
		}();
		QCOMPARE(wrappedSession.submit(snapshot), svs_status(SVS_INVALID_INPUT));
		auto minimalSnapshot = snapshot;
		minimalSnapshot.voice_id = "minimal";
		QCOMPARE(wrappedSession.submit(minimalSnapshot), svs_status(SVS_OK));
		{
			auto wrappedResult = wrappedSession.render();
			QCOMPARE(wrappedResult.status(), svs_status(SVS_OK));
			QVERIFY(wrappedResult.value().audio);
			QCOMPARE(wrappedSession.submit(minimalSnapshot), svs_status(SVS_INVALID_INPUT));
		}
		QCOMPARE(wrappedSession.submit(minimalSnapshot), svs_status(SVS_OK));
		QVERIFY(wrappedSession.ranges().copy().find("endTick") != std::string::npos);
	}
	void releaseDemonstrationAndPerformance()
	{
		const auto output = qEnvironmentVariable("SVS_RELEASE_FIXTURE_DIR");
		if (output.isEmpty())
			return;
		QVERIFY(QDir().mkpath(output));
		QStandardPaths::setTestModeEnabled(true);
		QCoreApplication::setApplicationName("SVSRelease-" + QFileInfo(m_configuration.path()).fileName());
		QCOMPARE(svs::Cache::instance().diskBytes(), qint64(0));
		QCOMPARE(svs::Cache::instance().memoryBytes(), qint64(0));
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		QVector<SVSTrack*> tracks;
		QVector<SVSClip*> clips;
		auto cleanup = qScopeGuard([&] {
			for (auto* track : tracks)
				delete track;
			song->tempoModel().setValue(previousTempo);
		});
		const auto voice = svs::Registry::instance().voices().first();
		const QStringList languages{"zh", "ja", "en", "en"},
			lyrics{QString::fromUtf8("啦"), QString::fromUtf8("ら"), "la", "la"};
		for (int trackIndex = 0; trackIndex < 4; ++trackIndex)
		{
			auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
			tracks << track;
			track->bindVoice(voice.pluginId, trackIndex == 3 ? "minimal" : "full");
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			QVERIFY(track->setLanguage(languages[trackIndex]));
			track->setName(QString("SVS %1 (%2)").arg(trackIndex == 3 ? "Reduced" : "Full", languages[trackIndex]));
			track->volumeModel()->setValue(35);
			track->panningModel()->setValue((trackIndex - 1.5) * 20);
			track->setPortraitSettings(
				{{"visible", true}, {"transparency", QJsonArray{0, 50, 100, 50}[trackIndex]}, {"x", 1.}, {"y", 1.}});
			if (trackIndex < 3)
			{
				QVERIFY(track->setParameter("example.mode", "advanced"));
				QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			}
			for (int clipIndex = 0; clipIndex < 16; ++clipIndex)
			{
				auto* clip = static_cast<SVSClip*>(track->createClip(clipIndex * 384));
				clips << clip;
				QVector<svs::Note> notes;
				for (int index = 0; index < 64; ++index)
				{
					svs::Note note;
					note.id = QString("demo-%1-%2-%3").arg(trackIndex).arg(clipIndex).arg(index);
					note.tick = index * 6;
					note.duration = 4;
					note.pitch = 60 + (index % 4) * 2 + trackIndex * 3;
					note.language = languages[trackIndex];
					note.lyric = lyrics[trackIndex];
					if (trackIndex < 3)
						note.parameters = {{"example.power", 90 + index % 20}, {"example.soft", index % 8 == 0},
							{"example.label", QString("note %1").arg(index)}};
					notes << note;
				}
				svs::Curves curves;
				if (trackIndex < 3)
				{
					svs::Curve tension;
					tension.id = "example.tension";
					tension.scope = "clip";
					tension.interpolation = "hermite";
					tension.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
					for (int point = 0; point < 5; ++point)
						tension.evaluator.points.push_back(
							{point * 96., .25 + .2 * std::sin(point), 0, 0, true, false});
					curves[tension.id] = tension;
					svs::Curve mode;
					mode.id = "example.mode";
					mode.scope = "track";
					mode.type = "enum";
					mode.interpolation = "step";
					mode.evaluator.interpolation = svs_sdk::Interpolation::Step;
					mode.evaluator.points = {{0, 0, 0, 0, true, false, "basic"},
						{192, 0, 0, 0, true, false, "advanced"}, {384, 0, 0, 0, true, false, "basic"}};
					curves[mode.id] = mode;
					if (trackIndex == 0 && clipIndex == 0)
					{
						svs::Curve pitch;
						pitch.id = "svs.pitch";
						pitch.unit = "semitone";
						pitch.mode = "absolute";
						pitch.interpolation = "hermite";
						pitch.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
						for (int point = 0; point < 4096; ++point)
							pitch.evaluator.points.push_back(
								{384. * point / 4095, 60. + 2. * std::sin(point * .02), 0, 0, true, false});
						curves[pitch.id] = pitch;
					}
				}
				clip->setEditorData(notes, curves);
				clip->setAutoResize(false);
				clip->changeLength(384);
			}
		}
		QCOMPARE(clips.size(), 64);
		QCOMPARE(clips.first()->curves()["svs.pitch"].evaluator.points.size(), size_t(4096));
		// Distribute user inputs only; no local derived-cache reference is required.
		DataFile project(DataFile::Type::SongProject);
		project.head().setAttribute("bpm", 120);
		project.head().setAttribute("mastervol", 100);
		song->saveState(project, project.content());
		const auto savedClips = project.elementsByTagName("svsclip");
		for (int index = 0; index < savedClips.size(); ++index)
		{
			auto node = savedClips.at(index).toElement();
			node.removeAttribute("cacheKey");
			node.removeAttribute("cacheInputHash");
			node.removeAttribute("cacheSampleRate");
		}
		QVERIFY(project.writeFile(QDir(output).filePath("SVSExample-demo.mmp"), false));
		gui::SVSCanvas canvas(clips.first());
		canvas.resize(1000, 520);
		canvas.setZoom(2, 1);
		canvas.setQuantization(6);
		canvas.show();
		QCoreApplication::processEvents();
		const auto body = canvas.noteRect(clips.first()->notes()[2]).center().toPoint();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QVector<double> latency;
		double longestBlock = 0;
		int concurrentSamples = 0;
		auto& scheduler = svs::SynthesisScheduler::instance();
		QElapsedTimer synthesis;
		synthesis.start();
		for (int sample = 0; sample < 120; ++sample)
		{
			QElapsedTimer elapsed;
			elapsed.start();
			const QPointF local = body + QPoint(24 + sample % 8, 0);
			QMouseEvent event(QEvent::MouseMove, local, QPointF(canvas.mapToGlobal(local.toPoint())), Qt::NoButton,
				Qt::LeftButton, Qt::NoModifier);
			QCoreApplication::sendEvent(&canvas, &event);
			canvas.repaint();
			QCoreApplication::processEvents();
			const double milliseconds = elapsed.nsecsElapsed() / 1e6;
			latency << milliseconds;
			longestBlock = std::max(longestBlock, milliseconds);
			if (scheduler.activeCount() > 0)
				++concurrentSamples;
			QVERIFY(scheduler.activeCount() <= scheduler.budget());
		}
		canvas.cancelOperation();
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QVERIFY(concurrentSamples > 0);
		std::sort(latency.begin(), latency.end());
		const auto p95 = latency[int(std::ceil(latency.size() * .95)) - 1];
		QVERIFY2(p95 < 50, qPrintable(QString("Preview p95 %1 ms").arg(p95)));
		QVERIFY2(longestBlock < 100, qPrintable(QString("GUI block %1 ms").arg(longestBlock)));
		QElapsedTimer wait;
		wait.start();
		while (std::any_of(clips.begin(), clips.end(), [](const auto* clip) { return !clip->audio(); })
			&& wait.elapsed() < 30000)
		{
			QElapsedTimer elapsed;
			elapsed.start();
			QCoreApplication::processEvents();
			longestBlock = std::max(longestBlock, elapsed.nsecsElapsed() / 1e6);
			QTest::qWait(1);
		}
		for (auto* clip : clips)
			QVERIFY2(clip->audio() != nullptr, qPrintable(clip->status()));
		QVERIFY(longestBlock < 100);
		QVERIFY(scheduler.peakActiveCount() <= scheduler.budget());
		QVERIFY(svs::Cache::instance().memoryBytes() <= 128u * 1024 * 1024);
		QVERIFY(svs::Cache::instance().diskBytes() <= 512u * 1024 * 1024);
		QJsonObject record{{"dataset", "4 tracks x 16 clips x 64 notes; one 4096-anchor curve"}, {"coldCache", true},
			{"previewSamples", latency.size()}, {"previewP95Ms", p95}, {"guiLongestBlockMs", longestBlock},
			{"samplesDuringSynthesis", concurrentSamples}, {"synthesisAndInteractionMs", synthesis.elapsed()},
			{"peakActive", scheduler.peakActiveCount()}, {"budget", scheduler.budget()},
			{"memoryCacheBytes", double(svs::Cache::instance().memoryBytes())},
			{"diskCacheBytes", double(svs::Cache::instance().diskBytes())},
			{"scope", "Deterministic example and native host only; not a real model benchmark"}};
		QFile report(QDir(output).filePath("performance.json"));
		QVERIFY(report.open(QIODevice::WriteOnly));
		QVERIFY(report.write(QJsonDocument(record).toJson()) > 0);
		qInfo().noquote() << QJsonDocument(record).toJson(QJsonDocument::Compact);
	}
	void releaseDemonstrationRuntime()
	{
		const auto source = qEnvironmentVariable("SVS_RELEASE_DEMO"),
				   output = qEnvironmentVariable("SVS_RELEASE_RUNTIME_DIR");
		if (source.isEmpty())
			return;
		QVERIFY(!output.isEmpty());
		QVERIFY(QDir().mkpath(output));
		QStandardPaths::setTestModeEnabled(true);
		QCoreApplication::setApplicationName("SVSDemo-" + QFileInfo(m_configuration.path()).fileName());
		auto* song = Engine::getSong();
		song->loadProject(source);
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			song->clearProject();
		});
		QCOMPARE(song->tracks().size(), size_t(4));
		QVector<SVSClip*> clips;
		for (int index = 0; index < 4; ++index)
		{
			auto* track = dynamic_cast<SVSTrack*>(song->tracks()[index]);
			QVERIFY(track);
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			QCOMPARE(track->numOfClips(), 16);
			QCOMPARE(track->portraitSettings()["transparency"].toInt(), (QJsonArray{0, 50, 100, 50}[index].toInt()));
			auto* clip = static_cast<SVSClip*>(track->getClip(0));
			QCOMPARE(clip->notes().size(), 64);
			if (index == 0)
				QCOMPARE(clip->curves()["svs.pitch"].evaluator.points.size(), size_t(4096));
			gui::SVSPianoRoll editor(clip);
			auto* portrait = dynamic_cast<gui::SVSImageLoader*>(editor.findChild<QObject*>("svsPortraitLoader"));
			QVERIFY(portrait);
			QTRY_VERIFY_WITH_TIMEOUT(!portrait->image().isNull() || !portrait->diagnostic().isEmpty(), 5000);
			QVERIFY2(!portrait->image().isNull(), qPrintable(portrait->diagnostic()));
			QVERIFY(gui::SVSImageLoader::cacheBytes() <= 32u * 1024 * 1024);
			for (auto* item : track->getClips())
				clips << static_cast<SVSClip*>(item);
		}
		auto* first = clips.first();
		gui::SVSCanvas canvas(first);
		canvas.resize(1000, 520);
		canvas.setZoom(2, 1);
		canvas.setQuantization(6);
		const auto original = first->notes()[2];
		const auto body = canvas.noteRect(original).center().toPoint();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(24, 0));
		QCOMPARE(first->notes()[2].tick, original.tick + 6);
		canvas.beginLyric(original.id);
		auto* lyric = canvas.findChild<QLineEdit*>("svsInlineLyric");
		QVERIFY(lyric);
		lyric->setText(QString::fromUtf8("你"));
		QTest::keyClick(lyric, Qt::Key_Return);
		QCOMPARE(first->notes()[2].lyric, QString::fromUtf8("你"));
		QElapsedTimer wait;
		wait.start();
		while (std::any_of(clips.begin(), clips.end(), [](const auto* clip) { return !clip->audio(); })
			&& wait.elapsed() < 30000)
			QTest::qWait(5);
		for (auto* clip : clips)
			QVERIFY2(clip->audio() != nullptr, qPrintable(clip->status()));
		song->getTimeline(Song::PlayMode::Song).setTicks(0);
		song->playSong();
		double playbackEnergy = 0;
		for (int period = 0; period < 30; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				playbackEnergy += frame[0] * frame[0] + frame[1] * frame[1];
		song->stop();
		QVERIFY(playbackEnergy > .01);
		DataFile edited(DataFile::Type::SongProject);
		edited.head().setAttribute("bpm", 120);
		edited.head().setAttribute("mastervol", 100);
		song->saveState(edited, edited.content());
		const auto editedPath = QDir(output).filePath("SVSExample-edited.mmp");
		QVERIFY(edited.writeFile(editedPath, false));
		const auto notes = first->notes();
		const auto curves = first->curves();
		const auto portrait = static_cast<SVSTrack*>(first->getTrack())->portraitSettings();
		song->loadProject(editedPath);
		first = static_cast<SVSClip*>(song->tracks()[0]->getClip(0));
		QCOMPARE(first->notes(), notes);
		QCOMPARE(first->curves(), curves);
		QCOMPARE(static_cast<SVSTrack*>(first->getTrack())->portraitSettings(), portrait);
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		const auto wavePath = QDir(output).filePath("SVSExample-demo.wav");
		song->setExportLoop(false);
		song->setRenderBetweenMarkers(false);
		Engine::audioEngine()->storeAudioDevice();
		{
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, wavePath);
			QSignalSpy finished(&renderer, &ProjectRenderer::finished);
			renderer.startProcessing();
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
			QVERIFY2(renderer.renderSucceeded(), qPrintable(renderer.renderError()));
			renderer.wait();
		}
		const auto samples = wavePCM(wavePath);
		QVERIFY(samples.size() >= 32000 * 2 * 64);
		double exportEnergy = 0;
		for (float value : samples)
		{
			QVERIFY(std::isfinite(value));
			exportEnergy += value * value;
		}
		QVERIFY(exportEnergy > 100);
		qInfo() << "Distributed demo: edit/portrait/synthesis/playback/save/reopen/export PASS; samples="
				<< samples.size() << "energy=" << exportEnergy;
	}
	void sdkManifestDiagnostics()
	{
		const auto source = svs::Registry::instance().voices().first();
		QFile manifest(QDir(source.package).filePath("manifest.json"));
		QVERIFY(manifest.open(QIODevice::ReadOnly));
		const auto base = QJsonDocument::fromJson(manifest.readAll()).object();
		const auto entry = base["entry"].toString();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		auto fixture = [&](const QString& name, QJsonObject descriptor) {
			const auto folder = directory.filePath(name);
			if (!QDir().mkpath(folder)
				|| !QFile::copy(QDir(source.package).filePath(entry), QDir(folder).filePath(entry)))
				return false;
			QFile file(QDir(folder).filePath("manifest.json"));
			return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(descriptor).toJson()) > 0;
		};
		auto valid = base;
		valid["id"] = "org.lmms.svs.test.duplicate";
		QVERIFY(fixture("a-valid", valid));
		QVERIFY(fixture("b-duplicate", valid));
		auto incompatible = base;
		incompatible["architecture"] = "unsupported-test-architecture";
		QVERIFY(fixture("c-architecture", incompatible));
		incompatible = base;
		incompatible["platform"] = "unsupported-test-platform";
		QVERIFY(fixture("d-platform", incompatible));
		incompatible = base;
		incompatible["apiMajor"] = 999;
		QVERIFY(fixture("e-abi", incompatible));
		const bool wasSet = qEnvironmentVariableIsSet("LMMS_SVS_PLUGIN_DIR");
		const auto previous = qgetenv("LMMS_SVS_PLUGIN_DIR");
		qputenv("LMMS_SVS_PLUGIN_DIR", directory.path().toUtf8());
		auto restore = qScopeGuard([&] {
			if (wasSet)
				qputenv("LMMS_SVS_PLUGIN_DIR", previous);
			else
				qunsetenv("LMMS_SVS_PLUGIN_DIR");
		});
		svs::Registry isolated;
		isolated.voices();
		QTRY_VERIFY_WITH_TIMEOUT(!isolated.scanning(), 30000);
		const auto voices = isolated.voices();
		QCOMPARE(std::count_if(voices.begin(), voices.end(),
					 [](const auto& voice) { return voice.pluginId == "org.lmms.svs.test.duplicate"; }),
			2);
		const auto diagnostics = isolated.diagnostics().join('\n');
		QVERIFY2(diagnostics.contains("Duplicate SVS plugin ID"), qPrintable(diagnostics));
		QVERIFY(diagnostics.contains("SVS architecture mismatch"));
		QVERIFY(diagnostics.contains("SVS platform mismatch"));
		QVERIFY(diagnostics.contains("Invalid SVS manifest"));
	}
	void sdkMinimalNativeTiming()
	{
		const auto library = qEnvironmentVariable("SVS_MINIMAL_TEST_LIBRARY");
		if (library.isEmpty())
			return;
		svs::Plugin plugin(library);
		QVERIFY2(plugin.valid(), qPrintable(plugin.error()));
		QString error;
		svs::Capabilities capabilities;
		const auto schema = plugin.capabilities("minimal", {}, error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(svs::Capabilities::parse(schema, capabilities, error));
		QCOMPARE(capabilities.pitchInput, QString("none"));
		svs::Input input;
		input.clipId = "minimal-native";
		input.voiceId = "minimal";
		input.generation = input.revision = input.request = 1;
		input.rate = 32000;
		input.secondsPerTick = 1. / 96;
		input.duration = .5;
		svs::Note note;
		note.id = "minimal-note";
		note.tick = 24;
		note.duration = 48;
		input.notes = {note};
		input.document = {{"position", 192.}, {"contentOffset", 24.}, {"secondsPerTick", 1. / 96},
			{"capabilities", schema}, {"curves", QJsonObject{}},
			{"tempoMap",
				QJsonArray{QJsonObject{{"tick", 0.}, {"secondsPerTick", 1. / 96}},
					QJsonObject{{"tick", 192.}, {"secondsPerTick", 1. / 192}}}}};
		const auto audio = plugin.render(input, error);
		QVERIFY2(audio != nullptr, qPrintable(error));
		QCOMPARE(audio->rate, uint32_t(32000));
		QVERIFY(!audio->samples.empty());
		double energy = 0;
		for (float value : audio->samples)
			energy += value * value;
		QVERIFY(energy > 1);
		QVERIFY(std::abs(audio->startSeconds - 1.75) < 1e-8);
		QVERIFY(std::abs(audio->mapping.globalSeconds(note.tick) - 2.) < 1e-8);
		QVERIFY(std::abs(audio->mapping.localSeconds(72) - audio->mapping.localSeconds(24) - .25) < 1e-8);
		QString mime;
		QVERIFY(plugin.resource("avatar.svg", mime, error).isEmpty());
		QVERIFY(error.contains("unavailable"));
	}
	void missingPluginCachedPlayback()
	{
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			delete track;
			song->tempoModel().setValue(previousTempo);
		});
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		svs::Note note;
		note.id = "missing-cache";
		note.lyric = QUuid::createUuid().toString();
		note.duration = 96;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QVERIFY(!clip->audio()->cacheKey.isEmpty());
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QDomDocument document;
		auto root = document.createElement("fixture");
		document.appendChild(root);
		root.setAttribute("tempo", song->getTempo());
		auto binding = document.createElement("svstrack");
		root.appendChild(binding);
		track->saveTrackSpecificSettings(document, binding, false);
		auto node = document.createElement("svsclip");
		root.appendChild(node);
		clip->saveSettings(document, node);
		QVERIFY(node.attribute("cacheKey").size() == 64);
		QVERIFY(node.attribute("cacheInputHash").size() == 64);
		QFile file(directory.filePath("fixture.xml"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(document.toByteArray());
		file.close();
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert("LMMS_SVS_PLUGIN_DIR", directory.filePath("no-plugins"));
		environment.insert("LMMS_DATA_DIR", directory.path() + "/");
		environment.insert("SVS_MISSING_CACHE_FIXTURE", file.fileName());
		const auto report = directory.filePath("child-results.txt");
		QProcess process;
		process.setProcessEnvironment(environment);
		process.start(QCoreApplication::applicationFilePath(),
			{"cachedRestoreWithoutPlugin", "-o", report + ",txt", "-o", "-,txt"});
		QVERIFY(process.waitForStarted(5000));
		QVERIFY(process.waitForFinished(15000));
		QFile childResult(report);
		QVERIFY(childResult.open(QIODevice::ReadOnly));
		const auto output = childResult.readAll() + process.readAllStandardOutput() + process.readAllStandardError();
		QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, output.constData());
		QVERIFY(output.contains("3 passed, 0 failed"));
	}
	void cachedRestoreWithoutPlugin()
	{
		const auto fixture = qEnvironmentVariable("SVS_MISSING_CACHE_FIXTURE");
		if (fixture.isEmpty())
			return;
		QVERIFY(svs::Registry::instance().voices().isEmpty());
		QFile file(fixture);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(file.readAll()));
		const auto root = document.documentElement();
		auto* song = Engine::getSong();
		song->tempoModel().setValue(root.attribute("tempo").toInt());
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
		});
		track->loadTrackSpecificSettings(root.firstChildElement("svstrack"));
		QVERIFY(!svs::Registry::instance().plugin(track->pluginId()));
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		const auto saved = root.firstChildElement("svsclip");
		clip->loadSettings(saved);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QVERIFY(clip->status().contains("cached audio"));
		const auto audio = clip->audio();
		QVERIFY(audio->cacheKey == saved.attribute("cacheKey"));
		song->getTimeline(Song::PlayMode::Song).setTicks(193);
		song->playSong();
		double energy = 0;
		for (int period = 0; period < 10; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				energy += frame[0] * frame[0] + frame[1] * frame[1];
		song->stop();
		QVERIFY(energy > .01);
		auto* copy = static_cast<SVSClip*>(clip->clone());
		QTRY_VERIFY_WITH_TIMEOUT(copy->audio() != nullptr, 10000);
		QVERIFY(copy->notes()[0].id != clip->notes()[0].id);
		QVERIFY(copy->audio()->feedback["pronunciations"].toObject().contains(copy->notes()[0].id));
		delete copy;
		const auto path = QFileInfo(fixture).dir().filePath("cached.wav");
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderProject();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
			QCOMPARE(failed.count(), 0);
		}
		const auto samples = wavePCM(path);
		double exportedEnergy = 0;
		for (float sample : samples)
			exportedEnergy += sample * sample;
		QVERIFY(exportedEnergy > 1);
		auto notes = clip->notes();
		notes[0].lyric = "edited without engine";
		clip->setNotes(notes);
		QVERIFY(!clip->audio());
		QVERIFY(clip->status().contains("no valid cached audio"));
		clip->loadSettings(saved);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		clip->movePosition(960);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status().contains("no valid cached audio"), 10000);
		QVERIFY(!clip->audio());
		auto missing = saved.cloneNode(true).toElement();
		missing.setAttribute("cacheKey", QString(64, '0'));
		clip->loadSettings(missing);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status().contains("no valid cached audio"), 10000);
		QVERIFY(!clip->audio());
	}
	void nativeSvsMuteAndSolo()
	{
		if (!m_guiApplication)
			QSKIP("Native GUI required");
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		auto* song = Engine::getSong();
		auto* left = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto* right = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stop();
			delete right;
			delete left;
		});
		auto* window = static_cast<QWidget*>(m_guiApplication->mainWindow());
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		QCoreApplication::processEvents();
		gui::SVSTrackView* leftView = nullptr;
		gui::SVSTrackView* rightView = nullptr;
		for (auto* view : window->findChildren<gui::SVSTrackView*>())
		{
			if (view->getTrack() == left)
				leftView = view;
			if (view->getTrack() == right)
				rightView = view;
		}
		QVERIFY(leftView && rightView);
		auto* mute = leftView->findChild<QAbstractButton*>("btn-mute");
		auto* solo = rightView->findChild<QAbstractButton*>("btn-solo");
		QVERIFY(mute && solo);
		const auto voice = svs::Registry::instance().voices().first();
		left->bindVoice(voice.pluginId, "full");
		right->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(left->capabilitiesReady() && right->capabilitiesReady(), 10000);
		left->panningModel()->setValue(-100);
		right->panningModel()->setValue(100);
		auto* a = static_cast<SVSClip*>(left->createClip(0));
		auto* b = static_cast<SVSClip*>(right->createClip(0));
		svs::Note note;
		note.id = "mute-playback";
		note.duration = 192;
		note.pitch = 60;
		a->setNotes({note});
		note.pitch = 67;
		b->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(a->audio() && b->audio(), 10000);
		auto energy = [&] {
			song->stop();
			song->getTimeline(Song::PlayMode::Song).setTicks(0);
			song->playSong();
			QPair<double, double> result{0, 0};
			for (int period = 0; period < 100; ++period)
			{
				for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				{
					if (period >= 16)
					{
						result.first += frame[0] * frame[0];
						result.second += frame[1] * frame[1];
					}
				}
			}
			song->stop();
			return result;
		};
		const auto audible = energy();
		QVERIFY(audible.first > 0.01 && audible.second > 0.01);
		QTest::mouseClick(mute, Qt::LeftButton);
		QVERIFY2(left->isMuted(), "Song Editor mute button must change the SVS track model");
		const auto muted = energy();
		QVERIFY(muted.first < 1e-10 && muted.second > 0.01);
		QTest::mouseClick(mute, Qt::LeftButton);
		QVERIFY(!left->isMuted());
		QTest::mouseClick(solo, Qt::LeftButton);
		QVERIFY(left->isMuted());
		QVERIFY(!right->isMuted());
		const auto soloed = energy();
		QVERIFY(soloed.first < 1e-10 && soloed.second > 0.01);
		QTest::qWait(400);
		QVERIFY(window->screen()->grabWindow(window->winId()).save("doc/svs/validation/SVS-mute-solo-native.png"));
		QTest::mouseClick(solo, Qt::LeftButton);
		QVERIFY(!left->isMuted() && !right->isMuted());
	}
	void mutedSvsDefersSynthesis()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		auto cleanup = qScopeGuard([&] { delete track; });
		const auto voice = svs::Registry::instance().voices().first();
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		track->getMutedModel()->setValue(true);
		const auto request = clip->captureInput(44100).request;
		svs::Note note;
		note.id = "muted-render";
		note.duration = 192;
		note.pitch = 60;
		clip->setNotes({note});
		clip->synthesize();
		QTest::qWait(200);
		QCOMPARE(clip->captureInput(44100).request, request);
		QCOMPARE(clip->status(), QString("Dirty"));
		QVERIFY(!clip->audio());
		track->getMutedModel()->setValue(false);
		QTRY_COMPARE_WITH_TIMEOUT(clip->status(), QString("Ready"), 10000);
		const auto cached = clip->audio();
		QVERIFY(cached);
		track->getMutedModel()->setValue(true);
		track->getMutedModel()->setValue(false);
		QTest::qWait(100);
		QCOMPARE(clip->audio(), cached);
		track->getMutedModel()->setValue(true);
		note.pitch = 67;
		const auto editedRequest = clip->captureInput(44100).request;
		clip->setNotes({note});
		QTest::qWait(200);
		QCOMPARE(clip->captureInput(44100).request, editedRequest);
		track->getMutedModel()->setValue(false);
		QTRY_COMPARE_WITH_TIMEOUT(clip->status(), QString("Ready"), 10000);
		QVERIFY(clip->audio() && clip->audio() != cached);
		clip->invalidate();
		clip->synthesize();
		track->getMutedModel()->setValue(true);
		const auto suspendedRequest = clip->captureInput(44100).request;
		QTRY_COMPARE_WITH_TIMEOUT(svs::SynthesisScheduler::instance().activeCount(), 0, 10000);
		QCOMPARE(clip->status(), QString("Dirty"));
		QCOMPARE(clip->captureInput(44100).request, suspendedRequest);
		track->getMutedModel()->setValue(false);
		QTRY_COMPARE_WITH_TIMEOUT(clip->status(), QString("Ready"), 10000);
		clip->cancelSynthesis();
		track->getMutedModel()->setValue(true);
		track->getMutedModel()->setValue(false);
		QTest::qWait(100);
		QCOMPARE(clip->status(), QString("Cancelled"));
	}
	void cachedPlaybackAndTrackExports()
	{
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		const auto previousBegin = timeline.loopBegin(), previousEnd = timeline.loopEnd();
		const bool previousLoop = timeline.loopEnabled(), previousExportLoop = song->exportLoop(),
				   previousMarkers = song->renderBetweenMarkers();
		song->tempoModel().setValue(120);
		timeline.setLoopEnabled(false);
		song->setExportLoop(false);
		song->setRenderBetweenMarkers(false);
		song->setLoopRenderCount(1);
		const auto voice = svs::Registry::instance().voices().first();
		auto* left = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto* right = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete right;
			delete left;
			song->tempoModel().setValue(previousTempo);
			timeline.setLoopPoints(previousBegin, previousEnd);
			timeline.setLoopEnabled(previousLoop);
			song->setExportLoop(previousExportLoop);
			song->setRenderBetweenMarkers(previousMarkers);
		});
		left->setName("Left");
		right->setName("Right");
		left->bindVoice(voice.pluginId, "full");
		right->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(left->capabilitiesReady() && right->capabilitiesReady(), 10000);
		left->panningModel()->setValue(-100);
		right->panningModel()->setValue(100);
		auto* a = static_cast<SVSClip*>(left->createClip(0));
		auto* b = static_cast<SVSClip*>(right->createClip(192));
		svs::Note note;
		note.id = "left-export";
		note.duration = 192;
		note.pitch = 60;
		a->setNotes({note});
		note.id = "right-export";
		note.pitch = 67;
		b->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(a->audio() && b->audio(), 10000);
		const auto first = a->audio(), second = b->audio();
		// Settle LMMS' existing per-model panning ramps for both tracks, so the
		// comparison starts from the same mixer state as the subsequent export.
		timeline.setTicks(0);
		song->playSong();
		for (int period = 0; timeline.ticks() < 384 && period < 2000; ++period)
			Engine::audioEngine()->renderNextPeriod();
		song->stop();
		timeline.setTicks(0);
		song->playSong();
		Engine::audioEngine()->renderNextPeriod();
		QVector<float> live;
		// Normal export includes the existing final bar for draining the mixer tail.
		for (int period = 0; timeline.ticks() < 576 && period < 2000; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
			{
				live.push_back(frame[0]);
				live.push_back(frame[1]);
			}
		song->stop();
		QVERIFY(!live.isEmpty());
		QCOMPARE(a->audio(), first);
		QCOMPARE(b->audio(), second);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const OutputSettings settings(Engine::audioEngine()->outputSampleRate(), 192,
			OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		const auto mixedPath = directory.filePath("mixed.wav");
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, mixedPath);
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderProject();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
			QCOMPARE(failed.count(), 0);
		}
		const auto mixed = wavePCM(mixedPath);
		QCOMPARE(mixed.size(), live.size());
		double error = 0, energy = 0, middleError = 0;
		qsizetype worst = 0;
		for (qsizetype i = 0; i < mixed.size(); ++i)
		{
			const auto difference = std::abs(double(mixed[i]) - live[i]);
			if (difference > error)
			{
				error = difference;
				worst = i;
			}
			if (i > 4096 && i < mixed.size() - 4096)
				middleError = std::max(middleError, difference);
			energy += mixed[i] * mixed[i];
		}
		QVERIFY(energy > 1);
		QVERIFY2(error < 1e-6,
			qPrintable(
				QString("Cached playback/export max sample error: %1 index=%2 live=%3 export=%4 interior=%5 rate=%6")
					.arg(error, 0, 'g', 12)
					.arg(worst)
					.arg(live[worst])
					.arg(mixed[worst])
					.arg(middleError)
					.arg(settings.getSampleRate())));
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, directory.path());
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderTracks();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
			QCOMPARE(failed.count(), 0);
			QVERIFY(!left->isMuted() && !right->isMuted());
		}
		const auto stemA = wavePCM(directory.filePath("1_Left.wav")),
				   stemB = wavePCM(directory.filePath("2_Right.wav"));
		QVERIFY(!stemA.isEmpty() && !stemB.isEmpty());
		QCOMPARE(stemB.size(), mixed.size());
		double stemError = 0;
		for (qsizetype i = 0; i < mixed.size(); ++i)
		{
			const double sum = (i < stemA.size() ? stemA[i] : 0) + (i < stemB.size() ? stemB[i] : 0);
			stemError = std::max(stemError, std::abs(sum - mixed[i]));
		}
		QVERIFY2(stemError < 1e-6, qPrintable(QString("Mixed/stem max sample error: %1").arg(stemError, 0, 'g', 12)));
		timeline.setLoopPoints(0, 192);
		timeline.setLoopEnabled(true);
		timeline.setTicks(0);
		song->playSong();
		int wraps = 0;
		auto previous = timeline.ticks();
		double loopEnergy = 0;
		for (int period = 0; period < 700; ++period)
		{
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				loopEnergy += frame[0] * frame[0] + frame[1] * frame[1];
			if (timeline.ticks() < previous)
				++wraps;
			previous = timeline.ticks();
		}
		QVERIFY(wraps >= 2);
		QVERIFY(loopEnergy > 1);
		QCOMPARE(a->audio(), first);
		QCOMPARE(b->audio(), second);
		timeline.setLoopEnabled(false);
		song->setPlayPos(240);
		double seekEnergy = 0;
		for (int period = 0; period < 10; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				seekEnergy += frame[1] * frame[1];
		QVERIFY(seekEnergy > .01);
		song->stop();
	}
	void frozenTrackExportBatch()
	{
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		const auto voice = svs::Registry::instance().voices().first();
		auto* left = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto* right = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete right;
			delete left;
			song->tempoModel().setValue(previousTempo);
		});
		left->setName("FrozenLeft");
		right->setName("FrozenRight");
		left->bindVoice(voice.pluginId, "full");
		right->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(left->capabilitiesReady() && right->capabilitiesReady(), 10000);
		auto* a = static_cast<SVSClip*>(left->createClip(0));
		auto* b = static_cast<SVSClip*>(right->createClip(192));
		svs::Note note;
		note.id = "batch-left";
		note.duration = 192;
		note.pitch = 60;
		a->setNotes({note});
		note.id = "batch-right";
		note.pitch = 67;
		b->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(a->audio() && b->audio(), 10000);
		QTemporaryDir baseline, frozen;
		QVERIFY(baseline.isValid() && frozen.isValid());
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, baseline.path());
			QSignalSpy done(&manager, &RenderManager::finished);
			manager.renderTracks();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
		}
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, frozen.path());
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderTracks();
			auto changed = a->notes();
			changed[0].pitch = 84;
			changed[0].lyric = "changed after batch capture";
			a->setNotes(changed);
			a->movePosition(960);
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
			QCOMPARE(failed.count(), 0);
			QVERIFY(!left->isMuted() && !right->isMuted());
		}
		for (const auto& name : QStringList{"1_FrozenLeft.wav", "2_FrozenRight.wav"})
		{
			const auto expected = wavePCM(baseline.filePath(name)), actual = wavePCM(frozen.filePath(name));
			QVERIFY(!expected.isEmpty());
			QCOMPARE(actual.size(), expected.size());
			double error = 0;
			for (qsizetype i = 0; i < actual.size(); ++i)
				error = std::max(error, std::abs(double(actual[i]) - expected[i]));
			QVERIFY2(error < 1e-6, qPrintable(QString("Frozen batch sample error: %1").arg(error, 0, 'g', 12)));
		}
		QCOMPARE(a->startPosition(), TimePos(960));
		QCOMPARE(a->notes()[0].pitch, 84.);
	}
	void runningExportContextGuard_data()
	{
		QTest::addColumn<QString>("change");
		QTest::newRow("mix") << QString("mix");
		QTest::newRow("tempo") << QString("tempo");
		QTest::newRow("delete-track") << QString("delete-track");
	}
	void runningExportContextGuard()
	{
		QFETCH(QString, change);
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
			song->tempoModel().setValue(previousTempo);
		});
		track->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setAutoResize(false);
		clip->changeLength(192 * 32);
		svs::Note note;
		note.id = "running-context";
		note.duration = 192 * 32;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		const auto path = directory.filePath("changed-running.wav");
		Engine::audioEngine()->storeAudioDevice();
		auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		QSignalSpy finished(&renderer, &ProjectRenderer::finished);
		QSignalSpy failed(&renderer, &ProjectRenderer::svsExportFailed);
		bool changed = false;
		connect(&renderer, &ProjectRenderer::progressChanged, &renderer, [&](int) {
			if (!changed && renderer.isRunning() && song->isExporting())
			{
				changed = true;
				if (change == "mix")
					track->volumeModel()->setValue(0);
				else if (change == "tempo")
					song->tempoModel().setValue(150);
				else
				{
					delete track;
					track = nullptr;
				}
			}
		});
		renderer.startProcessing();
		QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
		renderer.wait();
		QVERIFY(changed);
		QCOMPARE(failed.count(), 1);
		QVERIFY(!renderer.renderSucceeded());
		QVERIFY(renderer.renderError().contains("while rendering"));
		QVERIFY(!QFileInfo::exists(path));
		QVERIFY(!song->isExporting());
	}
	void variableTempoFileExport()
	{
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		const bool previousLoop = timeline.loopEnabled(), previousExportLoop = song->exportLoop(),
				   previousMarkers = song->renderBetweenMarkers();
		timeline.setLoopEnabled(false);
		song->setExportLoop(false);
		song->setRenderBetweenMarkers(false);
		song->setLoopRenderCount(1);
		song->tempoModel().setValue(120);
		auto* automation = Track::create(Track::Type::Automation, song);
		auto* tempo = static_cast<AutomationClip*>(automation->createClip(0));
		tempo->addObject(&song->tempoModel());
		tempo->setAutoResize(false);
		tempo->changeLength(384);
		tempo->putValue(0, 120, false);
		tempo->putValue(192, 240, false);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
			delete automation;
			song->tempoModel().setValue(previousTempo);
			timeline.setLoopEnabled(previousLoop);
			song->setExportLoop(previousExportLoop);
			song->setRenderBetweenMarkers(previousMarkers);
		});
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setAutoResize(false);
		clip->changeLength(384);
		svs::Note note;
		note.id = "tempo-file";
		note.duration = 384;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto audio = clip->audio();
		QVERIFY(std::abs(audio->mapping.localSeconds(384) - 3.) < 1e-9);
		QCOMPARE(Engine::framesPerTick(), Engine::framesPerTick(Engine::audioEngine()->outputSampleRate()));
		timeline.setTicks(0);
		song->playSong();
		Engine::audioEngine()->renderNextPeriod();
		QVector<float> live;
		for (int period = 0; timeline.ticks() < 576 && period < 3000; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
			{
				live.push_back(frame[0]);
				live.push_back(frame[1]);
			}
		song->stop();
		QCOMPARE(clip->audio(), audio);
		song->tempoModel().setValue(120);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("tempo.wav");
		const OutputSettings settings(Engine::audioEngine()->outputSampleRate(), 192,
			OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderProject();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
			QCOMPARE(failed.count(), 0);
		}
		const auto exported = wavePCM(path);
		QCOMPARE(exported.size(), live.size());
		double error = 0, energy = 0;
		for (qsizetype i = 0; i < live.size(); ++i)
		{
			error = std::max(error, std::abs(double(exported[i]) - live[i]));
			energy += exported[i] * exported[i];
		}
		QVERIFY(energy > 1);
		QVERIFY2(error < 1e-6,
			qPrintable(QString("Variable tempo cached playback/export sample error: %1").arg(error, 0, 'g', 12)));
	}
	void deletedTrackExportPreparation()
	{
		auto* song = Engine::getSong();
		const auto voice = svs::Registry::instance().voices().first();
		QPointer<SVSTrack> track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			if (track)
				delete track.data();
		});
		track->setName("Deleted export voice");
		track->bindVoice(voice.pluginId, "full");
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setName("Deleted phrase");
		svs::Note note;
		note.id = "deleted-export";
		clip->setNotes({note});
		const auto clipId = clip->id();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("deleted.wav");
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		Engine::audioEngine()->storeAudioDevice();
		auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		renderer.setIgnoreFailedSVSRegions(true);
		QSignalSpy finished(&renderer, &ProjectRenderer::finished);
		renderer.startProcessing();
		delete track.data();
		QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
		QVERIFY(!renderer.renderSucceeded());
		QVERIFY(renderer.renderError().contains(clipId));
		QVERIFY(renderer.renderError().contains("Deleted export voice"));
		QVERIFY(!QFileInfo::exists(path));
		QVERIFY(!song->isExporting());
		QTRY_COMPARE_WITH_TIMEOUT(svs::SynthesisScheduler::instance().activeCount(), 0, 10000);
		QTest::qWait(100);
		QCOMPARE(finished.count(), 1);
	}
	void exportContextMutationRejected()
	{
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto* automation = Track::create(Track::Type::Automation, song);
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
			delete automation;
			song->tempoModel().setValue(previousTempo);
		});
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "frozen-context";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto* tempo = static_cast<AutomationClip*>(automation->createClip(0));
		tempo->addObject(&song->tempoModel());
		tempo->setAutoResize(false);
		tempo->changeLength(192);
		tempo->putValue(0, 120, false);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		const auto previousVolume = track->volumeModel()->value();
		for (bool changeTempo : {false, true})
		{
			const auto path = directory.filePath(changeTempo ? "changed-tempo.wav" : "changed-mix.wav");
			Engine::audioEngine()->storeAudioDevice();
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			renderer.setIgnoreFailedSVSRegions(true);
			QSignalSpy finished(&renderer, &ProjectRenderer::finished);
			renderer.startProcessing();
			if (changeTempo)
				tempo->putValue(0, 240, false);
			else
				track->volumeModel()->setValue(0);
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
			QVERIFY(!renderer.renderSucceeded());
			QVERIFY(renderer.renderError().contains(clip->id()));
			QVERIFY(renderer.renderError().contains(changeTempo ? "Project tempo changed" : "SVS mix controls"));
			QVERIFY(!QFileInfo::exists(path));
			QVERIFY(!song->isExporting());
			track->volumeModel()->setValue(previousVolume);
		}
	}
	void nativeSVSFileExport()
	{
		auto* song = Engine::getSong();
		const auto tempo = song->getTempo();
		const bool loop = song->exportLoop(), markers = song->renderBetweenMarkers();
		song->tempoModel().setValue(120);
		song->setExportLoop(true);
		song->setRenderBetweenMarkers(false);
		song->setLoopRenderCount(1);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
			song->tempoModel().setValue(tempo);
			song->setExportLoop(loop);
			song->setRenderBetweenMarkers(markers);
		});
		track->bindVoice(voice.pluginId, "full");
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "file-export";
		note.duration = 96;
		note.lyric = "hello";
		clip->setNotes({note});
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const OutputSettings settings(
			32000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		auto readPCM = [](const QString& path) {
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
				return QVector<float>{};
			const auto bytes = file.readAll();
			QVector<float> samples;
			for (qint64 offset = 12; offset + 8 <= bytes.size();)
			{
				const auto size = qFromLittleEndian<quint32>(bytes.constData() + offset + 4);
				if (offset + 8 + size > bytes.size())
					break;
				if (bytes.mid(offset, 4) == "data")
				{
					for (qint64 at = offset + 8; at + 4 <= offset + 8 + size; at += 4)
						samples.push_back(std::bit_cast<float>(qFromLittleEndian<quint32>(bytes.constData() + at)));
					break;
				}
				offset += 8 + size + (size & 1);
			}
			return samples;
		};
		const auto path = directory.filePath("voice.wav");
		Engine::audioEngine()->storeAudioDevice();
		{
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QVERIFY(renderer.isReady());
			QSignalSpy finished(&renderer, &ProjectRenderer::finished);
			renderer.startProcessing();
			note.lyric = "world";
			clip->setNotes({note});
			clip->movePosition(960);
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
			QVERIFY2(renderer.renderSucceeded(), qPrintable(renderer.renderError()));
			renderer.wait();
		}
		const auto samples = readPCM(path);
		QVERIFY(samples.size() >= 32000 * 2 * 2);
		QVERIFY(samples.size() < 33000 * 2 * 2);
		double energy = 0;
		for (float sample : samples)
		{
			QVERIFY(std::isfinite(sample));
			energy += sample * sample;
		}
		QVERIFY(energy > 1);
		clip->movePosition(0);
		track->bindVoice("missing.plugin", "missing.voice");
		const auto failedPath = directory.filePath("failed.wav");
		Engine::audioEngine()->storeAudioDevice();
		{
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, failedPath);
			QSignalSpy finished(&renderer, &ProjectRenderer::finished);
			renderer.startProcessing();
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
			QVERIFY(!renderer.renderSucceeded());
			QVERIFY(renderer.renderError().contains(clip->id()));
			QVERIFY(!QFileInfo::exists(failedPath));
		}
		const auto ignoredPath = directory.filePath("silence.wav");
		Engine::audioEngine()->storeAudioDevice();
		{
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, ignoredPath);
			renderer.setIgnoreFailedSVSRegions(true);
			QSignalSpy finished(&renderer, &ProjectRenderer::finished);
			renderer.startProcessing();
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
			QVERIFY(renderer.renderSucceeded());
			renderer.wait();
		}
		const auto silence = readPCM(ignoredPath);
		QVERIFY(!silence.isEmpty());
		double silentEnergy = 0;
		for (float sample : silence)
			silentEnergy += sample * sample;
		QVERIFY(silentEnergy < 1e-9);
		const auto cancelledPath = directory.filePath("cancelled.wav");
		Engine::audioEngine()->storeAudioDevice();
		{
			auto restore = qScopeGuard([] { Engine::audioEngine()->restoreAudioDevice(); });
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, cancelledPath);
			renderer.startProcessing();
			renderer.abortProcessing();
			QVERIFY(!renderer.isRunning());
			QVERIFY(!song->isExporting());
			QVERIFY(!QFileInfo::exists(cancelledPath));
		}
	}
	void exportSnapshotPreparation()
	{
		auto* song = Engine::getSong();
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] { delete track; });
		track->setName("Export voice");
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		clip->setName("Frozen phrase");
		svs::Note note;
		note.id = "export-note";
		note.lyric = "hello";
		note.duration = 48;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto captured = svs::ExportSnapshot::capture(*song, 32000);
		QCOMPARE(captured.size(), 1);
		QCOMPARE(captured[0].input.rate, uint32_t(32000));
		QCOMPARE(captured[0].input.notes[0].lyric, QString("hello"));
		captured[0].input.document["developmentFaults"] = QJsonObject{{"delayMs", 100}};
		svs::ExportSnapshot snapshot(captured);
		bool complete = false;
		snapshot.completed = [&] { complete = true; };
		snapshot.prepare();
		note.lyric = "world";
		clip->setNotes({note});
		clip->movePosition(960);
		bool heartbeat = false;
		QTimer::singleShot(0, this, [&] { heartbeat = true; });
		QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 100);
		QTRY_VERIFY_WITH_TIMEOUT(complete, 10000);
		QCOMPARE(snapshot.state(), svs::ExportSnapshot::State::Ready);
		QVERIFY(snapshot.regions()[0].audio);
		QCOMPARE(snapshot.regions()[0].audio->rate, uint32_t(32000));
		QCOMPARE(snapshot.regions()[0].position, 192.);
		QCOMPARE(snapshot.regions()[0]
					 .audio->feedback["pronunciations"]
					 .toObject()["export-note"]
					 .toObject()["text"]
					 .toString(),
			QString("hello"));
		QCOMPARE(clip->notes()[0].lyric, QString("world"));
		auto failed = captured;
		failed[0].input.document["developmentFaults"] = QJsonObject{{"fail", true}};
		svs::ExportSnapshot failure(failed);
		failure.prepare();
		QTRY_COMPARE_WITH_TIMEOUT(failure.state(), svs::ExportSnapshot::State::Failed, 10000);
		QVERIFY(failure.diagnostics().join('\n').contains("Frozen phrase"));
		QVERIFY(failure.diagnostics().join('\n').contains(clip->id()));
		QVERIFY(failure.diagnostics().join('\n').contains("192"));
		svs::ExportSnapshot ignored(failed);
		ignored.prepare(true);
		QTRY_COMPARE_WITH_TIMEOUT(ignored.state(), svs::ExportSnapshot::State::Ready, 10000);
		QCOMPARE(ignored.diagnostics().size(), 1);
		QVERIFY(!ignored.regions()[0].audio);
		auto late = captured;
		late[0].input.document["developmentFaults"] = QJsonObject{{"delayMs", 150}, {"lateReturn", true}};
		svs::ExportSnapshot cancelled(late);
		cancelled.prepare();
		QTest::qWait(30);
		cancelled.cancel();
		QCOMPARE(cancelled.state(), svs::ExportSnapshot::State::Cancelled);
		QTRY_COMPARE_WITH_TIMEOUT(svs::SynthesisScheduler::instance().activeCount(), 0, 10000);
		QVERIFY(!cancelled.regions()[0].audio);
		track->setMuted(true);
		QVERIFY(svs::ExportSnapshot::capture(*song, 48000).isEmpty());
		track->setMuted(false);
		clip->setMuted(true);
		QVERIFY(svs::ExportSnapshot::capture(*song, 48000).isEmpty());
	}
	void tempoSnapshotPipelineAndPlayback()
	{
		auto* song = Engine::getSong();
		const auto originalTempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto* automation = Track::create(Track::Type::Automation, song);
		auto* tempo = static_cast<AutomationClip*>(automation->createClip(0));
		tempo->addObject(&song->tempoModel());
		tempo->setAutoResize(false);
		tempo->changeLength(384);
		tempo->putValue(0, 120, false);
		tempo->putValue(192, 240, false);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			song->stop();
			delete track;
			delete automation;
			song->tempoModel().setValue(originalTempo);
			QCoreApplication::processEvents();
		});
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setAutoResize(false);
		clip->changeLength(384);
		svs::Note note;
		note.id = "tempo-pipeline";
		note.duration = 384;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto first = clip->audio();
		QVERIFY(first->mapping.tempo);
		QVERIFY(std::abs(first->mapping.localSeconds(384) - 3.) < 1e-9);
		QCOMPARE(first->samples.size() / 2, size_t(std::ceil(3 * first->rate)));
		tempo->putValue(192, 60, false);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() && clip->audio() != first, 10000);
		auto changed = clip->audio();
		QVERIFY(std::abs(changed->mapping.localSeconds(384) - 6.) < 1e-9);
		QCOMPARE(changed->samples.size() / 2, size_t(std::ceil(6 * changed->rate)));
		QVERIFY(std::abs(first->mapping.localSeconds(384) - 3.) < 1e-9);
		const auto before = svs::TempoSource::forSong(*song).snapshot()->toJson();
		song->getTimeline(Song::PlayMode::Song).setTicks(193);
		song->playSong();
		double energy = 0;
		for (int period = 0; period < 10; ++period)
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
				energy += frame[0] * frame[0] + frame[1] * frame[1];
		QVERIFY(energy > .01);
		QCOMPARE(song->getTempo(), 60);
		QCoreApplication::processEvents();
		QVERIFY2(clip->audio() == changed,
			qPrintable(clip->status()
				+ " before=" + QString::fromUtf8(QJsonDocument(before).toJson(QJsonDocument::Compact)) + " after="
				+ QString::fromUtf8(QJsonDocument(svs::TempoSource::forSong(*song).snapshot()->toJson())
						.toJson(QJsonDocument::Compact))));
		song->stop();
		QCoreApplication::processEvents();
		QCOMPARE(clip->audio(), changed);
		tempo->setMuted(true);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() && clip->audio() != changed, 10000);
		QVERIFY(std::abs(clip->audio()->mapping.localSeconds(384) - 8.) < 1e-9);
	}
	void immutableTempoAutomationSnapshot()
	{
		auto* song = Engine::getSong();
		auto* first = Track::create(Track::Type::Automation, song);
		auto* a = static_cast<AutomationClip*>(first->createClip(192));
		a->addObject(&song->tempoModel());
		a->setAutoResize(false);
		a->changeLength(384);
		a->setStartTimeOffset(24);
		a->putValue(0, 120, false);
		a->putValue(96, 180, false);
		a->putValue(288, 80, false);
		a->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
		auto* second = Track::create(Track::Type::Automation, song);
		auto* b = static_cast<AutomationClip*>(second->createClip(384));
		b->addObject(&song->tempoModel());
		b->setAutoResize(false);
		b->changeLength(192);
		b->putValue(0, 160, false);
		b->putValue(96, 100, false);
		auto* patternTrack = static_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
		const auto index = patternTrack->patternIndex();
		auto* pattern = patternTrack->createClip(576);
		pattern->changeLength(384);
		auto* patternAutomation = Track::create(Track::Type::Automation, Engine::patternStore());
		patternAutomation->createClipsForPattern(index);
		auto* p = static_cast<AutomationClip*>(patternAutomation->getClip(index));
		p->addObject(&song->tempoModel());
		p->setAutoResize(false);
		p->changeLength(192);
		p->putValue(0, 200, false);
		p->putValue(96, 90, false);
		p->setProgressionType(AutomationClip::ProgressionType::Linear);
		auto snapshot = svs::TempoSnapshot::capture(*song, 120);
		const auto frozen = snapshot->toJson();
		QJsonArray points;
		QString error;
		QVERIFY(snapshot->buildMap(1152, points, error));
		svs::TimeMapping mapping;
		QVERIFY(svs::readTimeMapping({{"tempoMap", points}}, .01, mapping, error));
		for (int tick = 0; tick < 1152; ++tick)
		{
			const auto values = song->automatedValuesAt(tick);
			const auto expected = values.contains(&song->tempoModel())
				? int(std::clamp(values.value(&song->tempoModel()), float(song->tempoModel().minValue()),
					  float(song->tempoModel().maxValue())))
				: 120;
			QCOMPARE(snapshot->tempoAt(tick), expected);
			QVERIFY(std::abs(mapping.globalSeconds(tick + 1) - mapping.globalSeconds(tick) - 60. / (expected * 48))
				< 1e-10);
		}
		a->putValue(96, 220, false);
		b->setMuted(true);
		p->putValue(0, 240, false);
		QCOMPARE(snapshot->toJson(), frozen);
		auto changed = svs::TempoSnapshot::capture(*song, 120);
		QVERIFY(changed->toJson() != frozen);
		QCOMPARE(changed->tempoAt(576), 240);
		auto control = std::make_shared<svs::RenderControl>();
		control->cancel();
		QVERIFY(!changed->buildMap(1152, points, error, control));
		QCOMPARE(error, QString("Cancelled"));
		delete patternAutomation;
		delete patternTrack;
		delete second;
		delete first;
		QVERIFY(svs::TempoSnapshot::capture(*song, 120)->layers.empty());
	}
	void variableTempoMappingAndPlugin()
	{
		svs_sdk::TempoMap tempo;
		QVERIFY(tempo.setPoints({{0, .01}, {192, .02}, {384, .005}}));
		QVERIFY(!tempo.setPoints({{0, .01}, {0, .02}}));
		QVERIFY(!tempo.setPoints({{0, 0}}));
		QVERIFY(!tempo.setPoints({{1, .01}}));
		QCOMPARE(tempo.secondsAt(192), 1.92);
		QVERIFY(std::abs(tempo.secondsAt(384) - 5.76) < 1e-12);
		for (double tick = -12; tick < 600; tick += .125)
			QVERIFY(std::abs(tempo.tickAt(tempo.secondsAt(tick)) - tick) < 1e-10);
		svs::Input input;
		input.clipId = "variable-tempo";
		input.voiceId = "full";
		input.rate = 48000;
		input.revision = 1;
		input.secondsPerTick = .01;
		QJsonArray points{QJsonObject{{"tick", 0}, {"secondsPerTick", .01}},
			QJsonObject{{"tick", 192}, {"secondsPerTick", .02}}, QJsonObject{{"tick", 384}, {"secondsPerTick", .005}}};
		svs::Curve curve;
		curve.id = "svs.pitch";
		curve.unit = "semitone";
		curve.mode = "absolute";
		curve.insert(0, 60.);
		curve.insert(96, 64.);
		curve.insert(240, 68.);
		curve.insert(384, 70.);
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString error;
		auto capabilities = plugin->capabilities("full", {}, error);
		input.document = {{"position", 96}, {"contentOffset", 24}, {"tempoMap", points}, {"secondsPerTick", .01},
			{"capabilities", capabilities}, {"language", "en"}, {"curves", svs::curvesToJson({{curve.id, curve}})}};
		svs::TimeMapping mapping;
		QVERIFY(svs::readTimeMapping(input.document, .01, mapping, error));
		input.duration = mapping.localSeconds(384);
		svs::Note note;
		note.id = "variable-note";
		note.tick = 0;
		note.duration = 360;
		note.phonemes = {{"phonemeSet", "example.multilingual.v1"}, {"symbols", QJsonArray{"l", "a"}},
			{"segments",
				QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", -6}, {"durationTicks", 12}},
					QJsonObject{{"symbol", "a"}, {"startTick", 6}, {"durationTicks", 354}}}}};
		input.notes = {note};
		auto audio = plugin->render(input, error);
		QVERIFY2(audio != nullptr, qPrintable(error));
		QVERIFY(std::abs(audio->startSeconds - mapping.globalSeconds(-6)) < 1e-10);
		QVERIFY(std::abs(audio->startTick + 6) < 1e-10);
		QVERIFY(audio->mapping.tempo);
		for (const auto& value : audio->feedback["pitch"].toArray())
		{
			const auto sample = value.toObject();
			const auto tick = mapping.tickAtLocalSeconds(sample["startSeconds"].toDouble());
			QVERIFY(std::abs(sample["value"].toDouble() - curve.valueAt(tick)->toDouble()) < 1e-8);
		}
		const auto phonemes = audio->feedback["phonemes"].toArray();
		QCOMPARE(phonemes.size(), 2);
		QVERIFY(std::abs(phonemes[1].toObject()["durationSeconds"].toDouble()
					- (mapping.localSeconds(360) - mapping.localSeconds(6)))
			< 1e-10);
		for (double tick : {0., 119.5, 120., 120.5, 312., 359.})
		{
			const auto frame = size_t(mapping.samplePosition(tick, audio->startTick, audio->rate));
			QVERIFY(frame + 128 < audio->samples.size() / 2);
			double energy = 0;
			for (size_t f = frame; f < frame + 128; ++f)
				energy += audio->samples[f * 2] * audio->samples[f * 2];
			QVERIFY(energy > 1e-5);
		}
		QTemporaryDir directory;
		const auto key = svs::Cache::key(input, plugin->identity());
		{
			svs::Cache cache(directory.path());
			cache.put(key, input, audio);
		}
		svs::Cache reopened(directory.path());
		auto cached = reopened.get(key, input);
		QVERIFY(cached);
		QVERIFY(cached->mapping.tempo);
		QVERIFY(std::abs(cached->mapping.samplePosition(240, cached->startTick, 48000)
					- mapping.samplePosition(240, -6, 48000))
			< 1e-7);
		input.document["tempoMap"] = QJsonArray{QJsonObject{{"tick", 0}, {"secondsPerTick", 0}}};
		QVERIFY(!plugin->render(input, error));
		QVERIFY(error.contains("tempo"));
		QVERIFY(svs::Cache::key(input, plugin->identity()) != key);
	}
	void asynchronousVoiceDeclaration()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString error;
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		svs::Input input;
		input.clipId = "busy-declaration";
		input.voiceId = "full";
		input.rate = 48000;
		input.revision = 1;
		input.secondsPerTick = .01;
		input.duration = .5;
		svs::Note note;
		note.id = "busy-note";
		input.notes = {note};
		input.document = {{"capabilities", track->capabilities().original}, {"language", "en"}, {"secondsPerTick", .01},
			{"developmentFaults", QJsonObject{{"delayMs", 1000}, {"nonce", QUuid::createUuid().toString()}}}};
		svs::SynthesisScheduler scheduler(1);
		bool completed = false;
		scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { completed = true; });
		QTest::qWait(80);
		QElapsedTimer timer;
		timer.start();
		track->bindVoice(voice.pluginId, "minimal");
		QVERIFY2(timer.elapsed() < 100, "Voice binding blocked the GUI on the rendering engine");
		QVERIFY(!track->capabilitiesReady());
		track->bindVoice(voice.pluginId, "full");
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setNotes({note});
		clip->cancelSynthesis();
		bool heartbeat = false;
		QTimer::singleShot(0, this, [&] { heartbeat = true; });
		QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 100);
		QVERIFY(!completed);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 5000);
		QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
		QVERIFY(track->capabilities().parameter("example.breath", "clip"));
		QCOMPARE(clip->status(), QString("Cancelled"));
		QVERIFY(!clip->audio());
		track->bindVoice(voice.pluginId, "minimal");
		delete track;
		QTest::qWait(100);
	}
	void persistenceAndMigrationProtection()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		QDomDocument source;
		QVERIFY(source.setContent(
			R"(<svstrack schemaVersion="1" pluginId="missing.plugin" voiceId="missing.voice" pluginVersion="7" voiceVersion="8" language="ja-JP" nameMode="custom" vendor="keep" parameters='{"hidden":42}'><vendorState flag="1"/></svstrack>)"));
		track->loadTrackSpecificSettings(source.documentElement());
		QVERIFY(!track->readOnly());
		QCOMPARE(track->language(), QString("ja-JP"));
		QDomDocument saved;
		auto trackNode = saved.createElement("svstrack");
		track->saveTrackSpecificSettings(saved, trackNode, false);
		QCOMPARE(trackNode.attribute("voiceVersion"), QString("8"));
		QCOMPARE(trackNode.attribute("pluginVersion"), QString("7"));
		QCOMPARE(trackNode.attribute("vendor"), QString("keep"));
		QVERIFY(!trackNode.firstChildElement("vendorState").isNull());
		QCOMPARE(trackNode.attribute("language"), QString("ja-JP"));
		QCOMPARE(track->parameters()["hidden"].toInt(), 42);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		QDomDocument clipDoc;
		QVERIFY(clipDoc.setContent(
			R"(<svsclip schemaVersion="0" id="stable-clip" pos="192" len="192" off="0" autoresize="0" vendor="root" curves='{"future.curve":{"futureFormat":2}}' parameters='{"hidden":11}'><vendorState/><notes vendor="container"><vendorState/><note id="stable-note" tick="0.25" duration="180" pitch="60.5" lyric="你" vendor="note"><vendorState value="x"/></note></notes></svsclip>)"));
		clip->loadSettings(clipDoc.documentElement());
		QVERIFY(!clip->readOnly());
		QCOMPARE(clip->id(), QString("stable-clip"));
		QCOMPARE(clip->notes()[0].tick, .25);
		auto verifyExtras = [&](SVSClip* current) {
			QDomDocument doc;
			auto node = doc.createElement("svsclip");
			current->saveSettings(doc, node);
			QCOMPARE(node.attribute("schemaVersion"), QString("1"));
			QCOMPARE(node.attribute("vendor"), QString("root"));
			QVERIFY(!node.firstChildElement("vendorState").isNull());
			const auto notes = node.firstChildElement("notes");
			QCOMPARE(notes.attribute("vendor"), QString("container"));
			QVERIFY(!notes.firstChildElement("vendorState").isNull());
			const auto note = notes.firstChildElement("note");
			QCOMPARE(note.attribute("vendor"), QString("note"));
			QCOMPARE(note.firstChildElement("vendorState").attribute("value"), QString("x"));
			QVERIFY(QJsonDocument::fromJson(node.attribute("curves").toUtf8()).object().contains("future.curve"));
			QCOMPARE(current->parameters()["hidden"].toInt(), 11);
		};
		verifyExtras(clip);
		auto* copy = static_cast<SVSClip*>(clip->clone());
		QVERIFY(copy->id() != clip->id());
		QVERIFY(copy->notes()[0].id != clip->notes()[0].id);
		verifyExtras(copy);
		auto* right = copy->splitAt(288);
		QVERIFY(right);
		verifyExtras(right);
		QDomDocument roundtrip;
		auto node = roundtrip.createElement("svsclip");
		clip->saveSettings(roundtrip, node);
		clip->loadSettings(node);
		verifyExtras(clip);
		QCOMPARE(clip->id(), QString("stable-clip"));
		auto rejected = [&](const QString& attribute, const QString& value) {
			auto bad = clipDoc.documentElement().cloneNode(true).toElement();
			bad.setAttribute(attribute, value);
			QDomDocument before;
			before.appendChild(before.importNode(bad, true));
			clip->loadSettings(bad);
			QVERIFY(clip->readOnly());
			QVERIFY(!clip->migrationDiagnostic().isEmpty());
			const auto notes = clip->notes();
			clip->setNotes({});
			clip->movePosition(960);
			clip->synthesize();
			QCOMPARE(clip->notes(), notes);
			QVERIFY(!clip->audio());
			QDomDocument after;
			auto result = after.createElement("svsclip");
			after.appendChild(result);
			clip->saveSettings(after, result);
			QCOMPARE(after.toString(), before.toString());
			gui::SVSPianoRoll editor(clip);
			QVERIFY(!editor.findChild<gui::SVSCanvas*>()->isEnabled());
		};
		rejected("schemaVersion", "99");
		rejected("parameters", "{broken");
		rejected("curves", R"({"svs.pitch":{"futureFormat":2}})");
		rejected("len", "invalid");
		auto future = clipDoc.documentElement().cloneNode(true).toElement();
		future.setAttribute("schemaVersion", 99);
		clip->loadSettings(future);
		auto* protectedCopy = static_cast<SVSClip*>(clip->clone());
		QVERIFY(protectedCopy->readOnly());
		QVERIFY(protectedCopy->id() != clip->id());
		QVERIFY(protectedCopy->notes()[0].id != clip->notes()[0].id);
		QDomDocument protectedDocument;
		auto protectedNode = protectedDocument.createElement("svsclip");
		protectedCopy->saveSettings(protectedDocument, protectedNode);
		QCOMPARE(protectedNode.attribute("id"), protectedCopy->id());
		QCOMPARE(protectedNode.firstChildElement("notes").firstChildElement("note").attribute("id"),
			protectedCopy->notes()[0].id);
		QCOMPARE(protectedNode.attribute("schemaVersion"), QString("99"));
		QCOMPARE(protectedNode.attribute("curves"), future.attribute("curves"));
		QCOMPARE(protectedNode.firstChildElement("notes")
					 .firstChildElement("note")
					 .firstChildElement("vendorState")
					 .attribute("value"),
			QString("x"));
		delete protectedCopy;
		auto badTrack = source.documentElement().cloneNode(true).toElement();
		badTrack.setAttribute("schemaVersion", 99);
		track->loadTrackSpecificSettings(badTrack);
		QVERIFY(track->readOnly());
		QVERIFY(copy->readOnly());
		QVERIFY(!track->setLanguage("en-US"));
		track->bindVoice("other", "other");
		QCOMPARE(track->pluginId(), QString("missing.plugin"));
		QDomDocument output;
		auto result = output.createElement("svstrack");
		track->saveTrackSpecificSettings(output, result, false);
		QCOMPARE(result.attribute("schemaVersion"), QString("99"));
		QCOMPARE(result.attribute("language"), QString("ja-JP"));
		QVERIFY(!result.firstChildElement("vendorState").isNull());
		delete track;
	}
	void arrangementSplitAndTrackClone()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		clip->setAutoResize(false);
		clip->changeLength(384);
		clip->setStartTimeOffset(-24);
		svs::Note note;
		note.id = "split-arrangement";
		note.tick = 48;
		note.duration = 240;
		note.pronunciation = "l a";
		note.parameters = {{"example.power", 75}, {"unknown", 42}};
		note.phonemes = {{"phonemeSet", "example.multilingual.v1"}, {"symbols", QJsonArray{"l", "a"}}};
		svs::Curve curve;
		curve.id = "svs.pitch";
		curve.unit = "semitone";
		curve.mode = "absolute";
		curve.interpolation = "hermite";
		curve.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
		for (const auto& point : QVector<QPointF>{{0, 60}, {100, 63}, {150, 65}, {300, 68}, {420, 70}})
			curve.insert(point.x(), point.y());
		clip->setEditorData({note}, {{curve.id, curve}});
		auto* journal = Engine::projectJournal();
		const auto enabled = journal->isJournalling();
		auto restore = qScopeGuard([journal, enabled] { journal->setJournalling(enabled); });
		journal->setJournalling(true);
		track->setJournalling(true);
		clip->setJournalling(true);
		QVERIFY(!clip->splitAt(192));
		QVERIFY(!clip->splitAt(576));
		auto* right = clip->splitAt(312);
		QVERIFY(right);
		QCOMPARE(track->numOfClips(), 2);
		QCOMPARE(clip->length(), TimePos(120));
		QCOMPARE(right->startPosition(), TimePos(312));
		QCOMPARE(right->length(), TimePos(264));
		QCOMPARE(clip->startTimeOffset(), TimePos(0));
		QCOMPARE(right->startTimeOffset(), TimePos(0));
		QCOMPARE(clip->notes()[0].tick, 24.);
		QCOMPARE(clip->notes()[0].duration, 96.);
		QCOMPARE(right->notes()[0].tick, 0.);
		QCOMPARE(right->notes()[0].duration, 144.);
		QVERIFY(right->notes()[0].id != note.id);
		QCOMPARE(right->notes()[0].parameters, note.parameters);
		QCOMPARE(right->notes()[0].pronunciation, note.pronunciation);
		QVERIFY(clip->notes()[0].phonemes.isEmpty());
		QVERIFY(right->notes()[0].phonemes.isEmpty());
		for (double tick = 0; tick <= 120; tick += .25)
			QVERIFY(std::abs(clip->curves()[curve.id].valueAt(tick)->toDouble() - curve.valueAt(tick + 24)->toDouble())
				< 1e-8);
		for (double tick = 0; tick <= 264; tick += .25)
			QVERIFY(
				std::abs(right->curves()[curve.id].valueAt(tick)->toDouble() - curve.valueAt(tick + 144)->toDouble())
				< 1e-8);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() && right->audio(), 10000);
		const auto rightId = right->id();
		journal->undo();
		QCOMPARE(track->numOfClips(), 1);
		clip = static_cast<SVSClip*>(track->getClip(0));
		QCOMPARE(clip->notes()[0].phonemes, note.phonemes);
		QCOMPARE(clip->length(), TimePos(384));
		QCOMPARE(clip->startTimeOffset(), TimePos(-24));
		QCOMPARE(clip->curves()[curve.id].toJson(), curve.toJson());
		journal->redo();
		QCOMPARE(track->numOfClips(), 2);
		QCOMPARE(static_cast<SVSClip*>(track->getClip(1))->id(), rightId);
		auto* copied = static_cast<SVSTrack*>(track->clone());
		QCOMPARE(copied->numOfClips(), 2);
		for (int i = 0; i < 2; ++i)
		{
			auto* source = static_cast<SVSClip*>(track->getClip(i));
			auto* target = static_cast<SVSClip*>(copied->getClip(i));
			QVERIFY(source->id() != target->id());
			QVERIFY(source->notes()[0].id != target->notes()[0].id);
			QCOMPARE(source->notes()[0].parameters, target->notes()[0].parameters);
			QCOMPARE(source->curves(), target->curves());
		}
		delete copied;
		delete track;
	}
	void resultOriginAndCopyPosition()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		const auto tempo = Engine::getSong()->getTempo();
		const double spt = 60. / (tempo * 48);
		svs::Note note;
		note.id = "origin-note";
		note.tick = 0;
		note.duration = 96;
		note.phonemes = {{"phonemeSet", "example.multilingual.v1"}, {"symbols", QJsonArray{"l", "a"}},
			{"segments",
				QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", -6.}, {"durationTicks", 12.}},
					QJsonObject{{"symbol", "a"}, {"startTick", 6.}, {"durationTicks", 90.}}}}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto original = clip->audio();
		QVERIFY(std::abs(original->startTick + 6) < 1e-9);
		QVERIFY(std::abs(original->startSeconds - 186 * spt) < 1e-9);
		double leadEnergy = 0;
		for (size_t frame = 0; frame < size_t(6 * spt * original->rate); ++frame)
			leadEnergy += original->samples[frame * 2] * original->samples[frame * 2];
		QVERIFY(leadEnergy > 0.01);
		auto* copy = static_cast<SVSClip*>(clip->clone());
		copy->movePosition(960);
		QTRY_VERIFY_WITH_TIMEOUT(copy->audio() != nullptr, 10000);
		QVERIFY(copy->id() != clip->id());
		QVERIFY(copy->notes()[0].id != note.id);
		QCOMPARE(copy->notes()[0].tick, 0.);
		QCOMPARE(copy->notes()[0].phonemes, note.phonemes);
		QVERIFY(std::abs(copy->audio()->startSeconds - 954 * spt) < 1e-9);
		QVERIFY(std::abs(copy->audio()->startTick + 6) < 1e-9);
		svs::TimeMapping mapping{960, 24, spt};
		QCOMPARE(mapping.projectTick(24), 960.);
		QCOMPARE(mapping.localTick(960), 24.);
		QVERIFY(std::abs(mapping.resultStartTick(mapping.globalSeconds(-6)) + 6) < 1e-9);
		copy->setStartTimeOffset(-24);
		QVERIFY(!copy->audio());
		QTRY_VERIFY_WITH_TIMEOUT(copy->audio() != nullptr, 10000);
		QVERIFY(std::abs(copy->audio()->startSeconds - 930 * spt) < 1e-9);
		QCOMPARE(copy->notes()[0].tick, 0.);
		QVERIFY(std::abs(copy->audio()->startTick + 6) < 1e-9);
		auto energyAt = [&](int tick) {
			Engine::getSong()->getTimeline(Song::PlayMode::Song).setTicks(tick);
			Engine::getSong()->playSong();
			double energy = 0;
			for (int period = 0; period < 10; ++period)
				for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
					energy += frame[0] * frame[0] + frame[1] * frame[1];
			Engine::getSong()->stop();
			for (int period = 0; period < 4; ++period)
				Engine::audioEngine()->renderNextPeriod();
			return energy;
		};
		QVERIFY(energyAt(193) > 0.01);
		QVERIFY(energyAt(961) > 0.01);
		QVERIFY(energyAt(760) < 1e-8);
		Engine::getSong()->tempoModel().setValue(tempo + 20);
		QVERIFY(!clip->audio());
		QVERIFY(!copy->audio());
		QTRY_VERIFY_WITH_TIMEOUT(copy->audio() != nullptr, 10000);
		const double changedSpt = 60. / ((tempo + 20) * 48);
		QVERIFY(std::abs(copy->audio()->startSeconds - 930 * changedSpt) < 1e-9);
		QCOMPARE(copy->notes()[0].phonemes, note.phonemes);
		delete track;
		Engine::getSong()->tempoModel().setValue(tempo);
	}
	void cacheIdentityBudgetAndRecovery()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		svs::Cache cache(directory.path(), 9000, 25000);
		svs::Input input;
		input.clipId = "original";
		input.voiceId = "full";
		input.rate = 48000;
		input.revision = 1;
		input.generation = 1;
		input.request = 1;
		input.secondsPerTick = .01;
		input.duration = .2;
		svs::Note note;
		note.id = "original-note";
		note.lyric = "hello";
		input.notes = {note};
		input.document = {{"clipId", input.clipId}, {"position", 192}, {"contentOffset", 24}, {"tempo", 120},
			{"voiceVersion", "1"}, {"trackParameters", QJsonObject{{"gain", 1}}}};
		auto audio = std::make_shared<svs::Audio>();
		audio->samples.resize(2000, .25f);
		audio->waveform.build(audio->samples);
		audio->revision = 1;
		audio->feedback = {{"phonemes", QJsonArray{QJsonObject{{"noteId", note.id}, {"symbol", "hello"}}}},
			{"pronunciations", QJsonObject{{note.id, QJsonObject{{"text", "hello"}}}}}};
		const auto key = svs::Cache::key(input, "plugin-version");
		cache.put(key, input, audio);
		QVERIFY(cache.memoryBytes() <= 9000);
		QVERIFY(cache.diskBytes() <= 25000);
		auto copy = input;
		copy.clipId = "copy";
		copy.document["clipId"] = copy.clipId;
		copy.notes[0].id = "copy-note";
		copy.revision = 3;
		copy.request = 5;
		copy.generation = 4;
		QCOMPARE(svs::Cache::key(copy, "plugin-version"), key);
		auto loaded = cache.get(key, copy);
		QVERIFY(loaded);
		QCOMPARE(loaded->revision, uint64_t(3));
		QCOMPARE(loaded->feedback["phonemes"].toArray()[0].toObject()["noteId"].toString(), QString("copy-note"));
		QVERIFY(loaded->feedback["pronunciations"].toObject().contains("copy-note"));
		QCOMPARE(loaded->samples, audio->samples);
		cache.clearMemory();
		loaded = cache.get(key, copy);
		QVERIFY(loaded);
		QCOMPARE(loaded->samples, audio->samples);
		QVERIFY(!loaded->feedback["pronunciations"].toObject().contains("original-note"));
		for (const auto& field : QStringList{"position", "tempo", "contentOffset", "voiceVersion"})
		{
			auto changed = input;
			changed.document[field] = QString("different");
			QVERIFY(svs::Cache::key(changed, "plugin-version") != key);
		}
		auto changed = input;
		changed.notes[0].lyric = "la";
		QVERIFY(svs::Cache::key(changed, "plugin-version") != key);
		changed = input;
		changed.rate = 44100;
		QVERIFY(svs::Cache::key(changed, "plugin-version") != key);
		QVERIFY(svs::Cache::key(input, "other-plugin") != key);
		cache.clearMemory();
		QFile cacheIndex(QDir(cache.engineDirectory(input.document["pluginId"].toString())).filePath(key + ".svsmeta"));
		QVERIFY(cacheIndex.open(QIODevice::ReadOnly));
		const auto audioHash = QJsonDocument::fromJson(cacheIndex.readAll()).object()["audioSHA256"].toString();
		cacheIndex.close();
		QFile corrupt(QDir(cache.engineDirectory(input.document["pluginId"].toString())).filePath(audioHash + ".wav"));
		QVERIFY(corrupt.open(QIODevice::ReadWrite));
		QVERIFY(corrupt.seek(40));
		QVERIFY(corrupt.write("bad") == 3);
		corrupt.close();
		QVERIFY(!cache.get(key, copy));
		cache.put(key, input, audio);
		cache.clearMemory();
		QVERIFY(cache.get(key, copy));
		for (int i = 0; i < 6; ++i)
		{
			auto different = input;
			different.document["position"] = i * 192;
			cache.put(svs::Cache::key(different, "plugin-version"), different, audio);
			QVERIFY(cache.memoryBytes() <= 9000);
			QVERIFY(cache.diskBytes() <= 25000);
		}
		QVERIFY(!cache.get("../outside", input));
	}
	void schedulerShutdownWaitsForWorkers()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString error;
		svs::Input input;
		input.clipId = "shutdown";
		input.voiceId = "full";
		input.secondsPerTick = .01;
		input.duration = .2;
		input.notes = {svs::Note{}};
		input.document = {{"capabilities", plugin->capabilities("full", {}, error)}, {"language", "en"},
			{"developmentFaults", QJsonObject{{"delayMs", 150}, {"lateReturn", true}}}};
		QSemaphore entered, finished;
		int published = 0;
		svs::SynthesisScheduler scheduler(1);
		auto running = scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++published; });
		auto queued = scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++published; });
		scheduler.declarationPool().start(QRunnable::create([&] {
			entered.release();
			QThread::msleep(150);
			finished.release();
		}));
		QVERIFY(entered.tryAcquire(1, 2000));
		scheduler.shutdown();
		QVERIFY(finished.tryAcquire());
		QVERIFY(running->cancelled);
		QVERIFY(queued->cancelled);
		QCOMPARE(scheduler.activeCount(), 0);
		QCOMPARE(scheduler.queuedCount(), 0);
		QCoreApplication::processEvents();
		QCOMPARE(published, 0);
		auto rejected = scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++published; });
		QVERIFY(rejected->cancelled);
		QCOMPARE(scheduler.activeCount(), 0);
	}
	void synthesisCancellationAndBudget()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString error;
		svs::Input input;
		input.clipId = "scheduler-test";
		input.voiceId = "full";
		input.revision = 1;
		input.request = 1;
		input.generation = 1;
		input.secondsPerTick = 60. / (120 * 48);
		input.duration = .2;
		svs::Note note;
		note.id = "note";
		note.duration = 19.2;
		input.notes = {note};
		input.document
			= {{"capabilities", plugin->capabilities("full", {}, error)}, {"secondsPerTick", input.secondsPerTick},
				{"language", "en"}, {"developmentFaults", QJsonObject{{"delayMs", 100}, {"lateReturn", true}}}};
		svs::SynthesisScheduler scheduler(2);
		int oldResults = 0, newResults = 0;
		QStringList states;
		auto first = scheduler.submit(
			plugin, input, 0, [&](const QString& state) { states << state; }, [&](auto, const auto&) { ++oldResults; });
		QCOMPARE(scheduler.activeCount(), 1);
		scheduler.cancel(first);
		QCOMPARE(scheduler.activeCount(), 1);
		input.revision = 2;
		input.request = 2;
		input.notes[0].lyric = "hello";
		input.document.remove("developmentFaults");
		input.document["queryCapabilities"] = true;
		std::shared_ptr<const svs::Audio> published;
		scheduler.submit(
			plugin, input, 10, [](const auto&) {},
			[&](auto audio, const auto&) {
				published = audio;
				++newResults;
			});
		QCOMPARE(scheduler.activeCount(), 1);
		QCOMPARE(scheduler.queuedCount(), 1);
		QTRY_COMPARE_WITH_TIMEOUT(newResults, 1, 5000);
		QVERIFY(published);
		QCOMPARE(published->revision, uint64_t(2));
		QCOMPARE(
			published->feedback["pronunciations"].toObject()["note"].toObject()["text"].toString(), QString("hello"));
		QCOMPARE(oldResults, 0);
		QCOMPARE(scheduler.activeCount(), 0);
		QCOMPARE(scheduler.peakActiveCount(), 1);
		QCOMPARE(states, QStringList({"Queued", "Rendering"}));
		// Separate engines may occupy both global slots; queued cancellation does
		// not release running slots, and a failed job can be retried.
		const auto library = QDir(voice.package).filePath("SVSExample");
		auto second = std::make_shared<svs::Plugin>(library);
		QVERIFY2(second->valid(), qPrintable(second->error()));
		input.document["developmentFaults"] = QJsonObject{{"delayMs", 80}};
		int complete = 0;
		auto a = scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++complete; });
		auto b = scheduler.submit(second, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++complete; });
		QCOMPARE(scheduler.activeCount(), 2);
		auto pending = scheduler.submit(plugin, input, 0, [](const auto&) {}, [&](auto, const auto&) { ++oldResults; });
		scheduler.cancel(pending);
		QCOMPARE(scheduler.queuedCount(), 0);
		QCOMPARE(scheduler.activeCount(), 2);
		QTRY_COMPARE_WITH_TIMEOUT(complete, 2, 5000);
		QCOMPARE(scheduler.peakActiveCount(), 2);
		QVERIFY(scheduler.peakActiveCount() <= scheduler.budget());
		input.document["developmentFaults"] = QJsonObject{{"fail", true}};
		bool failed = false;
		scheduler.submit(
			plugin, input, 0, [](const auto&) {},
			[&](auto audio, const auto& diagnostic) { failed = !audio && !diagnostic.isEmpty(); });
		QTRY_VERIFY_WITH_TIMEOUT(failed, 5000);
		input.document.remove("developmentFaults");
		scheduler.submit(
			plugin, input, 0, [](const auto&) {},
			[&](auto audio, const auto&) {
				if (audio)
					++complete;
			});
		QTRY_COMPARE_WITH_TIMEOUT(complete, 3, 5000);
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setNotes({note});
		clip->cancelSynthesis();
		QCOMPARE(clip->status(), QString("Cancelled"));
		QVERIFY(!clip->audio());
		note.lyric = "hello";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 5000);
		QCOMPARE(clip->audio()->feedback["pronunciations"].toObject()["note"].toObject()["text"].toString(),
			QString("hello"));
		clip->setNotes({svs::Note{}});
		delete track;
		QTRY_COMPARE_WITH_TIMEOUT(svs::SynthesisScheduler::instance().activeCount(), 0, 5000);
	}
	void finalEditorInteractionGates()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "hover-note";
		note.tick = 48;
		note.duration = 96;
		note.lyric = "la";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		gui::SVSCanvas canvas(clip);
		canvas.resize(900, 500);
		const auto point = canvas.noteRect(note).center().toPoint();
		QHelpEvent help(QEvent::ToolTip, point, canvas.mapToGlobal(point));
		QApplication::sendEvent(&canvas, &help);
		QVERIFY(canvas.toolTip().contains("voiceDictionary"));
		QVERIFY(canvas.toolTip().contains("Phonemes"));
		const auto* mode = track->capabilities().parameter("example.mode", "track");
		QVERIFY(mode);
		gui::SVSCanvas lane(clip);
		lane.resize(900, 200);
		lane.setParameterLane(*mode);
		lane.setTool(gui::SVSCanvas::Tool::Anchor);
		svs::Curve curve;
		curve.id = mode->id;
		curve.type = "enum";
		curve.interpolation = "step";
		curve.evaluator.interpolation = svs_sdk::Interpolation::Step;
		curve.insert(48, "basic");
		curve.insert(96, "advanced");
		clip->setEditorData(clip->notes(), {{curve.id, curve}});
		const auto anchor = lane.curvePointAt(96, 1).toPoint();
		QTest::mousePress(&lane, Qt::LeftButton, Qt::NoModifier, anchor + QPoint(-12, -2));
		QTest::mouseMove(&lane, anchor + QPoint(12, 10));
		QTest::mouseRelease(&lane, Qt::LeftButton, Qt::NoModifier, anchor + QPoint(12, 10));
		lane.deleteSelection();
		QCOMPARE(clip->curves()[curve.id].evaluator.points.size(), size_t(1));
		QCOMPARE(clip->curves()[curve.id].evaluator.points[0].valueId, std::string("basic"));
		auto* mime = new QMimeData;
		mime->setData("application/x-lmms-svs-curve",
			QJsonDocument(QJsonObject{{"curve", curve.toJson()}, {"length", 48.}}).toJson());
		QApplication::clipboard()->setMimeData(mime);
		const auto before = clip->curves();
		auto readOnly = *mode;
		readOnly.writable = false;
		lane.setParameterLane(readOnly);
		lane.pasteSelection(300);
		QCOMPARE(clip->curves(), before);
		canvas.pasteSelection(300);
		QCOMPARE(clip->curves(), before);
		gui::SVSResultStrip strip(clip);
		auto* journal = Engine::projectJournal();
		const auto enabled = journal->isJournalling();
		auto reset = qScopeGuard([journal, enabled] { journal->setJournalling(enabled); });
		journal->setJournalling(true);
		clip->setJournalling(true);
		auto notes = clip->notes();
		notes[0].lyric = "hello";
		clip->setNotes(notes);
		QTest::keyClick(&strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes()[0].lyric, QString("la"));
		QTest::keyClick(&strip, Qt::Key_Y, Qt::ControlModifier);
		QCOMPARE(clip->notes()[0].lyric, QString("hello"));
		delete track;
	}
	void relativePitchUsesDeclaredReference()
	{
		svs::Curve edited;
		edited.id = "svs.pitch";
		edited.unit = "semitone";
		edited.mode = "absolute";
		edited.interpolation = "hermite";
		edited.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
		for (auto point : QVector<QPointF>{{0, 60}, {110.25, 66}, {240.5, 63}, {410.75, 71}, {600, 68}})
			edited.insert(point.x(), point.y());
		auto reference = edited;
		reference.id = "svs.referencePitch";
		reference.evaluator.points.clear();
		for (auto point : QVector<QPointF>{{0, 64}, {180.5, 60}, {360.25, 70}, {600, 64}})
			reference.insert(point.x(), point.y());
		svs::Curve offset;
		QString error;
		QVERIFY2(svs::absolutePitchToOffset(edited, reference, offset, error), qPrintable(error));
		for (double tick = 0; tick <= 600; tick += .25)
		{
			auto relative = offset.valueAt(tick);
			QVERIFY(relative);
			QVERIFY(
				std::abs(relative->toDouble() + reference.valueAt(tick)->toDouble() - edited.valueAt(tick)->toDouble())
				< 1e-9);
		}
		svs::Curve restored;
		QVERIFY(svs::Curve::fromJson(offset.toJson(), restored, error));
		QCOMPARE(restored.toJson(), offset.toJson());
		auto broken = reference;
		broken.erase(140, 240);
		QVERIFY(!svs::absolutePitchToOffset(edited, broken, offset, error));
		QVERIFY(error.contains("Reference"));
		auto stepped = reference;
		stepped.interpolation = "step";
		stepped.evaluator.interpolation = svs_sdk::Interpolation::Step;
		QVERIFY(svs::absolutePitchToOffset(edited, stepped, offset, error));
		for (double tick = 0; tick <= 600; tick += .25)
			QVERIFY(std::abs(offset.valueAt(tick)->toDouble() + stepped.valueAt(tick)->toDouble()
						- edited.valueAt(tick)->toDouble())
				< 1e-9);
		const auto voice = svs::Registry::instance().voices().first();
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		auto schema = plugin->capabilities("full", {}, error);
		auto relativeSchema = schema;
		auto pitch = relativeSchema["pitch"].toObject();
		pitch["input"] = "offset";
		pitch["referencePitch"] = reference.toJson();
		relativeSchema["pitch"] = pitch;
		svs::Capabilities capabilities;
		QVERIFY2(svs::Capabilities::parse(relativeSchema, capabilities, error), qPrintable(error));
		auto missing = relativeSchema;
		pitch.remove("referencePitch");
		missing["pitch"] = pitch;
		QVERIFY(!svs::Capabilities::parse(missing, capabilities, error));
		svs::Input input;
		input.clipId = "offset-test";
		input.voiceId = "full";
		input.revision = 1;
		input.rate = 48000;
		input.secondsPerTick = .002;
		input.duration = 600 * input.secondsPerTick;
		svs::Note note;
		note.id = "pitch-note";
		note.tick = 0;
		note.duration = 600;
		input.notes = {note};
		input.document = {{"capabilities", schema}, {"language", "en"}, {"secondsPerTick", input.secondsPerTick},
			{"curves", svs::curvesToJson({{edited.id, edited}})}};
		auto absolute = plugin->render(input, error);
		QVERIFY2(absolute != nullptr, qPrintable(error));
		input.document["capabilities"] = relativeSchema;
		const auto saved = input.document["curves"];
		auto converted = plugin->render(input, error);
		QVERIFY2(converted != nullptr, qPrintable(error));
		QCOMPARE(input.document["curves"], saved);
		QCOMPARE(absolute->samples.size(), converted->samples.size());
		double difference = 0;
		for (size_t i = 0; i < absolute->samples.size(); ++i)
			difference = std::max(difference, std::abs(double(absolute->samples[i] - converted->samples[i])));
		QVERIFY2(difference < 1e-5, qPrintable(QString::number(difference, 'g', 17)));
		const auto values = converted->feedback["pitch"].toArray();
		QVERIFY(!values.isEmpty());
		for (const auto& value : values)
		{
			const auto sample = value.toObject();
			const auto tick = sample["startSeconds"].toDouble() / input.secondsPerTick;
			QVERIFY(std::abs(sample["value"].toDouble() - edited.valueAt(tick)->toDouble()) < 1e-9);
		}
	}
	void continuationChainAndNavigation()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		const auto voice = svs::Registry::instance().voices().first();
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note root;
		root.id = "chain-root";
		root.tick = 0;
		root.duration = 48;
		root.lyric = "la";
		auto held = root;
		held.id = "chain-held";
		held.tick = 48;
		held.lyric = track->capabilities().continuation;
		auto second = held;
		second.id = "chain-second";
		second.tick = 96;
		auto last = root;
		last.id = "chain-last";
		last.tick = 144;
		clip->setNotes({last, second, root, held});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto chained = clip->audio();
		auto pronunciations = chained->feedback["pronunciations"].toObject();
		for (const auto& id : QStringList{held.id, second.id})
		{
			QVERIFY(pronunciations[id].toObject()["continuation"].toBool());
			QVERIFY(pronunciations[id].toObject()["diagnostic"].toString().isEmpty());
			QCOMPARE(pronunciations[id].toObject()["text"], pronunciations[root.id].toObject()["text"]);
		}
		QVERIFY(gui::editorPronunciation(clip, second).continuation);
		gui::SVSCanvas canvas(clip);
		canvas.resize(900, 500);
		canvas.beginLyric(root.id);
		auto* text = canvas.findChild<QLineEdit*>("svsInlineLyric");
		QVERIFY(text);
		QTest::keyClick(text, Qt::Key_Tab);
		QVERIFY(canvas.selectedNotes().contains(last.id));
		QTest::keyClick(text, Qt::Key_Tab, Qt::ShiftModifier);
		QVERIFY(canvas.selectedNotes().contains(root.id));
		QTest::keyClick(text, Qt::Key_Escape);
		root.duration = 144;
		clip->setNotes({root, last});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto sustained = clip->audio();
		QCOMPARE(chained->samples.size(), sustained->samples.size());
		double difference = 0;
		for (size_t i = 0; i < chained->samples.size(); ++i)
			difference = std::max(difference, std::abs(double(chained->samples[i] - sustained->samples[i])));
		QVERIFY2(difference < 1e-6, qPrintable(QString::number(difference, 'g', 17)));
		root.duration = 48;
		clip->setNotes({root, held, second, last});
		QTest::keyClick(&canvas, Qt::Key_A, Qt::ControlModifier);
		canvas.splitSelection(120);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		pronunciations = clip->audio()->feedback["pronunciations"].toObject();
		for (const auto& note : clip->notes())
			if (note.lyric == held.lyric)
				QVERIFY(pronunciations[note.id].toObject()["continuation"].toBool());
		held.tick = 49;
		clip->setNotes({root, held, second});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		pronunciations = clip->audio()->feedback["pronunciations"].toObject();
		QVERIFY(!pronunciations[held.id].toObject()["continuation"].toBool());
		QVERIFY(!pronunciations[second.id].toObject()["continuation"].toBool());
		QVERIFY(!pronunciations[held.id].toObject()["diagnostic"].toString().isEmpty());
		delete track;
	}
	void noteSplitReparsesPhonemes()
	{
		auto* journal = Engine::projectJournal();
		const auto enabled = journal->isJournalling();
		auto reset = qScopeGuard([journal, enabled] { journal->setJournalling(enabled); });
		journal->setJournalling(true);
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		const auto voice = svs::Registry::instance().voices().first();
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setJournalling(true);
		svs::Note note;
		note.id = "split-phoneme";
		note.tick = 48;
		note.duration = 192;
		note.lyric = "la";
		note.pronunciation = "la";
		note.parameters = {{"example.power", 80}};
		note.phonemes = {{"symbols", QJsonArray{"l", "a"}},
			{"segments",
				QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", -12}, {"durationTicks", 120}},
					QJsonObject{{"symbol", "a"}, {"startTick", 108}, {"durationTicks", 84}}}}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		svs::Curve curve;
		curve.id = "svs.pitch";
		curve.mode = "absolute";
		curve.insert(0, 60.);
		curve.insert(300, 64.);
		clip->setEditorData(clip->notes(), {{curve.id, curve}});
		gui::SVSCanvas canvas(clip);
		canvas.resize(900, 500);
		QTest::keyClick(&canvas, Qt::Key_A, Qt::ControlModifier);
		const auto original = clip->notes();
		const auto curves = clip->curves();
		const auto depth = journal->undoDepth();
		canvas.splitSelection(144);
		QCOMPARE(clip->notes().size(), 2);
		QCOMPARE(journal->undoDepth(), depth + 1);
		QCOMPARE(clip->notes()[0].duration, 96.);
		QCOMPARE(clip->notes()[1].tick, 144.);
		QCOMPARE(clip->notes()[1].duration, 96.);
		QVERIFY(clip->notes()[1].id != note.id);
		for (const auto& result : clip->notes())
		{
			QVERIFY(result.phonemes.isEmpty());
			QCOMPARE(result.lyric, note.lyric);
			QCOMPARE(result.pronunciation, note.pronunciation);
			QCOMPARE(result.parameters, note.parameters);
		}
		QCOMPARE(clip->curves(), curves);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto parsed = clip->audio()->feedback["pronunciations"].toObject();
		for (const auto& result : clip->notes())
			QVERIFY(parsed[result.id].toObject()["generated"].toBool());
		const auto divided = clip->notes();
		journal->undo();
		QCOMPARE(clip->notes(), original);
		journal->redo();
		QCOMPARE(clip->notes(), divided);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->notes(), divided);
		delete restored;
		QVERIFY(!svs::splitNoteForReparse(note, note.tick));
		QVERIFY(!svs::splitNoteForReparse(note, note.tick + note.duration));
		delete track;
	}
	void portraitResourcesAndPreferences()
	{
		QTemporaryDir package;
		QVERIFY(package.isValid());
		QImage source(160, 320, QImage::Format_ARGB32);
		source.fill(QColor(220, 20, 40, 255));
		source.setPixelColor(0, 0, Qt::transparent);
		auto path = package.filePath("portrait.png");
		QVERIFY(source.save(path));
		gui::SVSImageLoader loader;
		int completed = 0;
		loader.changed = [&] { ++completed; };
		loader.request(package.path(), path, {80, 80});
		QVERIFY(loader.image().isNull());
		QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(), 5000);
		QCOMPARE(loader.image().size(), QSize(40, 80));
		QVERIFY(loader.image().hasAlphaChannel());
		const auto cached = gui::SVSImageLoader::cacheBytes();
		QVERIFY(cached <= 32 * 1024 * 1024);
		auto duplicate = package.filePath("duplicate.png");
		QVERIFY(QFile::copy(path, duplicate));
		loader.request(package.path(), duplicate, {80, 80});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(), 5000);
		QCOMPARE(gui::SVSImageLoader::cacheBytes(), cached);
		loader.request(package.path(), path, {100, 100});
		loader.request(package.path(), package.filePath("missing.png"), {100, 100});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.diagnostic().isEmpty(), 5000);
		QVERIFY(loader.image().isNull());
		QFile broken(package.filePath("broken.png"));
		QVERIFY(broken.open(QIODevice::WriteOnly));
		broken.write("broken image");
		broken.close();
		loader.request(package.path(), broken.fileName(), {80, 80});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.diagnostic().isEmpty(), 5000);
		QVERIFY(loader.image().isNull());
		loader.request(package.path(), path, {80, 80});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(), 5000);
		for (const auto& variable : QStringList{"LMMS_SVS_TEST_AVATAR", "LMMS_SVS_TEST_PORTRAIT"})
		{
			const auto fixture = qEnvironmentVariable(variable.toUtf8().constData());
			if (fixture.isEmpty())
				continue;
			auto copy = package.filePath(variable + ".png");
			QVERIFY(QFile::copy(fixture, copy));
			loader.request(package.path(), copy, {400, 700});
			QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull() || !loader.diagnostic().isEmpty(), 5000);
			QVERIFY2(!loader.image().isNull(), qPrintable(loader.diagnostic()));
			QVERIFY(loader.image().width() <= 400 && loader.image().height() <= 700);
		}
		loader.request(package.path(), path, {80, 80});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(), 5000);
		ConfigManager::inst()->setValue("svs", "portraitVisible", "0");
		ConfigManager::inst()->setValue("svs", "portraitTransparency", "50");
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		QCOMPARE(track->portraitSettings()["transparency"].toInt(), 50);
		QVERIFY(!track->portraitSettings()["visible"].toBool());
		const auto voice = svs::Registry::instance().voices().first();
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "portrait-note";
		note.tick = 48;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto audio = clip->audio();
		gui::SVSPianoRoll editor(clip);
		auto* portrait = dynamic_cast<gui::SVSImageLoader*>(editor.findChild<QObject*>("svsPortraitLoader"));
		QVERIFY(portrait);
		QTRY_VERIFY_WITH_TIMEOUT(!portrait->image().isNull() || !portrait->diagnostic().isEmpty(), 5000);
		QVERIFY2(!portrait->image().isNull(), qPrintable(portrait->diagnostic()));
		auto* visible = editor.findChild<QCheckBox*>("svsPortraitVisible");
		auto* value = editor.findChild<QSpinBox*>("svsPortraitTransparencyValue");
		auto* slider = editor.findChild<QSlider*>("svsPortraitTransparency");
		QVERIFY(visible && value && slider);
		visible->setChecked(true);
		value->setValue(0);
		QCOMPARE(slider->value(), 0);
		QCOMPARE(clip->audio(), audio);
		auto settings = track->portraitSettings();
		settings["x"] = .25;
		settings["y"] = .5;
		track->setPortraitSettings(settings);
		const auto preferences = track->portraitSettings();
		track->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->portraitSettings(), preferences);
		editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #ffffff; }");
		QCOMPARE(track->portraitSettings(), preferences);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		ConfigManager::inst()->setValue("svs", "portraitTransparency", "99");
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		QCOMPARE(restored->portraitSettings(), preferences);
		delete restored;
		gui::SVSCanvas canvas(clip);
		canvas.resize(500, 300);
		canvas.setScroll(0, 66);
		canvas.setThemeColors({{"backgroundColor", QColor("#101010")}, {"gridLineColor", QColor("#101010")}});
		source.fill(QColor(220, 20, 40));
		auto rendered = [&](int transparency) {
			canvas.setPortrait(source, true, transparency);
			QImage image(canvas.size(), QImage::Format_ARGB32);
			canvas.render(&image);
			return image.pixelColor(490, 290);
		};
		QCOMPARE(rendered(0), QColor(220, 20, 40));
		const auto half = rendered(50);
		QVERIFY(std::abs(half.red() - 118) <= 1);
		QCOMPARE(rendered(100), QColor("#101010"));
		canvas.setPortrait(source, true, 0);
		const auto click = canvas.noteRect(note).center().toPoint();
		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, click);
		QVERIFY(canvas.selectedNotes().contains(note.id));
		auto* defaults = editor.findChild<QPushButton*>("svsPortraitDefaults");
		QVERIFY(defaults);
		value->setValue(50);
		defaults->click();
		auto* newTrack = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		QCOMPARE(newTrack->portraitSettings()["transparency"].toInt(), 50);
		delete newTrack;
		auto* reset = editor.findChild<QPushButton*>("svsPortraitReset");
		QVERIFY(reset);
		reset->click();
		QCOMPARE(track->portraitSettings()["transparency"].toInt(), 70);
		QCOMPARE(track->portraitSettings()["x"].toDouble(), 1.);
		delete track;
		ConfigManager::inst()->deleteValue("svs", "portraitVisible");
		ConfigManager::inst()->deleteValue("svs", "portraitTransparency");
	}
	void batchLyricsAndPronunciation()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		auto voice = svs::Registry::instance().voices().first();
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(!track->capabilities().languages.isEmpty(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note first;
		first.id = "a";
		first.tick = 0;
		first.duration = 48;
		first.lyric = "la";
		first.pronunciation = "retained";
		first.phonemes = {{"symbols", QJsonArray{"a"}}};
		auto held = first;
		held.id = "b";
		held.tick = 48;
		held.lyric = track->capabilities().continuation;
		held.pronunciation.clear();
		held.phonemes = {};
		auto last = held;
		last.id = "c";
		last.tick = 96;
		last.lyric = "la";
		clip->setNotes({first, held, last});
		const auto original = clip->notes();
		QCOMPARE(gui::SVSLyricEditor::splitLyrics(QString::fromUtf8("hello 世界 きゃ -")),
			QStringList({"hello", QString::fromUtf8("世"), QString::fromUtf8("界"), QString::fromUtf8("きゃ"), "-"}));
		{
			gui::SVSLyricEditor dialog(clip, {"a", "b", "c"});
			auto* text = dialog.findChild<QPlainTextEdit*>("svsBatchLyricText");
			QVERIFY(text);
			text->setPlainText(QString::fromUtf8("你 好"));
			QCOMPARE(clip->notes(), original);
			auto* table = dialog.findChild<QTableWidget*>("svsBatchLyricPreview");
			QCOMPARE(table->item(0, 2)->text(), QString::fromUtf8("你"));
			QCOMPARE(table->item(1, 2)->text(), held.lyric);
			QCOMPARE(table->item(2, 2)->text(), QString::fromUtf8("好"));
			dialog.reject();
			QCOMPARE(clip->notes(), original);
		}
		Engine::projectJournal()->setJournalling(true);
		clip->setJournalling(true);
		{
			gui::SVSLyricEditor dialog(clip, {"a", "b", "c"});
			auto* text = dialog.findChild<QPlainTextEdit*>();
			text->setPlainText("one two");
			QInputMethodEvent composing(QString::fromUtf8("未"), {});
			QApplication::sendEvent(text, &composing);
			dialog.accept();
			QCOMPARE(clip->notes(), original);
			QInputMethodEvent commit;
			commit.setCommitString("");
			QApplication::sendEvent(text, &commit);
			dialog.accept();
			QCOMPARE(clip->notes()[0].lyric, QString("one"));
			QCOMPARE(clip->notes()[2].lyric, QString("two"));
			QCOMPARE(clip->notes()[0].phonemes, first.phonemes);
			QCOMPARE(clip->notes()[0].pronunciation, first.pronunciation);
		}
		Engine::projectJournal()->undo();
		QCOMPARE(clip->notes(), original);
		Engine::projectJournal()->redo();
		QCOMPARE(clip->notes()[2].lyric, QString("two"));
		gui::SVSCanvas canvas(clip);
		canvas.setPronunciation("c", "manual");
		QCOMPARE(clip->notes()[2].pronunciation, QString("manual"));
		canvas.setPronunciation("c", {});
		QVERIFY(clip->notes()[2].pronunciation.isEmpty());
		bool foundCandidates = false;
		for (const auto& dictionary : track->dictionaries())
			for (auto entry = dictionary.entries.begin(); entry != dictionary.entries.end(); ++entry)
				if (!foundCandidates && entry.value().toArray().size() > 1)
				{
					auto notes = clip->notes();
					notes[2].lyric = entry.key();
					notes[2].language = dictionary.language;
					clip->setNotes(notes);
					const auto candidates = gui::editorPronunciation(clip, clip->notes()[2], true).candidates;
					QVERIFY(candidates.size() > 1);
					const auto choice = candidates.last().toObject()["reading"].toString();
					canvas.setPronunciation("c", choice);
					const auto resolved = gui::editorPronunciation(clip, clip->notes()[2]);
					QCOMPARE(resolved.text, choice);
					QCOMPARE(resolved.source, QString("manualPronunciation"));
					QVERIFY(resolved.generated);
					foundCandidates = true;
				}
		QVERIFY(foundCandidates);
		{
			gui::SVSLyricEditor dialog(clip, {"a"});
			auto newer = clip->notes();
			newer[0].lyric = "changed elsewhere";
			clip->setNotes(newer);
			dialog.accept();
			QCOMPARE(clip->notes(), newer);
			QCOMPARE(dialog.result(), 0);
		}
		Engine::projectJournal()->setJournalling(false);
		delete track;
	}
	void continuousCurves()
	{
		svs::Curve curve;
		curve.id = "pitch";
		curve.unit = "semitone";
		curve.interpolation = "hermite";
		curve.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
		for (const auto& point : QVector<QPointF>{{0, 60}, {120.5, 64}, {260.25, 67}, {410.5, 69}, {600, 72}})
			curve.insert(point.x(), point.y());
		for (double tick : {120.5, 192., 260.25, 384., 410.5, 576.})
		{
			auto left = curve.evaluator.evaluate(tick - 1e-5), right = curve.evaluator.evaluate(tick + 1e-5);
			QVERIFY(left.covered && right.covered);
			QVERIFY(std::abs(left.value - right.value) < 1e-5);
			QVERIFY(std::abs(left.derivative - right.derivative) < 1e-5);
		}
		const auto copied = curve.slice(150.25, 550.5);
		for (double tick = 150.25; tick <= 550.5; tick += .75)
			QVERIFY(std::abs(curve.valueAt(tick)->toDouble() - copied.valueAt(tick - 150.25)->toDouble()) < 1e-8);
		curve.erase(205.5, 300.25);
		QVERIFY(!curve.valueAt(250));
		QVERIFY(curve.valueAt(205.5));
		auto bytes = QJsonDocument(curve.toJson()).toJson();
		svs::Curve restored;
		QString error;
		QVERIFY2(svs::Curve::fromJson(QJsonDocument::fromJson(bytes).object(), restored, error), qPrintable(error));
		QVERIFY(!restored.valueAt(250));
		QCOMPARE(restored.toJson(), curve.toJson());
		restored.connect(220, 280);
		QVERIFY(restored.valueAt(250));
		QVERIFY(!restored.valueAt(210));
		QVERIFY(!restored.valueAt(290));
		svs::Curve discrete;
		discrete.id = "enum";
		discrete.type = "enum";
		discrete.interpolation = "step";
		discrete.evaluator.interpolation = svs_sdk::Interpolation::Step;
		discrete.insert(0, "first");
		discrete.insert(12.5, "second");
		QCOMPARE(discrete.valueAt(12)->toString(), QString("first"));
		QCOMPARE(discrete.valueAt(12.5)->toString(), QString("second"));
	}
	void waveformPyramid()
	{
		std::vector<float> samples(8192 * 2);
		for (size_t frame = 0; frame < 8192; ++frame)
		{
			samples[frame * 2] = float(std::sin(frame * .04));
			samples[frame * 2 + 1] = float(std::cos(frame * .07));
		}
		svs::Waveform waveform;
		waveform.build(samples);
		QVERIFY(waveform.bytes() < samples.size() * sizeof(float) / 16);
		for (size_t first = 0; first < 8192; first += 512)
		{
			auto peak = waveform.peak(first, first + 512);
			float low = 1, high = -1;
			for (size_t frame = first; frame < first + 512; ++frame)
			{
				low = std::min({low, samples[frame * 2], samples[frame * 2 + 1]});
				high = std::max({high, samples[frame * 2], samples[frame * 2 + 1]});
			}
			QCOMPARE(peak.minimum, low);
			QCOMPARE(peak.maximum, high);
		}
		waveform.build({});
		QCOMPARE(waveform.bytes(), size_t(0));
		QCOMPARE(waveform.peak(0, 64).maximum, 0.f);
	}
	void phonemeConstraintsAcrossTempoChanges()
	{
		auto* song = Engine::getSong();
		const auto originalTempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto* automation = Track::create(Track::Type::Automation, song);
		auto* tempo = static_cast<AutomationClip*>(automation->createClip(0));
		tempo->addObject(&song->tempoModel());
		tempo->setAutoResize(false);
		tempo->changeLength(384);
		tempo->putValue(0, 120, false);
		tempo->putValue(192, 240, false);
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto cleanup = qScopeGuard([&] {
			delete track;
			delete automation;
			song->tempoModel().setValue(originalTempo);
			QCoreApplication::processEvents();
		});
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto snapshot = svs::TempoSource::forSong(*song).snapshot();
		QVERIFY(std::abs(snapshot->tickAfterSeconds(191.8, .005) - 192.56) < 1e-9);
		QVERIFY(std::abs(snapshot->tickAfterSeconds(192.2, -.005) - 191.62) < 1e-9);
		QVERIFY(std::abs(snapshot->tickAfterSeconds(0, -.02) + 1.92) < 1e-9);
		auto* clip = static_cast<SVSClip*>(track->createClip(168));
		clip->setAutoResize(false);
		clip->changeLength(192);
		clip->setStartTimeOffset(-24);
		svs::Note note;
		note.id = "tempo-phonemes";
		note.tick = 48;
		note.duration = 48;
		note.lyric = "la";
		note.phonemes = {{"phonemeSet", track->capabilities().phonemeSetId}, {"symbols", QJsonArray{"l", "a"}},
			{"segments",
				QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", 0}, {"durationTicks", 24}},
					QJsonObject{{"symbol", "a"}, {"startTick", 24}, {"durationTicks", 24}}}}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		gui::SVSResultStrip strip(clip);
		strip.resize(900, 110);
		strip.setViewport(48, 1);
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(108, 18));
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(60, 18));
		const auto segments = clip->notes()[0].phonemes["segments"].toArray();
		QVERIFY(std::abs(segments[0].toObject()["durationTicks"].toDouble() - .96) < 1e-8);
		QVERIFY(std::abs(segments[1].toObject()["startTick"].toDouble() - .96) < 1e-8);
		QVERIFY(strip.setSelectedParameter("example.phonemeGain", .5));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		for (const auto& item : clip->audio()->feedback["phonemes"].toArray())
			QVERIFY(item.toObject()["durationSeconds"].toDouble() + 1e-8 >= .005);
	}
	void phonemeBoundariesAndAttributes()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		svs::Note note;
		note.id = "phoneme-note";
		note.tick = 48;
		note.duration = 96;
		note.lyric = "la";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		gui::SVSResultStrip strip(clip);
		strip.resize(900, 110);
		const auto initial = clip->notes();
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(252, 18));
		QTest::mouseMove(&strip, QPoint(276, 18));
		QCOMPARE(clip->notes(), initial);
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(276, 18));
		auto segments = clip->notes()[0].phonemes["segments"].toArray();
		QCOMPARE(segments.size(), 2);
		QCOMPARE(segments[0].toObject()["durationTicks"].toDouble(), 60.);
		QCOMPARE(segments[1].toObject()["startTick"].toDouble(), 60.);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(156, 18));
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(132, 18));
		QCOMPARE(clip->notes()[0].phonemes["segments"].toArray()[0].toObject()["startTick"].toDouble(), -12.);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto audio = clip->audio();
		const double secondsPerTick = 60. / (Engine::getSong()->getTempo() * (DefaultTicksPerBar / 4));
		auto energy = [secondsPerTick](const auto& audio, double from, double to) {
			double result = 0;
			for (size_t frame = size_t(from * secondsPerTick * audio->rate);
				frame < size_t(to * secondsPerTick * audio->rate); ++frame)
				result += audio->samples[frame * 2] * audio->samples[frame * 2];
			return result;
		};
		QVERIFY(energy(audio, 40, 44) > 1e-3);
		QVERIFY(strip.setSelectedParameter("example.phonemeGain", 0));
		QVERIFY(!strip.setSelectedParameter("example.phonemeGain", 3));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QCOMPARE(energy(clip->audio(), 60, 80), 0.);
		QVERIFY(energy(clip->audio(), 120, 140) > 1);
		const auto before = clip->notes();
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(276, 18));
		QTest::mouseMove(&strip, QPoint(300, 18));
		QTest::keyClick(&strip, Qt::Key_Escape);
		QCOMPARE(clip->notes(), before);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->notes()[0].phonemes, before[0].phonemes);
		delete restored;
		track->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(252, 18));
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(300, 18));
		QCOMPARE(clip->notes(), before);
		QVERIFY(!strip.setSelectedParameter("example.phonemeGain", 1));
		delete track;
	}
	void canvasThemeProperties()
	{
		QImage placeholder("data:/themes/default/svs_track.svg");
		QVERIFY(!placeholder.isNull());
		QCOMPARE(placeholder.size(), QSize(24, 24));
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		gui::SVSPianoRoll editor(clip);
		auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas);
		const QStringList icons{"select", "pencil", "pitch", "anchor", "smooth", "line", "erase", "track"};
		for (int i = 0; i < icons.size(); ++i)
		{
			QImage image(QString("data:/themes/default/svs_%1.svg").arg(i < 7 ? "tool_" + icons[i] : icons[i]));
			QVERIFY(!image.isNull());
			QCOMPARE(image.size(), QSize(24, 24));
			bool ink = false, transparent = false;
			for (int y = 0; y < image.height(); ++y)
				for (int x = 0; x < image.width(); ++x)
				{
					const auto color = image.pixelColor(x, y);
					if (color.alpha())
					{
						ink = true;
						QCOMPARE(color.red(), 255);
						QCOMPARE(color.green(), 255);
						QCOMPARE(color.blue(), 255);
					}
					else
						transparent = true;
				}
			QVERIFY(ink);
			QVERIFY(transparent);
			if (i < 7)
			{
				auto* button = editor.findChild<QToolButton*>(QString("svsTool%1").arg(i));
				QVERIFY(button);
				QVERIFY(!button->icon().isNull());
				QCOMPARE(button->iconSize(), QSize(24, 24));
				QVERIFY(!button->toolTip().isEmpty());
			}
		}
		auto renderColor = [canvas, &editor] {
			editor.show();
			QCoreApplication::processEvents();
			canvas->window()->layout()->activate();
			QImage image(canvas->size(), QImage::Format_ARGB32);
			image.fill(Qt::transparent);
			canvas->render(&image);
			return image.pixelColor(image.width() - 5, image.height() - 5);
		};
		editor.setStyleSheet(
			"lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #132435; qproperty-noteColor: #456789; }");
		editor.ensurePolished();
		QCOMPARE(editor.backgroundColor(), QColor("#132435"));
		QCOMPARE(editor.noteColor(), QColor("#456789"));
		QCOMPARE(renderColor(), QColor("#132435"));
		editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #abcdef; }");
		editor.ensurePolished();
		QCOMPARE(renderColor(), QColor("#abcdef"));
		editor.setStyleSheet(
			"lmms--gui--SVSPianoRoll { qproperty-gridLineColor: #112233; qproperty-beatLineColor: #446655; qproperty-barLineColor: #77aa88; }");
		editor.ensurePolished();
		clip->movePosition(TimePos(12));
		canvas->setScroll(0, 72);
		canvas->setQuantization(12);
		canvas->setZoom(.5, 1);
		QCoreApplication::processEvents();
		auto verifyGrid = [&] {
			const auto image = canvas->grab().toImage();
			auto pixel = [&](double tick) {
				const auto point = canvas->pointAt(tick, canvas->topPitch() - .5).toPoint();
				return image.pixelColor(point);
			};
			QCOMPARE(pixel(double(DefaultTicksPerBar) / Engine::getSong()->getTimeSigModel().getDenominator() - 12),
				QColor("#446655"));
			QCOMPARE(pixel(TimePos::ticksPerBar() - 12), QColor("#77aa88"));
		};
		verifyGrid();
		const auto fine = canvas->grab().toImage();
		QCOMPARE(fine.pixelColor(canvas->pointAt(12, canvas->topPitch() - .5).toPoint()), QColor("#112233"));
		canvas->setQuantization(96);
		canvas->setScroll(13, 72);
		QCoreApplication::processEvents();
		verifyGrid();
		editor.setStyleSheet("");
		editor.ensurePolished();
		QVERIFY(!editor.backgroundColor().isValid());
		QCOMPARE(renderColor(), canvas->palette().base().color());
		delete track;
	}
	void curveRangeTransactions()
	{
		svs::Curve original;
		original.id = "svs.pitch";
		original.mode = "absolute";
		original.interpolation = "hermite";
		original.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
		for (const auto& point : QVector<QPointF>{{0, 60}, {100, 65}, {250, 67}, {400, 70}, {600, 72}})
			original.insert(point.x(), point.y());
		gui::SVSCurveGesture gesture;
		gesture.begin(original, 150.25, 64., gui::SVSCurveGesture::Kind::Line);
		gesture.update(350.75, 68.);
		for (double tick = 0; tick < 150.25; tick += .75)
			QVERIFY(std::abs(original.valueAt(tick)->toDouble() - gesture.preview.valueAt(tick)->toDouble()) < 1e-8);
		for (double tick = 351; tick <= 600; tick += .75)
			QVERIFY(std::abs(original.valueAt(tick)->toDouble() - gesture.preview.valueAt(tick)->toDouble()) < 1e-8);
		QCOMPARE(gesture.preview.valueAt(250.5)->toDouble(), 66.);
		auto piece = gesture.preview.slice(175, 325);
		for (double tick = 175; tick <= 325; tick += .25)
			QVERIFY(std::abs(piece.valueAt(tick - 175)->toDouble() - gesture.preview.valueAt(tick)->toDouble()) < 1e-8);
		gesture.begin(original, 150.25, 64., gui::SVSCurveGesture::Kind::Smooth);
		gesture.update(350.75, 68.);
		QVERIFY(std::abs(gesture.preview.derivativeAt(150.25)) < 1e-8);
		gesture.begin(original, 192.5, 64., gui::SVSCurveGesture::Kind::Erase);
		gesture.update(384.25, 68.);
		QVERIFY(!gesture.preview.valueAt(250));
		for (double tick = 0; tick < 192.5; tick += .75)
			QVERIFY(std::abs(original.valueAt(tick)->toDouble() - gesture.preview.valueAt(tick)->toDouble()) < 1e-8);
	}
	void freehandMonotonicInterpolation()
	{
		// Dense mouse events on a shallow ramp must not become dozens of corners.
		svs::Curve original;
		original.id = "continuous";
		original.type = "float";
		original.insert(0, 0.);
		original.insert(100, 1.);
		gui::SVSCurveGesture gesture;
		gesture.begin(original, 20., .2, gui::SVSCurveGesture::Kind::Freehand);
		for (int tick = 21; tick <= 60; ++tick)
			gesture.update(tick, tick / 100.);
		const auto& curve = gesture.preview;
		QVERIFY2(std::abs(curve.derivativeAt(20)) < 1e-9,
			"Freehand stroke must ease in with TuneLab's zero endpoint tangent");
		QVERIFY(std::abs(curve.derivativeAt(60)) < 1e-9);
		int interior = 0;
		for (const auto& point : curve.evaluator.points)
			if (point.tick >= 20 && point.tick <= 60)
				++interior;
		QVERIFY2(interior <= 10, "Dense pointer events must be simplified before monotonic Hermite interpolation");
		for (double tick = 20; tick <= 60; tick += .25)
		{
			const auto value = curve.valueAt(tick)->toDouble();
			QVERIFY(value >= .2 - 1e-9 && value <= .6 + 1e-9);
		}
		QCOMPARE(curve.valueAt(10), original.valueAt(10));
		QCOMPARE(curve.valueAt(80), original.valueAt(80));
		svs::Curve restored;
		QString error;
		QVERIFY(svs::Curve::fromJson(curve.toJson(), restored, error));
		for (double tick = 20; tick <= 60; tick += .25)
			QCOMPARE(restored.valueAt(tick), curve.valueAt(tick));
		gesture.begin(original, 0, .2, gui::SVSCurveGesture::Kind::Freehand);
		gesture.update(0, .2);
		const auto serialized = QJsonDocument::fromJson(QJsonDocument(gesture.preview.toJson()).toJson()).object();
		QVERIFY2(svs::Curve::fromJson(serialized, restored, error), qPrintable(error));
		// Independently calculated Hermite reference: harmonic interior tangent
		// 2/(1/.01+1/.02), zero endpoint tangent, and u=.5 on the first span.
		gesture.begin(original, 20., .2, gui::SVSCurveGesture::Kind::Freehand);
		gesture.update(40., .4);
		gesture.update(60., .8);
		QVERIFY(std::abs(gesture.preview.valueAt(30)->toDouble() - (.3 - 20 * (2. / (100 + 50)) * .125)) < 1e-9);
		const auto slopeLeft = gesture.preview.derivativeAt(40 - 1e-5),
				   slopeRight = gesture.preview.derivativeAt(40 + 1e-5);
		QVERIFY(std::abs(slopeLeft - slopeRight) < 1e-7);
		for (const auto& type : QStringList{"int", "bool", "enum"})
		{
			auto discrete = original;
			discrete.type = type;
			discrete.interpolation = "step";
			discrete.evaluator.interpolation = svs_sdk::Interpolation::Step;
			discrete.evaluator.points.clear();
			const QJsonValue low = type == "bool" ? QJsonValue(false)
				: type == "enum"				  ? QJsonValue("low")
												  : QJsonValue(0),
							 high = type == "bool" ? QJsonValue(true)
				: type == "enum"				   ? QJsonValue("high")
												   : QJsonValue(1);
			gesture.begin(discrete, 20, low, gui::SVSCurveGesture::Kind::Freehand);
			gesture.update(60, high);
			QCOMPARE(gesture.preview.valueAt(40), std::optional<QJsonValue>(low));
		}
	}
	void feedbackPitchInterpolation()
	{
		svs::TimeMapping mapping;
		mapping.secondsPerTick = .01;
		QJsonArray samples;
		for (int i = 0; i < 3; ++i)
			samples.append(QJsonObject{{"noteId", "first"}, {"startSeconds", i * .04}, {"durationSeconds", .04},
				{"value", i == 1 ? 64. : 60.}});
		const auto curves = gui::feedbackPitchCurves(samples, mapping);
		QCOMPARE(curves.size(), 1);
		const auto& curve = curves[0];
		QVERIFY(curve.valueAt(2)->toDouble() > 60.);
		QVERIFY(curve.valueAt(2)->toDouble() < 64.);
		QCOMPARE(curve.valueAt(4)->toDouble(), 64.);
		for (double tick = 0; tick < 12; tick += .1)
		{
			const auto value = curve.valueAt(tick)->toDouble();
			QVERIFY(value >= 60. - 1e-9 && value <= 64. + 1e-9);
		}
		const auto left = curve.derivativeAt(4 - 1e-5), right = curve.derivativeAt(4 + 1e-5);
		QVERIFY(std::abs(left - right) < 1e-4);
		samples.append(
			QJsonObject{{"noteId", "second"}, {"startSeconds", .12}, {"durationSeconds", .04}, {"value", 72.}});
		samples.append(
			QJsonObject{{"noteId", "second"}, {"startSeconds", .20}, {"durationSeconds", .04}, {"value", 75.}});
		const auto split = gui::feedbackPitchCurves(samples, mapping);
		QCOMPARE(split.size(), 3);
		QVERIFY(!split[1].valueAt(18));
		QCOMPARE(split[0].valueAt(12)->toDouble(), 60.);
		QCOMPARE(split[1].valueAt(12)->toDouble(), 72.);
	}
	void pitchDrawingAndPluginEvaluation()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		QVector<svs::Note> notes;
		for (int i = 0; i < 4; ++i)
		{
			svs::Note note;
			note.id = QString("pitch-%1").arg(i);
			note.tick = i == 3 ? 432 : i * 120;
			note.duration = i == 3 ? 144 : 96;
			note.pitch = 60 + i * 3;
			note.lyric = "la";
			notes.push_back(note);
		}
		clip->setNotes(notes);
		QVERIFY(clip->setParameter("example.breath", 0));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto baseline = clip->audio();
		gui::SVSCanvas canvas(clip);
		canvas.resize(1400, 500);
		canvas.setTool(gui::SVSCanvas::Tool::Freehand);
		auto begin = canvas.curvePointAt(0, 64).toPoint(), end = canvas.curvePointAt(600.25, 70).toPoint();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, begin);
		QVERIFY(clip->curves().isEmpty());
		for (auto point : QVector<QPointF>{{120.25, 65}, {260.5, 67}, {410.25, 68}})
			QTest::mouseMove(&canvas, canvas.curvePointAt(point.x(), point.y()).toPoint());
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
		QVERIFY(clip->curves().contains("svs.pitch"));
		const auto drawn = clip->curves()["svs.pitch"];
		QVERIFY(drawn.valueAt(192));
		QVERIFY(drawn.valueAt(384));
		QVERIFY(drawn.valueAt(576));
		QCOMPARE(clip->notes(), notes);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const double secondsPerTick = 60. / (Engine::getSong()->getTempo() * (DefaultTicksPerBar / 4));
		for (const auto& item : clip->audio()->feedback["pitch"].toArray())
		{
			const auto sample = item.toObject();
			const auto tick = sample["startSeconds"].toDouble() / secondsPerTick;
			auto value = drawn.valueAt(tick);
			QVERIFY(value);
			QVERIFY(std::abs(sample["value"].toDouble() - value->toDouble()) < 5e-4);
		}
		canvas.setTool(gui::SVSCanvas::Tool::Erase);
		const auto before = clip->curves();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.curvePointAt(200, 66).toPoint());
		QTest::mouseMove(&canvas, canvas.curvePointAt(300, 67).toPoint());
		QTest::keyClick(&canvas, Qt::Key_Escape);
		QCOMPARE(clip->curves(), before);
		auto curves = clip->curves();
		auto constant = drawn;
		constant.evaluator.points.clear();
		constant.evaluator.gaps.clear();
		constant.insert(0, 72.);
		constant.insert(600, 72.);
		curves["svs.pitch"] = constant;
		clip->setEditorData(notes, curves);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto octave = clip->audio();
		auto crossings = [](const std::shared_ptr<const svs::Audio>& audio) {
			int count = 0;
			for (size_t frame = size_t(audio->rate * .1); frame < size_t(audio->rate * .4); ++frame)
				if (audio->samples[(frame - 1) * 2] <= 0 && audio->samples[frame * 2] > 0)
					++count;
			return count;
		};
		QVERIFY(std::abs(double(crossings(octave)) / crossings(baseline) - 2) < .05);
		svs::Curve power;
		power.id = "example.power";
		power.type = "int";
		power.interpolation = "step";
		power.evaluator.interpolation = svs_sdk::Interpolation::Step;
		power.insert(0, 50);
		power.insert(600, 50);
		curves[power.id] = power;
		clip->setEditorData(notes, curves);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto energy = [](const auto& audio) {
			double result = 0;
			for (auto value : audio->samples)
				result += value * value;
			return result;
		};
		QVERIFY(std::abs(energy(clip->audio()) / energy(octave) - .25) < 1e-6);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->curves(), curves);
		delete restored;
		delete track;
	}
	void curveUndoAndSelectionMove()
	{
		auto* journal = Engine::projectJournal();
		const auto wasJournalling = journal->isJournalling();
		auto restoreJournal = qScopeGuard([journal, wasJournalling] { journal->setJournalling(wasJournalling); });
		journal->setJournalling(true);
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		clip->setJournalling(true);
		svs::Note note;
		note.id = "undo-note";
		note.tick = 48;
		note.duration = 96;
		note.pitch = 64;
		clip->setNotes({note});
		gui::SVSCanvas canvas(clip);
		canvas.resize(900, 500);
		canvas.setTool(gui::SVSCanvas::Tool::Line);
		const auto depth = journal->undoDepth();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.curvePointAt(0, 60).toPoint());
		QTest::mouseMove(&canvas, canvas.curvePointAt(192, 66).toPoint());
		QCOMPARE(journal->undoDepth(), depth);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.curvePointAt(384, 72).toPoint());
		QCOMPARE(journal->undoDepth(), depth + 1);
		const auto drawn = clip->curves();
		QTest::keyClick(&canvas, Qt::Key_Z, Qt::ControlModifier);
		QVERIFY(clip->curves().isEmpty());
		QTest::keyClick(&canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
		QCOMPARE(clip->curves(), drawn);
		canvas.setTool(gui::SVSCanvas::Tool::Notes);
		auto body = canvas.noteRect(note).center().toPoint();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(clip->curves(), drawn);
		note = clip->notes()[0];
		const auto from = note.tick, to = note.tick + note.duration;
		auto shape = drawn["svs.pitch"].slice(from, to);
		canvas.setMoveCurves(true);
		body = canvas.noteRect(note).center().toPoint();
		const auto beforeMove = journal->undoDepth();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(journal->undoDepth(), beforeMove + 1);
		for (double tick = 0; tick <= note.duration; tick += .5)
			QVERIFY(std::abs(shape.valueAt(tick)->toDouble()
						- clip->curves()["svs.pitch"].valueAt(from + 24 + tick)->toDouble())
				< 1e-8);
		QTest::keyClick(&canvas, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->curves(), drawn);
		QCOMPARE(clip->notes()[0].tick, from);
		delete track;
	}
	void noteTransactionsAndInlineLyrics()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		gui::SVSCanvas canvas(clip);
		canvas.resize(900, 500);
		const auto position = canvas.pointAt(24, 64) + QPointF(0, 6);
		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
		QVERIFY(clip->notes().isEmpty());
		QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
		QVERIFY(clip->notes().isEmpty());
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
		QCOMPARE(clip->notes().size(), 1);
		auto note = clip->notes()[0];
		note.duration = 96;
		clip->setNotes({note});
		const auto original = clip->notes();
		const auto body = canvas.noteRect(note).center().toPoint();
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseMove(&canvas, body + QPoint(48, 0));
		QCOMPARE(clip->notes(), original);
		QTest::keyClick(&canvas, Qt::Key_Escape);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(clip->notes(), original);
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseMove(&canvas, body + QPoint(48, 0));
		canvas.setTool(gui::SVSCanvas::Tool::Anchor);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(clip->notes(), original);
		canvas.setTool(gui::SVSCanvas::Tool::Notes);
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(clip->notes()[0].tick, 48.);
		QCOMPARE(clip->notes()[0].duration, 96.);
		canvas.beginLyric(note.id);
		auto* lyric = canvas.findChild<QLineEdit*>("svsInlineLyric");
		QVERIFY(lyric);
		lyric->setText("edited");
		QCOMPARE(clip->notes()[0].lyric, note.lyric);
		QTest::keyClick(lyric, Qt::Key_Return);
		QCOMPARE(clip->notes()[0].lyric, QString("edited"));
		canvas.beginLyric(note.id);
		lyric->setText("cancelled");
		QTest::keyClick(lyric, Qt::Key_Escape);
		QCOMPARE(clip->notes()[0].lyric, QString("edited"));
		canvas.copySelection();
		canvas.pasteSelection(384);
		QCOMPARE(clip->notes().size(), 2);
		QCOMPARE(clip->notes()[1].tick, 384.);
		QCOMPARE(clip->notes()[1].pitch, note.pitch);
		QVERIFY(clip->notes()[1].id != note.id);
		QVERIFY(int(clip->length()) >= 480);
		delete track;
	}
	void optimizedTuneLabNoteAndToolSemantics()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		gui::SVSPianoRoll editor(clip);
		auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas);
		canvas->resize(900, 500);
		canvas->setScroll(0, 72);
		canvas->setQuantization(12);
		QCOMPARE(canvas->tool(), gui::SVSCanvas::Tool::Pencil);
		QVERIFY(editor.findChild<QToolButton*>("svsTool1")->isChecked());
		auto* journal = Engine::projectJournal();
		const bool previous = journal->isJournalling();
		auto restore = qScopeGuard([&] { journal->setJournalling(previous); });
		journal->setJournalling(true);
		clip->setJournalling(true);
		const auto depth = journal->undoDepth();
		auto position = canvas->pointAt(24, 64) + QPointF(0, 12), end = canvas->pointAt(120, 64) + QPointF(0, 12);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
		QTest::mouseMove(canvas, end.toPoint());
		QVERIFY(clip->notes().isEmpty());
		QCOMPARE(journal->undoDepth(), depth);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end.toPoint());
		QCOMPARE(clip->notes().size(), 1);
		QCOMPARE(clip->notes()[0].tick, 24.);
		QCOMPARE(clip->notes()[0].duration, 96.);
		QCOMPARE(journal->undoDepth(), depth + 1);
		auto note = clip->notes()[0];
		auto body = canvas->noteRect(note).center().toPoint();
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, body);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, body + QPoint(48, 0));
		QCOMPARE(clip->notes()[0].tick, 48.);
		QCOMPARE(clip->notes()[0].duration, 96.);
		note = clip->notes()[0];
		auto edge = QPoint(int(canvas->noteRect(note).right() - 1), int(canvas->noteRect(note).center().y()));
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, edge);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, edge + QPoint(48, 0));
		QCOMPARE(clip->notes()[0].tick, 48.);
		QCOMPARE(clip->notes()[0].duration, 120.);
		for (int delta : {-48, 48})
		{
			note = clip->notes()[0];
			const auto tail = note.tick + note.duration;
			const auto resizeDepth = journal->undoDepth();
			edge = QPoint(int(canvas->noteRect(note).left() + 1), int(canvas->noteRect(note).center().y()));
			QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, edge);
			QTest::mouseMove(canvas, edge + QPoint(delta, 0));
			QCOMPARE(clip->notes()[0], note);
			QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, edge + QPoint(delta, 0));
			QCOMPARE(clip->notes()[0].tick, note.tick + delta / 2.);
			QCOMPARE(clip->notes()[0].tick + clip->notes()[0].duration, tail);
			QCOMPARE(journal->undoDepth(), resizeDepth + 1);
			QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
			QCOMPARE(clip->notes()[0], note);
			QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
			QCOMPARE(clip->notes()[0].tick, note.tick + delta / 2.);
		}
		const auto before = clip->notes();
		position = canvas->pointAt(192, 65) + QPointF(0, 12);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
		QTest::keyClick(canvas, Qt::Key_3);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint() + QPoint(72, 0));
		QCOMPARE(clip->notes(), before);
		QCOMPARE(canvas->tool(), gui::SVSCanvas::Tool::Freehand);
		const gui::SVSCanvas::Tool tools[]{gui::SVSCanvas::Tool::Notes, gui::SVSCanvas::Tool::Pencil,
			gui::SVSCanvas::Tool::Freehand, gui::SVSCanvas::Tool::Anchor, gui::SVSCanvas::Tool::Smooth};
		for (int i = 0; i < 5; ++i)
		{
			QTest::keyClick(canvas, Qt::Key_1 + i);
			QCOMPARE(canvas->tool(), tools[i]);
			QVERIFY(editor.findChild<QToolButton*>(QString("svsTool%1").arg(i))->isChecked());
			for (auto* lane : editor.findChildren<gui::SVSCanvas*>())
				QCOMPARE(lane->tool(), tools[i]);
		}
		QTest::keyClick(canvas, Qt::Key_1);
		QTest::mouseDClick(
			canvas, Qt::LeftButton, Qt::NoModifier, canvas->noteRect(clip->notes()[0]).center().toPoint());
		auto* lyric = canvas->findChild<QLineEdit*>("svsInlineLyric");
		QVERIFY(lyric->isVisibleTo(canvas));
		lyric->setText(QString::fromUtf8("你好"));
		QTest::keyClick(lyric, Qt::Key_Return);
		QCOMPARE(clip->notes()[0].lyric, QString::fromUtf8("你好"));
		QCOMPARE(clip->notes().size(), 1);
		QTest::mouseClick(editor.findChild<QToolButton*>("svsTool1"), Qt::LeftButton);
		canvas->beginLyric(note.id);
		QTest::keyClick(lyric, Qt::Key_3);
		QCOMPARE(canvas->tool(), gui::SVSCanvas::Tool::Pencil);
		QTest::keyClick(lyric, Qt::Key_Escape);
		auto second = clip->notes()[0];
		second.id = "batch-second";
		second.tick = 240;
		second.lyric = "a";
		clip->setNotes({second, clip->notes()[0]});
		QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
		gui::SVSLyricEditor lyrics(clip, canvas->selectedNotes());
		lyrics.findChild<QPlainTextEdit*>("svsBatchLyricText")->setPlainText(QString::fromUtf8("你好"));
		lyrics.accept();
		QCOMPARE(clip->notes()[0].lyric, QString::fromUtf8("好"));
		QCOMPARE(clip->notes()[1].lyric, QString::fromUtf8("你"));
		auto* visible = editor.findChild<QToolButton*>("svsParameterAreaVisible");
		QVERIFY(visible);
		visible->click();
		QVERIFY(!clip->editorState()["parameterAreaVisible"].toBool());
		visible->click();
		QVERIFY(clip->editorState()["parameterAreaVisible"].toBool());
	}
	void tuneLabStretchAlignmentNativeWindow()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note a;
		a.id = "stretch-a";
		a.tick = 48;
		a.duration = 48;
		a.pitch = 60;
		a.lyric = "la";
		auto b = a;
		b.id = "stretch-b";
		b.tick = 96;
		b.pitch = 63;
		auto segment = [](const char* symbol, double start, double duration, double weight) {
			return QJsonObject{{"symbol", symbol}, {"startTick", start}, {"durationTicks", duration},
				{"stretchWeight", weight}, {"fixtureExtra", "retained"}};
		};
		a.phonemes = {{"symbols", QJsonArray{"l", "a"}},
			{"segments", QJsonArray{segment("l", -12, 10, 0), segment("a", -2, 32, 1)}}};
		b.phonemes = {{"symbols", QJsonArray{"l", "a"}},
			{"segments", QJsonArray{segment("l", -18, 18, 0), segment("a", 0, 48, 1)}}};
		const QVector<svs::Note> original{a, b};
		clip->setNotes(original);
		QWidget window;
		window.setWindowTitle(QString::fromUtf8("SVS 拉伸操作对齐"));
		auto* layout = new QVBoxLayout(&window);
		auto* canvas = new gui::SVSCanvas(clip, &window);
		canvas->setTool(gui::SVSCanvas::Tool::Pencil);
		canvas->setScroll(0, 72);
		canvas->setQuantization(12);
		layout->addWidget(canvas);
		auto* strip = new gui::SVSResultStrip(clip, &window);
		strip->setQuantization(12);
		layout->addWidget(strip);
		QObject::connect(strip, &gui::SVSResultStrip::notePreviewChanged, canvas, &gui::SVSCanvas::setNotePreview);
		window.resize(700, 420);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		QTest::qWait(120);
		auto* journal = Engine::projectJournal();
		const auto journalling = journal->isJournalling();
		auto restore = qScopeGuard([&] { journal->setJournalling(journalling); });
		journal->setJournalling(true);
		clip->setJournalling(true);
		// Both notes, both edges, extension and contraction. The untouched outer
		// endpoint and the exact neighbor edit are checked against the UI evidence.
		for (int which : {0, 1})
			for (bool head : {false, true})
				for (int delta : {-12, 12})
				{
					clip->setNotes(original);
					const auto& note = original[which];
					const auto r = canvas->noteRect(note);
					const QPoint from(int(head ? r.left() + 2 : r.right() - 2), int(r.center().y()));
					const auto depth = journal->undoDepth();
					QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
					QTest::mouseMove(canvas, from + QPoint(delta * 2, 0));
					QCOMPARE(clip->notes(), original);
					QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, from + QPoint(delta * 2, 0));
					const auto changed = clip->notes();
					QCOMPARE(changed.size(), 2);
					QCOMPARE(changed[which].pitch, note.pitch);
					QCOMPARE(changed[which].tick, head ? note.tick + delta : note.tick);
					QCOMPARE(changed[which].tick + changed[which].duration,
						head ? note.tick + note.duration : note.tick + note.duration + delta);
					if (which == 0 && !head && delta > 0)
					{
						QCOMPARE(changed[1].tick, 108.);
						QCOMPARE(changed[1].tick + changed[1].duration, 144.);
					}
					else if (which == 1 && head && delta < 0)
					{
						QCOMPARE(changed[0].tick, 48.);
						QCOMPARE(changed[0].duration, 36.);
					}
					else
					{
						QCOMPARE(changed[1 - which].tick, original[1 - which].tick);
						QCOMPARE(changed[1 - which].duration, original[1 - which].duration);
						QCOMPARE(changed[1 - which].pitch, original[1 - which].pitch);
						QCOMPARE(changed[1 - which].parameters, original[1 - which].parameters);
					}
					QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
					QCOMPARE(clip->status(), QString("Ready"));
					QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 4);
					QCOMPARE(journal->undoDepth(), depth + 1);
					QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
					QCOMPARE(clip->notes(), original);
				}
		auto drag = [&](double from, double to, int y, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
			QPoint start(int(60 + from * 2), y), end(int(60 + to * 2), y);
			QTest::mousePress(strip, Qt::LeftButton, modifiers, start);
			QTest::mouseMove(strip, end);
			QCOMPARE(clip->notes(), original);
			QTest::mouseRelease(strip, Qt::LeftButton, modifiers, end);
		};
		auto get = [&](int note, int index, const char* field) {
			return clip->notes()[note].phonemes["segments"].toArray()[index].toObject()[field].toDouble();
		};
		const int lower = strip->height() - 6, upper = 6;
		clip->setNotes(original);
		drag(36, 32, lower);
		QCOMPARE(get(0, 0, "startTick"), -16.);
		QCOMPARE(get(0, 0, "durationTicks"), 14.);
		QCOMPARE(clip->notes()[0].tick, 48.);
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		drag(46, 50, lower);
		QCOMPARE(get(0, 0, "startTick"), -8.);
		QCOMPARE(get(0, 0, "durationTicks"), 10.);
		QCOMPARE(get(0, 1, "startTick"), 2.);
		QCOMPARE(get(0, 1, "durationTicks"), 28.);
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		drag(78, 74, lower);
		QCOMPARE(get(0, 1, "durationTicks"), 28.);
		QCOMPARE(get(1, 0, "startTick"), -22.);
		QCOMPARE(get(1, 0, "durationTicks"), 22.);
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		const auto depth = journal->undoDepth();
		drag(96, 100, lower);
		QCOMPARE(get(1, 0, "startTick"), -14.);
		QCOMPARE(get(1, 0, "durationTicks"), 18.);
		QCOMPARE(get(0, 1, "durationTicks"), 36.);
		QCOMPARE(get(1, 1, "startTick"), 4.);
		QCOMPARE(get(1, 1, "durationTicks"), 44.);
		QCOMPARE(clip->notes()[1].tick, 96.);
		QCOMPARE(journal->undoDepth(), depth + 1);
		QCOMPARE(clip->notes()[1].phonemes["segments"].toArray()[0].toObject()["fixtureExtra"].toString(),
			QString("retained"));
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		// Same x, different vertical half: shared note boundary changes both notes.
		drag(96, 100, upper);
		QCOMPARE(clip->notes()[0].duration, 52.);
		QCOMPARE(clip->notes()[1].tick, 100.);
		QCOMPARE(clip->notes()[1].tick + clip->notes()[1].duration, 144.);
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		drag(144, 148, lower);
		QCOMPARE(clip->notes()[1].duration, 52.);
		QCOMPARE(get(1, 1, "durationTicks"), 52.);
		QTest::keyClick(strip, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->notes(), original);
		drag(96, 101, lower, Qt::AltModifier);
		QCOMPARE(get(1, 1, "startTick"), 0.);
		QCOMPARE(clip->notes(), original); // Alt snaps back to 96, no override/checkpoint.
		QPoint point(252, lower);
		QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, point);
		QTest::mouseMove(strip, point + QPoint(8, 0));
		QTest::keyClick(strip, Qt::Key_Escape);
		QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, point + QPoint(8, 0));
		QCOMPARE(clip->notes(), original);
		QTest::qWait(120);
		if (const auto evidence = qEnvironmentVariable("LMMS_SVS_STRETCH_EVIDENCE"); !evidence.isEmpty())
		{
			QVERIFY(window.screen()->grabWindow(window.winId()).save(evidence));
		}
		window.close();
	}
	void optimizedTuneLabParameterResetSemantics()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "reset-note";
		note.duration = 384;
		clip->setNotes({note});
		const auto* declaration = track->capabilities().parameter("example.tension", "clip");
		QVERIFY(declaration);
		const auto parameter = *declaration;
		gui::SVSCanvas lane(clip);
		lane.resize(900, 200);
		lane.setParameterLane(parameter);
		lane.setTool(gui::SVSCanvas::Tool::Pencil);
		auto start = lane.curvePointAt(0, .2).toPoint(), end = lane.curvePointAt(300, .8).toPoint();
		QTest::mousePress(&lane, Qt::LeftButton, Qt::NoModifier, start);
		QTest::mouseMove(&lane, lane.curvePointAt(100, .7).toPoint());
		QTest::mouseRelease(&lane, Qt::LeftButton, Qt::NoModifier, end);
		QVERIFY(clip->curves().contains(parameter.id));
		const auto original = clip->curves();
		QVERIFY(original[parameter.id].valueAt(100)->toDouble() > .6);
		auto* journal = Engine::projectJournal();
		const bool previous = journal->isJournalling();
		auto restore = qScopeGuard([&] { journal->setJournalling(previous); });
		journal->setJournalling(true);
		clip->setJournalling(true);
		const auto depth = journal->undoDepth();
		const auto click = lane.curvePointAt(120, .9).toPoint();
		QTest::mouseClick(&lane, Qt::RightButton, Qt::NoModifier, click);
		const auto single = clip->curves();
		QCOMPARE(single[parameter.id].valueAt(120), std::optional<QJsonValue>(parameter.defaultValue));
		// Splitting a cubic re-expresses its coefficients: compare within floating
		// point error, across the whole untouched interval rather than two samples.
		for (double tick = 0; tick <= 300; tick += .5)
			if (tick != 120)
				QVERIFY(std::abs(single[parameter.id].valueAt(tick)->toDouble()
							- original[parameter.id].valueAt(tick)->toDouble())
					< 1e-10);
		QCOMPARE(journal->undoDepth(), depth + 1);
		QTest::keyClick(&lane, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->curves(), original);
		QTest::mousePress(&lane, Qt::RightButton, Qt::NoModifier, lane.curvePointAt(80, .9).toPoint());
		QTest::mouseMove(&lane, lane.curvePointAt(160, .1).toPoint());
		QTest::mouseMove(&lane, lane.curvePointAt(60, .4).toPoint());
		QCOMPARE(clip->curves(), original);
		QTest::mouseRelease(&lane, Qt::RightButton, Qt::NoModifier, lane.curvePointAt(120, .7).toPoint());
		const auto reset = clip->curves();
		for (double tick : {60., 80., 100., 120., 140., 160.})
			QCOMPARE(reset[parameter.id].valueAt(tick), std::optional<QJsonValue>(parameter.defaultValue));
		for (double tick : {20., 50., 170., 280.})
			QVERIFY(std::abs(reset[parameter.id].valueAt(tick)->toDouble()
						- original[parameter.id].valueAt(tick)->toDouble())
				< 1e-8);
		QCOMPARE(clip->notes(), QVector<svs::Note>{note});
		QCOMPARE(journal->undoDepth(), depth + 1);
		QTest::keyClick(&lane, Qt::Key_Z, Qt::ControlModifier);
		QCOMPARE(clip->curves(), original);
		QTest::keyClick(&lane, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
		QCOMPARE(clip->curves(), reset);
		QTest::mousePress(&lane, Qt::RightButton, Qt::NoModifier, lane.curvePointAt(200, .9).toPoint());
		QTest::mouseMove(&lane, lane.curvePointAt(240, .1).toPoint());
		QTest::keyClick(&lane, Qt::Key_Escape);
		QTest::mouseRelease(&lane, Qt::RightButton, Qt::NoModifier, end);
		QCOMPARE(clip->curves(), reset);
		lane.setTool(gui::SVSCanvas::Tool::Notes);
		QTest::mousePress(&lane, Qt::LeftButton, Qt::NoModifier, lane.curvePointAt(180, .3).toPoint());
		QTest::mouseRelease(&lane, Qt::LeftButton, Qt::NoModifier, lane.curvePointAt(260, .5).toPoint());
		QVERIFY(clip->curves() != reset);
		const auto before = clip->curves();
		lane.setParameterLane(parameter, true);
		QTest::mouseClick(&lane, Qt::RightButton, Qt::NoModifier, click);
		QCOMPARE(clip->curves(), before);
		QContextMenuEvent context(QContextMenuEvent::Mouse, click, lane.mapToGlobal(click));
		QApplication::sendEvent(&lane, &context);
		QVERIFY(context.isAccepted());
		QVERIFY(!QApplication::activePopupWidget());
		for (const auto& p : track->capabilities().parameters)
			if (p.id == "example.power" || p.id == "example.soft" || p.id == "example.mode")
			{
				lane.setParameterLane(p);
				QTest::mouseClick(&lane, Qt::RightButton, Qt::NoModifier, QPoint(300, 100));
				QVERIFY(clip->curves().contains(p.id));
				QCOMPARE(clip->curves()[p.id].scope, p.scope);
				QCOMPARE(clip->curves()[p.id].valueAt(120), std::optional<QJsonValue>(p.defaultValue));
			}
	}
	void automaticNoteStretchRenders()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		gui::SVSCanvas canvas(clip);
		canvas.resize(700, 400);
		canvas.setTool(gui::SVSCanvas::Tool::Pencil);
		canvas.setQuantization(12);
		canvas.show();
		QVERIFY(QTest::qWaitForWindowExposed(&canvas));
		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.pointAt(48, 60).toPoint() + QPoint(2, 6));
		QCOMPARE(clip->notes().size(), 1);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		const auto original = clip->notes();
		QCOMPARE(original[0].duration, 12.);
		QVERIFY(original[0].phonemes.isEmpty());
		for (bool head : {false, true})
		{
			clip->setNotes(original);
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
			const auto rect = canvas.noteRect(original[0]);
			const QPoint from(int(head ? rect.left() + 2 : rect.right() - 2), int(rect.center().y()));
			const auto to = from + QPoint(head ? -48 : 96, 0);
			QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, from);
			QTest::mouseMove(&canvas, to);
			QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, to);
			QVERIFY(clip->notes()[0].duration > original[0].duration);
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio() || clip->status().startsWith("Failed:"), 10000);
			QVERIFY2(clip->audio(), qPrintable(clip->status()));
			QVERIFY(clip->notes()[0].phonemes.isEmpty());
			QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 2);
		}
		canvas.close();
		gui::SVSResultStrip strip(clip);
		strip.resize(700, 36);
		strip.show();
		QVERIFY(QTest::qWaitForWindowExposed(&strip));
		for (bool head : {false, true})
		{
			clip->setNotes(original);
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
			const QPoint from(int(60 + 2 * (head ? 48 : 60)), 6);
			const auto to = from + QPoint(head ? -48 : 96, 0);
			QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, from);
			QTest::mouseMove(&strip, to);
			QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, to);
			QVERIFY(clip->notes()[0].duration > 12);
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio() || clip->status().startsWith("Failed:"), 10000);
			QVERIFY2(clip->audio(), qPrintable(clip->status()));
			QVERIFY(clip->notes()[0].phonemes.isEmpty());
			QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 2);
		}
		// A note saved by the broken build recovers on its next stretch.
		auto poisoned = original;
		poisoned[0].phonemes["segments"] = QJsonValue::Null;
		clip->setNotes(poisoned);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status().startsWith("Failed:"), 10000);
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(180, 6));
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(276, 6));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		QVERIFY(clip->notes()[0].phonemes.isEmpty());
		strip.close();
	}
	void stretchShortPhonemeRenders()
	{
		auto* song = Engine::getSong();
		const auto tempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto restore = qScopeGuard([&] { song->tempoModel().setValue(tempo); });
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(song);
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "short-consonant";
		note.tick = 48;
		note.duration = 48;
		note.lyric = "la";
		note.phonemes = {{"symbols", QJsonArray{"l", "a"}},
			{"segments",
				QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", 0}, {"durationTicks", .6}},
					QJsonObject{{"symbol", "a"}, {"startTick", .6}, {"durationTicks", 47.4}}}}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		gui::SVSCanvas canvas(clip);
		canvas.resize(700, 400);
		canvas.setTool(gui::SVSCanvas::Tool::Pencil);
		canvas.show();
		QVERIFY(QTest::qWaitForWindowExposed(&canvas));
		const auto rect = canvas.noteRect(note);
		QPoint start(int(rect.right() - 2), int(rect.center().y()));
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, start - QPoint(48, 0));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() || clip->status().startsWith("Failed:"), 10000);
		QVERIFY2(clip->audio(), qPrintable(clip->status()));
		QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 2);
		QVERIFY(clip->notes()[0].duration < 48);
		const auto adjusted = clip->notes()[0].phonemes["segments"].toArray();
		QVERIFY(adjusted[0].toObject()["durationTicks"].toDouble() + 1e-8 >= .48);
		canvas.close();
		note.phonemes["symbols"] = QJsonArray{"l", "a", "a"};
		note.phonemes["segments"] = QJsonArray{QJsonObject{{"symbol", "l"}, {"startTick", 0}, {"durationTicks", .6}},
			QJsonObject{{"symbol", "a"}, {"startTick", .6}, {"durationTicks", .6}},
			QJsonObject{{"symbol", "a"}, {"startTick", 1.2}, {"durationTicks", 46.8}}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		gui::SVSResultStrip strip(clip);
		strip.resize(700, 36);
		strip.show();
		QVERIFY(QTest::qWaitForWindowExposed(&strip));
		QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(252, 6));
		QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(156, 6));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() || clip->status().startsWith("Failed:"), 10000);
		QVERIFY2(clip->audio(), qPrintable(clip->status()));
		QVERIFY(clip->notes()[0].duration + 1e-7 >= 1.44);
		QVERIFY(clip->notes()[0].duration < 2);
		QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 3);
		strip.close();
	}
	void generatedPhonemeDragRenders()
	{
		auto* song = Engine::getSong();
		const auto tempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto restore = qScopeGuard([&] { song->tempoModel().setValue(tempo); });
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(song);
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note a;
		a.id = "generated-a";
		a.tick = 60;
		a.duration = 32;
		a.pitch = 58;
		a.lyric = "la";
		auto b = a;
		b.id = "generated-b";
		b.tick = 92;
		b.duration = 52;
		b.pitch = 60;
		{
			gui::SVSCanvas canvas(clip);
			canvas.resize(900, 400);
			canvas.setTool(gui::SVSCanvas::Tool::Pencil);
			canvas.show();
			QVERIFY(QTest::qWaitForWindowExposed(&canvas));
			for (const auto& note : {a, b})
			{
				QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier,
					canvas.pointAt(note.tick, note.pitch).toPoint() + QPoint(0, 6));
				QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier,
					canvas.pointAt(note.tick + note.duration, note.pitch).toPoint() + QPoint(0, 6));
				QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
				QCOMPARE(clip->status(), QString("Ready"));
			}
			QCOMPARE(clip->notes().size(), 2);
			canvas.close();
		}
		clip->setNotes({a, b});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 4);
		gui::SVSResultStrip strip(clip);
		strip.resize(900, 36);
		strip.show();
		QVERIFY(QTest::qWaitForWindowExposed(&strip));
		for (const auto& note : {a, b})
		{
			const auto junction = note.tick + note.duration / 2;
			QPoint from(int(60 + 2 * junction), 28);
			QTest::mousePress(&strip, Qt::LeftButton, Qt::NoModifier, from);
			QTest::mouseRelease(&strip, Qt::LeftButton, Qt::NoModifier, from + QPoint(8, 0));
			QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
			QCOMPARE(clip->status(), QString("Ready"));
			QCOMPARE(clip->audio()->feedback["phonemes"].toArray().size(), 4);
		}
		const auto notes = clip->notes();
		QCOMPARE(notes[0].tick, a.tick);
		QCOMPARE(notes[0].duration, a.duration);
		QCOMPARE(notes[1].tick, b.tick);
		QCOMPARE(notes[1].duration, b.duration);
		QVERIFY(strip.setSelectedParameter("example.phonemeGain", .5));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		strip.close();
	}
	void readOnlyReferenceNativeWindow()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "reference-note";
		note.duration = 192;
		note.lyric = "la";
		clip->setNotes({note});
		clip->synthesize();
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 10000);
		const auto audio = clip->audio();
		const auto notes = clip->notes();
		const auto curves = clip->curves();
		const svs::Parameter* parameter = nullptr;
		for (const auto& item : track->capabilities().feedbackParameters)
			if (item.id == "example.level")
				parameter = &item;
		QVERIFY(parameter);
		QVERIFY(!parameter->writable);
		svs::Curve level;
		QString error;
		QVERIFY2(svs::Curve::fromJson(
					 audio->feedback["curves"].toObject()["example.level"].toObject(), level, error, parameter),
			qPrintable(error));
		QVERIFY(level.evaluator.points.size() > 2);
		bool nonzero = false;
		for (const auto& point : level.evaluator.points)
		{
			QVERIFY(point.value >= 0 && point.value <= 1);
			nonzero |= point.value > 0;
		}
		QVERIFY(nonzero);
		gui::SVSPianoRoll editor(clip);
		editor.setWindowFlag(Qt::WindowStaysOnTopHint);
		editor.resize(1200, 850);
		editor.show();
		editor.raise();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		QTest::qWait(150);
		QVERIFY(!editor.findChild<QLabel*>("svsReadOnlyLabel"));
		QVERIFY(editor.findChild<QFrame*>("svsReadOnlyDivider")->isVisible());
		QVERIFY(!editor.findChild<QToolButton*>("svsReferenceVisible"));
		auto* tab = editor.findChild<QToolButton*>("svsParameterTab.feedback:example.level");
		QVERIFY(tab);
		QTest::mouseClick(tab, Qt::LeftButton);
		auto* lane = editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.level.feedback");
		QVERIFY(lane);
		auto* peakTab = editor.findChild<QToolButton*>("svsParameterTab.feedback:example.peak");
		QVERIFY(peakTab);
		const auto peakPoints = audio->feedback["curves"].toObject()["example.peak"].toObject()["points"].toArray();
		QVERIFY(peakPoints.size() > 2);
		QVERIFY(peakPoints[1].toObject()["value"].toDouble() > 0);
		QTest::mouseClick(peakTab, Qt::RightButton);
		QVERIFY(!clip->editorState()["lanes"].toObject()["feedback:example.peak"].toObject()["visible"].toBool());
		QVERIFY(clip->editorState()["lanes"].toObject()["feedback:example.level"].toObject()["visible"].toBool());
		QTest::mouseClick(peakTab, Qt::RightButton);
		auto capture = [&] {
			QTest::qWait(100);
			return editor.screen()->grabWindow(
				0, editor.mapToGlobal(QPoint()).x(), editor.mapToGlobal(QPoint()).y(), editor.width(), editor.height());
		};
		const auto on = capture();
		QTest::mouseClick(tab, Qt::RightButton);
		QVERIFY(!clip->editorState()["lanes"].toObject()["feedback:example.level"].toObject()["visible"].toBool());
		QVERIFY(clip->editorState()["lanes"].toObject()["feedback:example.energy"].toObject()["visible"].toBool(true));
		const auto off = capture();
		QVERIFY(on.toImage() != off.toImage());
		QTest::mouseClick(peakTab, Qt::RightButton);
		const auto bothOff = capture();
		QVERIFY(off.toImage() != bothOff.toImage());
		QTest::mouseClick(peakTab, Qt::RightButton);
		QTest::mouseClick(lane, Qt::LeftButton, Qt::NoModifier, lane->curvePointAt(60, .7).toPoint());
		QTest::mouseClick(lane, Qt::RightButton, Qt::NoModifier, lane->curvePointAt(60, .7).toPoint());
		QCOMPARE(clip->curves(), curves);
		QCOMPARE(clip->notes(), notes);
		QCOMPARE(clip->audio(), audio);
		{
			gui::SVSPianoRoll restored(clip);
			QVERIFY(!clip->editorState()["lanes"].toObject()["feedback:example.level"].toObject()["visible"].toBool());
		}
		QTest::mouseClick(tab, Qt::RightButton);
		QVERIFY(clip->editorState()["lanes"].toObject()["feedback:example.level"].toObject()["visible"].toBool());
		QCOMPARE(clip->audio(), audio);
		const auto evidence = qEnvironmentVariable("LMMS_SVS_REFERENCE_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QVERIFY(on.save(evidence + "-on.png"));
			QVERIFY(off.save(evidence + "-off.png"));
		}
		editor.close();
	}
	void parameterLaneDrawingAndPersistence()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "lane-note";
		clip->setNotes({note});
		gui::SVSPianoRoll editor(clip);
		QFile theme("data/themes/default/style.css");
		QVERIFY(theme.open(QIODevice::ReadOnly));
		editor.setStyleSheet(QString::fromUtf8(theme.readAll()));
		editor.resize(1200, 850);
		editor.show();
		QCoreApplication::processEvents();
		auto* main = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(main);
		auto* tabStrip = editor.findChild<QScrollArea*>("svsParameterTabs");
		QVERIFY(tabStrip);
		QCOMPARE(tabStrip->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
		QCOMPARE(editor.findChildren<gui::SVSCanvas*>().size(), 2);
		QVERIFY(editor.findChildren<QSpinBox*>(QRegularExpression("svsParameterHeight.*")).isEmpty());
		auto select = [&](const QString& key) -> gui::SVSCanvas* {
			auto* tab = editor.findChild<QToolButton*>("svsParameterTab." + key);
			if (!tab)
				return nullptr;
			QTest::mouseClick(tab, Qt::LeftButton);
			return editor.findChild<gui::SVSCanvas*>("svsParameterLane." + key.mid(key.indexOf(':') + 1)
				+ (key.startsWith("feedback:") ? ".feedback" : ".input"));
		};
		auto* tension = select("input:example.tension");
		QVERIFY(tension);
		tension->resize(900, 160);
		tension->setTool(gui::SVSCanvas::Tool::Line);
		QVERIFY(tabStrip->mapTo(&editor, QPoint()).y() >= tension->mapTo(&editor, QPoint()).y() + tension->height());
		QTest::mousePress(tension, Qt::LeftButton, Qt::NoModifier, tension->curvePointAt(0, .2).toPoint());
		QVERIFY(clip->curves().isEmpty());
		QTest::mouseRelease(tension, Qt::LeftButton, Qt::NoModifier, tension->curvePointAt(300.25, .8).toPoint());
		QVERIFY(clip->curves().contains("example.tension"));
		auto value = clip->curves()["example.tension"].valueAt(150);
		QVERIFY(value);
		QVERIFY(std::abs(value->toDouble() - .5) < .02);
		auto* soft = select("input:example.soft");
		QCOMPARE(soft, tension);
		soft->resize(900, 160);
		soft->setTool(gui::SVSCanvas::Tool::Freehand);
		QTest::mousePress(soft, Qt::LeftButton, Qt::NoModifier, soft->curvePointAt(0, 0).toPoint());
		QTest::mouseRelease(soft, Qt::LeftButton, Qt::NoModifier, soft->curvePointAt(300, 1).toPoint());
		QCOMPARE(clip->curves()["example.soft"].type, QString("bool"));
		QCOMPARE(clip->curves()["example.soft"].valueAt(150)->toBool(), false);
		QCOMPARE(clip->curves()["example.soft"].valueAt(300)->toBool(), true);
		auto* mode = select("input:example.mode");
		QCOMPARE(mode, tension);
		mode->resize(900, 160);
		mode->setTool(gui::SVSCanvas::Tool::Line);
		QTest::mousePress(mode, Qt::LeftButton, Qt::NoModifier, mode->curvePointAt(0, 0).toPoint());
		QTest::mouseRelease(mode, Qt::LeftButton, Qt::NoModifier, mode->curvePointAt(300, 1).toPoint());
		QCOMPARE(clip->curves()["example.mode"].valueAt(150)->toString(), QString("basic"));
		QCOMPARE(clip->curves()["example.mode"].valueAt(300)->toString(), QString("advanced"));
		auto* result = select("feedback:example.energy");
		QCOMPARE(result, tension);
		const auto before = clip->curves();
		result->setTool(gui::SVSCanvas::Tool::Freehand);
		QTest::mousePress(result, Qt::LeftButton, Qt::NoModifier, QPoint(100, 50));
		QTest::mouseRelease(result, Qt::LeftButton, Qt::NoModifier, QPoint(200, 70));
		QCOMPARE(clip->curves(), before);
		const auto topPitch = main->topPitch();
		main->setScroll(84, topPitch);
		QCOMPARE(tension->scrollTick(), 84.);
		tension->setZoom(1.5, 1);
		QCOMPARE(main->horizontalZoom(), 1.5);
		QCOMPARE(main->topPitch(), topPitch);
		auto* tab = editor.findChild<QToolButton*>("svsParameterTab.input:example.tension");
		QVERIFY(tab);
		QTest::mouseClick(tab, Qt::RightButton);
		QCOMPARE(
			clip->editorState()["lanes"].toObject()["input:example.tension"].toObject()["visible"].toBool(), false);
		QTest::mouseClick(tab, Qt::LeftButton);
		QVERIFY(tab->isChecked());
		QCOMPARE(clip->editorState()["selectedParameter"].toString(), QString("input:example.tension"));
		QTest::mouseClick(tab, Qt::RightButton);
		QVERIFY(tab->isChecked());
		QTest::mouseClick(tab, Qt::LeftButton);
		QVERIFY(tab->isChecked());
		QVERIFY(clip->editorState()["lanes"].toObject()["input:example.tension"].toObject()["visible"].toBool());
		// Plugin RGB reaches both active curve and overlay pixels, with each own range.
		// Isolate this overlay: several discrete parameters share the zero baseline
		// and antialiased strokes legitimately blend with each other there.
		const auto* softDescriptor = track->capabilities().parameter("example.soft", "note");
		QVERIFY(softDescriptor);
		tension->setParameterOverlays({{*softDescriptor, false}});
		tension->resize(900, 160);
		tension->setScroll(0, 72);
		tension->setTool(gui::SVSCanvas::Tool::Line);
		const auto image = tension->grab().toImage();
		const auto background
			= editor.backgroundColor().isValid() ? editor.backgroundColor() : tension->palette().base().color();
		auto nearColor = [&](QPoint point, QColor expected) {
			const double dr = expected.red() - background.red(), dg = expected.green() - background.green(),
						 db = expected.blue() - background.blue(), length = dr * dr + dg * dg + db * db;
			for (int y = point.y() - 2; y <= point.y() + 2; ++y)
				for (int x = point.x() - 2; x <= point.x() + 2; ++x)
					if (image.rect().contains(x, y))
					{
						const auto actual = image.pixelColor(x, y);
						const auto coverage
							= ((actual.red() - background.red()) * dr + (actual.green() - background.green()) * dg
								  + (actual.blue() - background.blue()) * db)
							/ length;
						// A one-pixel antialiased overlay may blend with the background; its RGB
						// must still be the declared color at a common coverage in all channels.
						if (coverage >= .3 && coverage <= 1.01
							&& std::abs(actual.red() - background.red() - coverage * dr) < 3
							&& std::abs(actual.green() - background.green() - coverage * dg) < 3
							&& std::abs(actual.blue() - background.blue() - coverage * db) < 3)
							return true;
					}
			return false;
		};
		QVERIFY(nearColor(tension->curvePointAt(150, .5).toPoint(), QColor("#AFD867")));
		QVERIFY(nearColor(tension->curvePointAt(100, 0).toPoint(), QColor("#83B9EB")));
		auto* gender = select("input:example.gender");
		QVERIFY(gender);
		QCOMPARE(gender->curvePointAt(10, -1).y(),
			gender->curvePointAt(10, 1).y() + gender->height() - 30.); // 24px ruler + 6px inset
		track->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(tab->isHidden());
		QCOMPARE(clip->curves(), before);
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(!tab->isHidden());
		QCOMPARE(editor.findChildren<gui::SVSCanvas*>().size(), 2);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		auto* saved = static_cast<SVSClip*>(restored->getClip(0));
		QCOMPARE(saved->editorState(), clip->editorState());
		QCOMPARE(saved->curves(), before);
		QTRY_VERIFY_WITH_TIMEOUT(restored->capabilitiesReady(), 10000);
		gui::SVSPianoRoll reopened(saved);
		auto* restoredTab = reopened.findChild<QToolButton*>("svsParameterTab.input:example.gender");
		QVERIFY(restoredTab);
		QVERIFY(restoredTab->isChecked());
		delete restored;
		delete track;
	}
	void noteWaveformAndViewportZoom()
	{
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		svs::Note note;
		note.id = "wave-note";
		note.tick = 48;
		note.duration = 96;
		note.pitch = 60;
		note.lyric = "la";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QVERIFY(!clip->audio()->samples.empty());
		gui::SVSPianoRoll editor(clip);
		editor.resize(1200, 740);
		editor.show();
		QCoreApplication::processEvents();
		auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		gui::SVSCanvas* parameters = nullptr;
		for (auto* area : editor.findChildren<gui::SVSCanvas*>())
			if (area->isParameterLane())
				parameters = area;
		QVERIFY(canvas);
		QVERIFY(parameters);
		canvas->setScroll(0, 64);
		const QColor waveColor("#ff00fe");
		canvas->setThemeColors({{"waveformColor", waveColor}});
		auto checkWave = [&] {
			QCoreApplication::processEvents();
			const auto image = canvas->grab().toImage();
			const auto bounds = canvas->noteRect(note);
			const int top = int(bounds.bottom() + 2),
					  bottom = std::min(image.height(), int(top + std::clamp(bounds.height() * .75, 6., 24.)));
			int inside = 0, outside = 0;
			for (int y = top; y < bottom; ++y)
				for (int x = 60; x < image.width(); ++x)
					if (image.pixelColor(x, y) == waveColor)
					{
						if (x >= std::ceil(bounds.left()) && x < std::ceil(bounds.right()))
							++inside;
						else
							++outside;
					}
			QVERIFY(inside > 0);
			QCOMPARE(outside, 0);
		};
		checkWave();
		canvas->setZoom(.75, 2);
		canvas->setScroll(24, 64);
		checkWave();
		auto wheel = [&](QPointF point, int delta) {
			QWheelEvent event(point, canvas->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, delta), Qt::NoButton,
				Qt::NoModifier, Qt::NoScrollPhase, false);
			QCoreApplication::sendEvent(canvas, &event);
			QVERIFY(event.isAccepted());
		};
		const QPointF key(30, 110);
		const auto pitch = canvas->pitchAt(key.y()), oldHeight = canvas->verticalZoom(),
				   oldHorizontal = canvas->horizontalZoom(), oldTick = canvas->scrollTick();
		wheel(key, 120);
		QVERIFY(canvas->verticalZoom() > oldHeight);
		QCOMPARE(canvas->horizontalZoom(), oldHorizontal);
		QCOMPARE(canvas->scrollTick(), oldTick);
		QVERIFY(std::abs(canvas->pitchAt(key.y()) - pitch) < 1e-8);
		wheel(key, -120);
		QVERIFY(std::abs(canvas->verticalZoom() - oldHeight) < 1e-8);
		const QPointF ruler(250, 12);
		const auto tick = canvas->tickAt(ruler.x());
		wheel(ruler, 120);
		QVERIFY(canvas->horizontalZoom() > oldHorizontal);
		QVERIFY(std::abs(canvas->tickAt(ruler.x()) - tick) < 1e-8);
		QCOMPARE(parameters->horizontalZoom(), canvas->horizontalZoom());
		QCOMPARE(parameters->scrollTick(), canvas->scrollTick());
		auto* bars = editor.findChild<QComboBox*>("svsBarZoom");
		QVERIFY(bars);
		for (int count : {1, 4, 16})
		{
			const int index = bars->findData(count);
			QVERIFY(index > 0);
			bars->setCurrentIndex(index);
			QVERIFY(QMetaObject::invokeMethod(bars, "activated", Q_ARG(int, index)));
			QVERIFY(std::abs(canvas->tickAt(canvas->width()) - canvas->scrollTick() - count * TimePos::ticksPerBar())
				< 1e-8);
			QCOMPARE(parameters->horizontalZoom(), canvas->horizontalZoom());
			QCOMPARE(bars->currentIndex(), index);
		}
		editor.resize(1500, 740);
		QCoreApplication::processEvents();
		QCOMPARE(bars->currentIndex(), 0);
		const auto state = clip->editorState();
		QCOMPARE(state["horizontalZoom"].toDouble(), canvas->horizontalZoom());
		QCOMPARE(state["verticalZoom"].toDouble(), canvas->verticalZoom());
		gui::SVSCanvas restored(clip);
		QCOMPARE(restored.horizontalZoom(), canvas->horizontalZoom());
		QCOMPARE(restored.verticalZoom(), canvas->verticalZoom());
		QCOMPARE(restored.scrollTick(), canvas->scrollTick());
	}
	void globalSidebarVoiceAndRelativeValues()
	{
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		gui::SVSPianoRoll editor(clip);
		editor.resize(1200, 740);
		editor.show();
		QCoreApplication::processEvents();
		auto* sidebar = editor.findChild<QScrollArea*>("svsVoicePanel");
		auto* singer = editor.findChild<QComboBox*>("svsSinger");
		QVERIFY(sidebar);
		QVERIFY(singer);
		QVERIFY(singer->isVisible());
		QCOMPARE(singer->currentIndex(), 0);
		QVERIFY(sidebar->findChildren<QSlider*>().isEmpty());
		auto* settings = editor.findChild<QDialog*>("svsEditorSettings");
		QVERIFY(settings);
		QVERIFY(!settings->isVisible());
		QVERIFY(!editor.findChild<QCheckBox*>("svsPortraitVisible")->isVisible());
		const auto voice = svs::Registry::instance().voices().first();
		const int full = singer->findData(voice.pluginId + "\nfull");
		QVERIFY(full > 0);
		QVERIFY(QMetaObject::invokeMethod(singer, "activated", Q_ARG(int, full)));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->voiceId(), QString("full"));
		QCOMPARE(singer->currentIndex(), full);
		auto* tension = editor.findChild<QDoubleSpinBox*>("svsGlobalValue.clip.example.tension");
		auto* slider = editor.findChild<QSlider*>("svsGlobalSlider.clip.example.tension");
		QVERIFY(tension);
		QVERIFY(slider);
		QVERIFY(slider->isVisible());
		QCOMPARE(tension->value(), 0.);
		QCOMPARE(tension->minimum(), -.25);
		QCOMPARE(tension->maximum(), .75);
		svs::Note note;
		note.id = "global-note";
		note.duration = 96;
		note.lyric = "la";
		svs::Curve curve;
		curve.id = "example.tension";
		curve.type = "float";
		curve.interpolation = "linear";
		curve.evaluator.interpolation = svs_sdk::Interpolation::Linear;
		curve.insert(0, .2);
		curve.insert(96, .5);
		clip->setEditorData({note}, {{curve.id, curve}});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto original = clip->curves(), baseline = original;
		const auto before = clip->audio();
		slider->setValue(4500);
		QCOMPARE(clip->parameters()[curve.id].toDouble(), .45);
		QCOMPARE(tension->value(), .2);
		QCOMPARE(clip->curves(), original);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto shifted = clip->audio();
		QVERIFY(shifted->samples != before->samples);
		auto shiftedCurve = curve;
		for (auto& point : shiftedCurve.evaluator.points)
			point.value += .2;
		tension->setValue(0);
		clip->setEditorData({note}, {{curve.id, shiftedCurve}});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QCOMPARE(clip->audio()->samples.size(), shifted->samples.size());
		double error = 0;
		for (size_t i = 0; i < shifted->samples.size(); ++i)
			error = std::max(error, std::abs(double(shifted->samples[i] - clip->audio()->samples[i])));
		QVERIFY(error < 1e-6);
		clip->setEditorData({note}, original);
		tension->setValue(.2);
		QCOMPARE(clip->curves(), baseline);
		const int minimal = singer->findData(voice.pluginId + "\nminimal");
		QVERIFY(minimal > 0);
		QVERIFY(QMetaObject::invokeMethod(singer, "activated", Q_ARG(int, minimal)));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(slider->isHidden() || !slider->isVisible());
		QCOMPARE(clip->parameters()[curve.id].toDouble(), .45);
		QVERIFY(QMetaObject::invokeMethod(singer, "activated", Q_ARG(int, full)));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(slider->isVisible());
		QCOMPARE(tension->value(), .2);
		QCOMPARE(editor.findChild<QSlider*>("svsGlobalSlider.clip.example.tension"), slider);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		auto restoreCleanup = qScopeGuard([&] { delete restored; });
		QTRY_VERIFY_WITH_TIMEOUT(restored->capabilitiesReady(), 10000);
		auto* saved = static_cast<SVSClip*>(restored->getClip(0));
		gui::SVSPianoRoll reopened(saved);
		QCOMPARE(saved->curves(), original);
		QCOMPARE(reopened.findChild<QDoubleSpinBox*>("svsGlobalValue.clip.example.tension")->value(), .2);
		auto* button = editor.findChild<QToolButton*>("svsEditorSettingsButton");
		QVERIFY(button);
		QTest::mouseClick(button, Qt::LeftButton);
		QVERIFY(settings->isVisible());
		settings->close();
	}
	void globalParameterCurveAndScrollSynchronization()
	{
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "global-power";
		note.duration = 96;
		svs::Curve curve;
		curve.id = "example.power";
		curve.scope = "note";
		curve.type = "int";
		curve.interpolation = "step";
		curve.evaluator.interpolation = svs_sdk::Interpolation::Step;
		curve.insert(0, 80);
		curve.insert(96, 120);
		clip->setEditorData({note}, {{curve.id, curve}});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto original = clip->curves();
		const auto audio = clip->audio();
		gui::SVSPianoRoll editor(clip);
		editor.resize(1200, 740);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		auto* tabs = editor.findChild<QScrollArea*>("svsParameterTabs");
		QVERIFY(tabs);
		auto* divider = editor.findChild<QFrame*>("svsReadOnlyDivider");
		QVERIFY(divider);
		QVERIFY(divider->isVisible());
		for (const auto& p : track->capabilities().parameters)
			if (svs::globalParameter(p) && p.isVisible(track->parameters()))
			{
				QVERIFY(editor.findChild<QWidget*>("svsGlobalValue." + p.scope + "." + p.id));
				QVERIFY(editor.findChild<QToolButton*>("svsParameterTab.input:" + p.id));
			}
		for (auto* tab : tabs->findChildren<QToolButton*>())
			if (tab->isVisible())
			{
				const int x = tab->mapTo(tabs->widget(), QPoint()).x();
				if (tab->objectName().contains("feedback:"))
					QVERIFY(x > divider->x());
				else
					QVERIFY(x < divider->x());
			}
		auto* power = editor.findChild<QDoubleSpinBox*>("svsGlobalValue.note.example.power");
		auto* slider = editor.findChild<QSlider*>("svsGlobalSlider.note.example.power");
		auto* soft = editor.findChild<QCheckBox*>("svsGlobalValue.note.example.soft");
		auto* mode = editor.findChild<QComboBox*>("svsGlobalValue.track.example.mode");
		QVERIFY(power);
		QVERIFY(slider);
		QVERIFY(soft);
		QVERIFY(mode);
		auto* tab = editor.findChild<QToolButton*>("svsParameterTab.input:example.power");
		QTest::mouseClick(tab, Qt::LeftButton);
		auto* lane = editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.power.input");
		QVERIFY(lane);
		const double y = lane->curvePointAt(48, 100).y();
		power->setValue(20);
		QVERIFY(lane->curvePointAt(48, 100).y() < y);
		QCOMPARE(clip->curves(), original);
		QCOMPARE(clip->notes()[0].parameters, note.parameters);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto shifted = clip->audio();
		QVERIFY(shifted->samples != audio->samples);
		power->setValue(0);
		auto shiftedCurve = curve;
		for (auto& point : shiftedCurve.evaluator.points)
			point.value += 20;
		clip->setEditorData({note}, {{curve.id, shiftedCurve}});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QCOMPARE(clip->audio()->samples, shifted->samples);
		clip->setEditorData({note}, original);
		power->setValue(20);
		lane->setTool(gui::SVSCanvas::Tool::Anchor);
		QTest::mouseDClick(lane, Qt::LeftButton, Qt::NoModifier, lane->curvePointAt(48, 100).toPoint());
		QVERIFY(clip->curves()[curve.id].valueAt(48).has_value());
		QVERIFY(std::abs(clip->curves()[curve.id].valueAt(48)->toDouble() - 100.)
			<= std::max(1., 200. / (lane->height() - 30.)));
		clip->setEditorData({note}, original);
		auto* journal = Engine::projectJournal();
		const bool previous = journal->isJournalling();
		journal->setJournalling(true);
		clip->setJournalling(true);
		auto restoreJournal = qScopeGuard([&] { journal->setJournalling(previous); });
		const auto depth = journal->undoDepth();
		slider->setSliderDown(true);
		slider->setValue(6500);
		slider->setValue(7000);
		slider->setSliderDown(false);
		QCOMPARE(journal->undoDepth(), depth + 1);
		QCOMPARE(power->value(), 40.);
		journal->undo();
		QCOMPARE(power->value(), 20.);
		journal->redo();
		QCOMPARE(power->value(), 40.);
		QVERIFY(clip->isJournalling());
		clip->setEditorData({note}, {});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		const auto loud = clip->audio();
		QTest::mouseClick(soft, Qt::LeftButton);
		QCOMPARE(clip->globalParameters()["example.soft"].toBool(), true);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QVERIFY(clip->audio()->samples != loud->samples);
		const auto index = mode->findData("advanced");
		QVERIFY(index >= 0);
		QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, index)));
		QTRY_VERIFY_WITH_TIMEOUT(editor.findChild<QSlider*>("svsGlobalSlider.clip.example.breath") != nullptr, 10000);
		note.id = "new-global-note";
		note.tick = 96;
		auto notes = clip->notes();
		notes.append(note);
		clip->setNotes(notes);
		QCOMPARE(clip->notes()[1].parameters, note.parameters);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QDomDocument document;
		auto root = document.createElement("test");
		document.appendChild(root);
		track->saveState(document, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		auto restoredCleanup = qScopeGuard([&] { delete restored; });
		QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->globalParameters(), clip->globalParameters());
		tabs->setFixedWidth(280);
		QCoreApplication::processEvents();
		QVERIFY(tabs->horizontalScrollBar()->maximum() > 0);
		tabs->horizontalScrollBar()->setValue(tabs->horizontalScrollBar()->maximum());
		QCoreApplication::processEvents();
		QCOMPARE(tabs->horizontalScrollBar()->value(), tabs->horizontalScrollBar()->maximum());
	}
	void nativePluginVoiceAndImageSettings()
	{
		QTemporaryDir images;
		QVERIFY(images.isValid());
		QImage image(80, 160, QImage::Format_ARGB32);
		image.fill(Qt::red);
		const auto path = images.filePath("fixture.png");
		QVERIFY(image.save(path));
		auto* config = ConfigManager::inst();
		const auto oldAvatar = config->value("svs", "testAvatarPath"),
				   oldPortrait = config->value("svs", "testPortraitPath");
		config->setValue("svs", "testAvatarPath", path);
		config->setValue("svs", "testPortraitPath", path);
		auto restore = qScopeGuard([&] {
			config->setValue("svs", "testAvatarPath", oldAvatar);
			config->setValue("svs", "testPortraitPath", oldPortrait);
		});
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->avatarPath(), path);
		QCOMPARE(track->portraitPath(), path);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		gui::SVSPianoRoll editor(clip);
		auto* loader = static_cast<gui::SVSImageLoader*>(editor.findChild<QObject*>("svsPortraitLoader"));
		QVERIFY(loader);
		QTRY_VERIFY_WITH_TIMEOUT(!loader->image().isNull(), 5000);
		auto* dialog = gui::createSVSPluginSettings(track, &editor);
		dialog->show();
		QVERIFY(QTest::qWaitForWindowExposed(dialog));
		auto* avatar = dialog->findChild<QLineEdit*>("svsPluginAvatarPath");
		auto* portrait = dialog->findChild<QLineEdit*>("svsPluginPortraitPath");
		auto* transparency = dialog->findChild<QSpinBox*>("svsPluginPortraitTransparency");
		auto* speaker = dialog->findChild<QComboBox*>("svsPluginSpeaker");
		QVERIFY(avatar);
		QVERIFY(portrait);
		QVERIFY(transparency);
		QVERIFY(speaker);
		QCOMPARE(avatar->text(), path);
		QCOMPARE(portrait->text(), path);
		transparency->setValue(50);
		QCOMPARE(track->portraitSettings()["transparency"].toInt(), 50);
		portrait->setText(images.filePath("missing.png"));
		QVERIFY(QMetaObject::invokeMethod(portrait, "editingFinished"));
		QTRY_VERIFY_WITH_TIMEOUT(!loader->diagnostic().isEmpty(), 5000);
		portrait->setText(path);
		QVERIFY(QMetaObject::invokeMethod(portrait, "editingFinished"));
		QTRY_VERIFY_WITH_TIMEOUT(!loader->image().isNull(), 5000);
		track->setName("Temporary");
		const int index = speaker->findData(voice.pluginId + "\nminimal");
		QVERIFY(index > 0);
		QVERIFY(QMetaObject::invokeMethod(speaker, "activated", Q_ARG(int, index)));
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->name(), track->voice().name);
		QCOMPARE(track->portraitPath(), path);
		QDomDocument document;
		auto root = document.createElement("test");
		document.appendChild(root);
		track->saveState(document, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		auto restoredCleanup = qScopeGuard([&] { delete restored; });
		QCOMPARE(restored->avatarPath(), path);
		QCOMPARE(restored->portraitPath(), path);
		QCOMPARE(restored->portraitSettings()["transparency"].toInt(), 50);
		dialog->close();
	}
	void compactEditorLayoutAndNoteLabels()
	{
		auto* config = ConfigManager::inst();
		const auto previous = config->value("ui", "printnotelabels");
		auto restore = qScopeGuard([&] { config->setValue("ui", "printnotelabels", previous); });
		config->setValue("ui", "printnotelabels", "0");
		const auto voice = svs::Registry::instance().voices().first();
		auto* track = new SVSTrack(Engine::getSong());
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "compact-note";
		note.pitch = 62;
		note.duration = 96;
		note.lyric = "la";
		clip->setNotes({note});
		gui::SVSPianoRoll editor(clip);
		editor.resize(1500, 900);
		editor.show();
		QCoreApplication::processEvents();
		auto* strip = editor.findChild<gui::SVSResultStrip*>();
		QVERIFY(strip);
		QCOMPARE(strip->height(), 36);
		auto* tabs = editor.findChild<QScrollArea*>("svsParameterTabs");
		QVERIFY(tabs);
		auto verifyTags = [&] {
			QRect bounds;
			for (auto* tab : tabs->findChildren<QToolButton*>())
				if (tab->isVisible())
				{
					QVERIFY(tab->icon().isNull());
					QCOMPARE(tab->toolButtonStyle(), Qt::ToolButtonTextOnly);
					bounds = bounds.united(QRect(tab->mapTo(tabs->widget(), QPoint()), tab->size()));
				}
			QVERIFY(!bounds.isEmpty());
			QVERIFY(std::abs(bounds.center().x() - tabs->widget()->rect().center().x()) <= 2);
		};
		verifyTags();
		editor.resize(1200, 740);
		QCoreApplication::processEvents();
		QCOMPARE(strip->height(), 36);
		verifyTags();
		auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas);
		canvas->setScroll(0, 65);
		QCoreApplication::processEvents();
		const auto originalNotes = clip->notes();
		const auto before = canvas->grab().toImage();
		config->setValue("ui", "printnotelabels", "1");
		QCoreApplication::processEvents();
		const auto labeled = canvas->grab().toImage();
		const auto keyTop = int(canvas->pointAt(0, 62).y());
		QVERIFY(before.copy(QRect(0, keyTop, 60, 24)) != labeled.copy(QRect(0, keyTop, 60, 24)));
		const auto noteRect = canvas->noteRect(note).toRect();
		QVERIFY(before.copy(noteRect) != labeled.copy(noteRect));
		QCOMPARE(clip->notes(), originalNotes);
		config->setValue("ui", "printnotelabels", "0");
		QCoreApplication::processEvents();
		QCOMPARE(canvas->grab().toImage().copy(noteRect), before.copy(noteRect));
		delete track;
	}
	void asynchronousVoicebankScanning()
	{
		const auto fixture = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (fixture.isEmpty())
			QSKIP("Explicit DiffSinger voicebank fixture required");
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		QString error;
		const QJsonObject empty{{"diffsinger.voicebankDirectories", QJsonArray{m_configuration.path()}}},
			populated{{"diffsinger.voicebankDirectories", QJsonArray{fixture}}};
		QVERIFY2(registry.refreshCatalog(id, empty, error), qPrintable(error));
		gui::SVSSettingsPage page;
		page.resize(1000, 750);
		auto* tabs = page.findChild<QTabWidget*>("svsEngineTabs");
		auto* body = page.findChild<QWidget*>("svsEnginePage." + id);
		QVERIFY(tabs);
		QVERIFY(body);
		tabs->setCurrentWidget(body);
		page.show();
		QVERIFY(QTest::qWaitForWindowExposed(&page));
		auto* rescan = body->findChild<QPushButton*>("svsRescanVoicebanks");
		auto* status = body->findChild<QLabel*>("svsEngineStatus");
		QVERIFY(rescan);
		QVERIFY(status);
		QSignalSpy finished(&registry, &svs::Registry::catalogScanFinished);
		int heartbeats = 0;
		QTimer timer;
		timer.setInterval(1);
		connect(&timer, &QTimer::timeout, this, [&] { ++heartbeats; });
		timer.start();
		bool onMainThread = true;
		auto connection = connect(
			&registry, &svs::Registry::catalogChanged, this,
			[&] { onMainThread &= QThread::currentThread() == qApp->thread(); }, Qt::DirectConnection);
		auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
		QElapsedTimer elapsed;
		elapsed.start();
		registry.refreshCatalogAsync(id, populated);
		QVERIFY2(elapsed.elapsed() < 200, "Submitting a scan must not wait for voicebank traversal");
		QVERIFY(registry.scanning(id));
		QVERIFY(!rescan->isEnabled());
		QVERIFY(status->text().contains("Scanning"));
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QVERIFY(heartbeats > 0);
		QVERIFY(onMainThread);
		QVERIFY(rescan->isEnabled());
		QCOMPARE(finished.size(), 1);
		QVERIFY(finished[0][1].toString().isEmpty());
		auto count = [&] {
			int result = 0;
			for (const auto& voice : registry.voices())
				if (voice.pluginId == id)
					++result;
			return result;
		};
		QCOMPARE(count(), 6);
		finished.clear();
		registry.refreshCatalogAsync(id, populated);
		registry.refreshCatalogAsync(id, empty);
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QCOMPARE(count(), 0);
		QCOMPARE(finished.size(), 1);
		registry.refreshCatalogAsync(id, populated);
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QCOMPARE(count(), 6);
		finished.clear();
		auto invalid = populated;
		invalid["diffsinger.renderSteps"] = 0;
		registry.refreshCatalogAsync(id, invalid);
		QCOMPARE(count(), 6);
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QCOMPARE(count(), 6);
		QVERIFY(!finished[0][1].toString().isEmpty());
		QVERIFY(registry.plugin(id)->valid());
		const auto key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		const auto previous = config->value("svsEngineSettings", key);
		auto restore = qScopeGuard([&] { config->setValue("svsEngineSettings", key, previous); });
		config->setValue(
			"svsEngineSettings", key, QString::fromUtf8(QJsonDocument(populated).toJson(QJsonDocument::Compact)));
		finished.clear();
		elapsed.restart();
		QTest::mouseClick(rescan, Qt::LeftButton);
		QVERIFY(elapsed.elapsed() < 200);
		QVERIFY(registry.scanning(id));
		QVERIFY(!rescan->isEnabled());
		if (m_guiApplication)
		{
			QCOMPARE(QGuiApplication::platformName(), QString("windows"));
			QTest::qWait(100);
			QVERIFY(page.screen()
					->grabWindow(page.winId())
					.save("doc/svs/validation/SVS-async-catalog-native-scanning.png"));
		}
		page.close();
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QCOMPARE(finished.size(), 1);
		QCOMPARE(count(), 6);
		{
			auto closing = std::make_unique<gui::SVSSettingsPage>();
			registry.refreshCatalogAsync(id, populated);
			closing.reset();
		}
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		QCOMPARE(count(), 6);
		{
			auto closing = std::make_unique<svs::Registry>();
			closing->voices();
			closing->refreshCatalogAsync(id, empty);
			closing.reset();
			QCoreApplication::processEvents();
		}
	}
	void exportWhileVoicebankScanning()
	{
		const auto fixture = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (fixture.isEmpty())
			QSKIP("Explicit DiffSinger voicebank fixture required");
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		QString error;
		const QJsonObject populated{
			{"diffsinger.voicebankDirectories", QJsonArray{fixture}}, {"diffsinger.renderSteps", 5}};
		QVERIFY2(registry.refreshCatalog(id, populated, error), qPrintable(error));
		svs::Voice voice;
		for (const auto& item : registry.voices())
			if (item.pluginId == id && item.name == QString::fromUtf8("芙宁娜"))
				voice = item;
		QVERIFY(!voice.id.isEmpty());
		QVERIFY2(registry.refreshCatalog(
					 id, {{"diffsinger.voicebankDirectories", QJsonArray{m_configuration.path()}}}, error),
			qPrintable(error));
		registry.refreshCatalogAsync(id, populated);
		QVERIFY(registry.scanning(id));
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, voice.id);
		QVERIFY(track->voice().id.isEmpty());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "catalog-export";
		note.lyric = QString::fromUtf8("你");
		note.duration = 48;
		clip->setNotes({note});
		QVERIFY(clip->status().contains("voicebank scan"));
		QVector<svs::ExportSnapshot::Region> regions;
		for (const auto& region : svs::ExportSnapshot::capture(*Engine::getSong(), 48000))
			if (region.track == track)
				regions.append(region);
		QCOMPARE(regions.size(), 1);
		QVERIFY(regions[0].catalogPending);
		QVERIFY(regions[0].diagnostic.isEmpty());
		const auto captured = regions[0].input;
		svs::ExportSnapshot snapshot(std::move(regions));
		snapshot.prepare();
		note.pitch = 65;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(snapshot.state() != svs::ExportSnapshot::State::Preparing, 180000);
		QVERIFY2(snapshot.state() == svs::ExportSnapshot::State::Ready, qPrintable(snapshot.diagnostics().join('\n')));
		QVERIFY(snapshot.regions()[0].audio);
		QCOMPARE(snapshot.regions()[0].input.notes, captured.notes);
		QCOMPARE(snapshot.regions()[0].input.revision, captured.revision);
		QVERIFY(!snapshot.regions()[0].input.document["voiceVersion"].toString().isEmpty());
		QCOMPARE(track->voiceId(), voice.id);
	}
	void svsBrowserEngineGroups()
	{
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		QString error;
		QVERIFY2(registry.plugin(id), qPrintable(registry.diagnostics().join('\n')));
		QVERIFY2(registry.refreshCatalog(
					 id, {{"diffsinger.voicebankDirectories", QJsonArray{m_configuration.path()}}}, error),
			qPrintable(error));
		gui::PluginBrowser browser(nullptr);
		auto* tree = browser.findChild<QTreeWidget*>();
		QVERIFY(tree);
		QTreeWidgetItem* root = nullptr;
		for (int i = 0; i < tree->topLevelItemCount(); ++i)
			if (tree->topLevelItem(i)->text(0) == "Singing Voice Synthesis")
				root = tree->topLevelItem(i);
		QVERIFY(root);
		QVERIFY(!root->isHidden());
		auto group = [&] {
			for (int i = 0; i < root->childCount(); ++i)
				if (root->child(i)->data(0, Qt::UserRole).toString() == id)
					return root->child(i);
			return static_cast<QTreeWidgetItem*>(nullptr);
		};
		QVERIFY(!group());
		const auto fixture = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		QVERIFY2(!fixture.isEmpty(), "Explicit DiffSinger voicebank fixture required");
		QVERIFY2(registry.refreshCatalog(id, {{"diffsinger.voicebankDirectories", QJsonArray{fixture}}}, error),
			qPrintable(error));
		QVERIFY(group());
		QCOMPARE(group()->text(0), QString("DiffSinger"));
		QCOMPARE(group()->childCount(), 6);
		for (int i = 0; i < root->childCount(); ++i)
			QVERIFY(!tree->itemWidget(root->child(i), 0));
		QVERIFY(QMetaObject::invokeMethod(&browser, "onFilterChanged", Q_ARG(QString, QString("DiffSinger"))));
		QVERIFY(!group()->isHidden());
		for (int i = 0; i < group()->childCount(); ++i)
			QVERIFY(!group()->child(i)->isHidden());
		const auto name = static_cast<gui::PluginDescWidget*>(tree->itemWidget(group()->child(0), 0))->name();
		QVERIFY(QMetaObject::invokeMethod(&browser, "onFilterChanged", Q_ARG(QString, name)));
		QVERIFY(!group()->isHidden());
		QVERIFY(!group()->child(0)->isHidden());
		QVERIFY(QMetaObject::invokeMethod(&browser, "onFilterChanged", Q_ARG(QString, QString("no-such-svs-voice"))));
		QVERIFY(group()->isHidden());
		QVERIFY(QMetaObject::invokeMethod(&browser, "onFilterChanged", Q_ARG(QString, QString{})));
		QVERIFY(!group()->isHidden());
		tree->setParent(nullptr);
		tree->resize(360, 700);
		tree->show();
		QVERIFY(QTest::qWaitForWindowExposed(tree));
		tree->scrollToItem(root, QAbstractItemView::PositionAtTop);
		QTest::qWait(500);
		QVERIFY(tree->screen()->grabWindow(tree->winId()).save("doc/svs/validation/SVS-browser-engine-groups.png"));
		tree->close();
		delete tree;
	}
	void diffSingerGlobalVocoderSettings()
	{
		const QString id = "org.lmms.svs.diffsinger", key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		const auto previous = config->value("svsEngineSettings", key);
		auto restore = qScopeGuard([&] { config->setValue("svsEngineSettings", key, previous); });
		config->setValue("svsEngineSettings", key, "{}");
		gui::SVSSettingsPage page;
		page.resize(1100, 800);
		page.show();
		QVERIFY(QTest::qWaitForWindowExposed(&page));
		auto* tabs = page.findChild<QTabWidget*>("svsEngineTabs");
		auto* body = page.findChild<QWidget*>("svsEnginePage." + id);
		QVERIFY(tabs && body);
		tabs->setCurrentWidget(body);
		QTRY_VERIFY_WITH_TIMEOUT(body->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories"), 10000);
		auto* editor = body->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories");
		QCOMPARE(editor->findChildren<QLineEdit*>("svsDirectoryPath").size(), 1);
		const auto defaultRoot = editor->findChild<QLineEdit*>("svsDirectoryPath")->text();
		QVERIFY(QDir::fromNativeSeparators(defaultRoot).endsWith("/svs/vocoders"));
		QTest::mouseClick(editor->findChild<QPushButton*>("svsDirectoryAdd"), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(body->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories")
									  ->findChildren<QLineEdit*>("svsDirectoryPath")
									  .size(),
			2, 10000);
		auto* path = body->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories")
						 ->findChildren<QLineEdit*>("svsDirectoryPath")
						 .last();
		path->setText(m_configuration.path());
		path->setModified(true);
		QVERIFY(QMetaObject::invokeMethod(path, "editingFinished"));
		page.save();
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(id), 30000);
		QCOMPARE(QJsonDocument::fromJson(config->value("svsEngineSettings", key).toUtf8())
					 .object()["diffsinger.vocoderDirectories"]
					 .toArray(),
			QJsonArray({defaultRoot, m_configuration.path()}));
		page.close();
		gui::SVSSettingsPage reopened;
		reopened.resize(1100, 800);
		reopened.show();
		QVERIFY(QTest::qWaitForWindowExposed(&reopened));
		auto* reopenedBody = reopened.findChild<QWidget*>("svsEnginePage." + id);
		reopened.findChild<QTabWidget*>("svsEngineTabs")->setCurrentWidget(reopenedBody);
		QTRY_VERIFY_WITH_TIMEOUT(
			reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories"), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories")
									 ->findChild<QLineEdit*>("svsDirectoryPath"),
			10000);
		QCOMPARE(reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories")
					 ->findChildren<QLineEdit*>("svsDirectoryPath")
					 .size(),
			2);
		QCOMPARE(reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.vocoderDirectories")
					 ->findChildren<QLineEdit*>("svsDirectoryPath")
					 .last()
					 ->text(),
			m_configuration.path());
		QTest::qWait(500);
		QVERIFY(reopened.screen()
				->grabWindow(reopened.winId())
				.save("doc/svs/validation/DiffSinger-global-vocoder-native-settings.png"));
		reopened.close();
	}
	void diffSingerCatalogSettingsAndResources()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (root.isEmpty())
			QSKIP("Explicit external DiffSinger fixture required");
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		registry.voices();
		auto plugin = registry.plugin(id);
		QVERIFY2(plugin, "Native DiffSinger engine must be deployed");
		QVERIFY(plugin->hasCatalogQuery());
		QString error;
		QVERIFY2(registry.refreshCatalog(
					 id, {{"diffsinger.voicebankDirectories", QJsonArray{m_configuration.path()}}}, error),
			qPrintable(error));
		int count = 0;
		for (const auto& voice : registry.voices())
			if (voice.pluginId == id)
				++count;
		QCOMPARE(count, 0);
		gui::SVSSettingsPage page;
		page.resize(1000, 750);
		page.show();
		QVERIFY(QTest::qWaitForWindowExposed(&page));
		auto* tabs = page.findChild<QTabWidget*>("svsEngineTabs");
		QVERIFY(tabs);
		auto* body = page.findChild<QWidget*>("svsEnginePage." + id);
		QVERIFY(body);
		tabs->setCurrentWidget(body);
		auto* panel = static_cast<gui::SVSParameterPanel*>(body->findChild<QWidget*>("svsParameterPanel"));
		QVERIFY(panel);
		QTRY_VERIFY_WITH_TIMEOUT(
			body->findChild<QDoubleSpinBox*>("svsParameter.track.diffsinger.renderSteps") != nullptr, 10000);
		auto* steps = body->findChild<QDoubleSpinBox*>("svsParameter.track.diffsinger.renderSteps");
		QCOMPARE(steps->minimum(), 1.);
		QCOMPARE(steps->maximum(), 100.);
		QCOMPARE(steps->value(), 20.);
		const auto declaration = plugin->engineSettings({}, {}, error);
		QCOMPARE(declaration["engineSettings"].toArray().size(), 4);
		auto* directories = body->findChild<QWidget*>("svsParameter.track.diffsinger.voicebankDirectories");
		QVERIFY(directories);
		auto* add = directories->findChild<QPushButton*>("svsDirectoryAdd");
		QVERIFY(add);
		QTest::mouseClick(add, Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(directories->findChildren<QLineEdit*>("svsDirectoryPath").size(), 1, 10000);
		auto* path = directories->findChild<QLineEdit*>("svsDirectoryPath");
		path->setText(root);
		path->setModified(true);
		QVERIFY(QMetaObject::invokeMethod(path, "editingFinished"));
		page.save();
		QTRY_VERIFY_WITH_TIMEOUT(!registry.scanning(id), 30000);
		count = 0;
		svs::Voice selected;
		for (const auto& voice : registry.voices())
			if (voice.pluginId == id)
			{
				++count;
				selected = voice;
			}
		QCOMPARE(count, 6);
		QVERIFY(selected.avatar.startsWith("svs-resource:"));
		const auto key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		config->saveConfigFile();
		const auto saved = config->value("svsEngineSettings", key);
		config->loadConfigFile(m_configuration.filePath("svs-test-config.xml"));
		QCOMPARE(config->value("svsEngineSettings", key), saved);
		QCOMPARE(QJsonDocument::fromJson(saved.toUtf8()).object()["diffsinger.voicebankDirectories"].toArray(),
			QJsonArray{root});
		gui::SVSImageLoader loader;
		loader.request(selected.package, selected.avatar, {120, 120});
		QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(), 10000);
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, selected.id);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "catalog-kept-note";
		note.lyric = QString::fromUtf8("测试");
		note.duration = 96;
		note.parameters = {{"unknown", 42}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->capabilities().languages.size(), 4);
		{
			gui::SVSPianoRoll editor(clip);
			editor.resize(1200, 800);
			editor.show();
			QVERIFY(QTest::qWaitForWindowExposed(&editor));
			QTest::qWait(500);
			QVERIFY(editor.screen()->grabWindow(editor.winId()).save("doc/svs/validation/A1-native-voice-images.png"));
			editor.close();
		}
		const auto notes = clip->notes();
		QSignalSpy changes(&registry, &svs::Registry::catalogChanged);
		QVERIFY2(registry.refreshCatalog(
					 id, {{"diffsinger.voicebankDirectories", QJsonArray{m_configuration.path()}}}, error),
			qPrintable(error));
		QVERIFY(!changes.isEmpty());
		QCOMPARE(track->voiceId(), selected.id);
		QCOMPARE(clip->notes()[0].lyric, notes[0].lyric);
		QCOMPARE(clip->notes()[0].parameters, notes[0].parameters);
		QVERIFY2(registry.refreshCatalog(id, {{"diffsinger.voicebankDirectories", QJsonArray{root}}}, error),
			qPrintable(error));
		QCOMPARE(track->voiceId(), selected.id);
		QCOMPARE(track->voice().version, selected.version);
		page.close();
		gui::SVSSettingsPage reopened;
		reopened.resize(1000, 750);
		reopened.show();
		QVERIFY(QTest::qWaitForWindowExposed(&reopened));
		auto* reopenedTabs = reopened.findChild<QTabWidget*>("svsEngineTabs");
		auto* reopenedBody = reopened.findChild<QWidget*>("svsEnginePage." + id);
		reopenedTabs->setCurrentWidget(reopenedBody);
		QTRY_VERIFY_WITH_TIMEOUT(
			reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.voicebankDirectories"), 10000);
		auto* reopenedDirectories
			= reopenedBody->findChild<QWidget*>("svsParameter.track.diffsinger.voicebankDirectories");
		QTRY_COMPARE_WITH_TIMEOUT(reopenedDirectories->findChildren<QLineEdit*>("svsDirectoryPath").size(), 1, 10000);
		QCOMPARE(reopenedDirectories->findChild<QLineEdit*>("svsDirectoryPath")->text(), root);
		QTest::qWait(500);
		QVERIFY(reopened.screen()->grabWindow(reopened.winId()).save("doc/svs/validation/A1-native-settings.png"));
		QTest::mouseClick(reopenedDirectories->findChild<QPushButton*>("svsDirectoryRemove"), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(reopenedDirectories->findChildren<QLineEdit*>("svsDirectoryPath").size(), 0, 10000);
		reopened.save();
		QCOMPARE(QJsonDocument::fromJson(config->value("svsEngineSettings", key).toUtf8())
					 .object()["diffsinger.voicebankDirectories"]
					 .toArray()
					 .size(),
			0);
		reopened.close();
	}
	void diffSingerPronunciationAndSpeakerPersistence()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (root.isEmpty())
			QSKIP("Explicit external DiffSinger fixture required");
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		QString error;
		QVERIFY2(registry.refreshCatalog(id, {{"diffsinger.voicebankDirectories", QJsonArray{root}}}, error),
			qPrintable(error));
		svs::Voice voice;
		for (const auto& candidate : registry.voices())
			if (candidate.pluginId == id && candidate.name == QString::fromUtf8("芙宁娜"))
				voice = candidate;
		QVERIFY(!voice.id.isEmpty());
		auto plugin = registry.plugin(id);
		QVERIFY(plugin);
		svs::Capabilities cap;
		QVERIFY2(svs::Capabilities::parse(plugin->capabilities(voice.id, {}, error), cap, error), qPrintable(error));
		QCOMPARE(cap.languages.size(), 4);
		QVERIFY(cap.phonemeTiming);
		QCOMPARE(cap.original["phonemes"].toObject()["minimumDurationSeconds"].toDouble(), .005);
		QCOMPARE(cap.original["phonemes"].toObject()["maximumLeadSeconds"].toDouble(), .15);
		const auto automatic
			= plugin->pronunciation(voice.id, {{"lyric", QString::fromUtf8("你")}, {"language", "zh"}}, error);
		QVERIFY2(automatic["generated"].toBool(), qPrintable(automatic["diagnostic"].toString()));
		QCOMPARE(automatic["phonemes"].toArray(), QJsonArray({"zh/n", "zh/i"}));
		const auto oov
			= plugin->pronunciation(voice.id, {{"lyric", QString::fromUtf8("☃")}, {"language", "zh"}}, error);
		QVERIFY(!oov["generated"].toBool());
		QVERIFY(!oov["diagnostic"].toString().isEmpty());
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY(track->setParameter("diffsinger.speaker", "name0"));
		QVERIFY(!track->setParameter("diffsinger.speaker", "unknown"));
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "diffsinger-user-note";
		note.lyric = QString::fromUtf8("你");
		note.pronunciation = "ni3";
		note.parameters = {{"future", 7}};
		note.phonemes = {{"segments",
			QJsonArray{QJsonObject{{"symbol", "zh/n"}, {"startTick", -9.6}, {"durationTicks", 9.6}},
				QJsonObject{{"symbol", "zh/i"}, {"startTick", 0}, {"durationTicks", 48}}}}};
		clip->setNotes({note});
		QDomDocument saved;
		auto state = saved.createElement("test");
		saved.appendChild(state);
		track->saveState(saved, state);
		auto* restored = static_cast<SVSTrack*>(Track::create(state.firstChildElement(), Engine::getSong()));
		auto restoredCleanup = qScopeGuard([&] { delete restored; });
		QCOMPARE(restored->parameters()["diffsinger.speaker"].toString(), QString("name0"));
		const auto restoredNote = static_cast<SVSClip*>(restored->getClip(0))->notes()[0];
		QCOMPARE(restoredNote.phonemes, note.phonemes);
		QCOMPARE(restoredNote.pronunciation, note.pronunciation);
		QCOMPARE(restoredNote.parameters, note.parameters);
		gui::SVSParameterPanel panel;
		if (m_guiApplication)
		{
			panel.setParent(m_guiApplication->mainWindow());
			panel.setWindowFlag(Qt::Tool);
		}
		panel.refresh(cap.parameters, "track", {track->parameters()}, {},
			[&](const QString& key, const QJsonValue& value) { track->setParameter(key, value); });
		panel.resize(350, 200);
		panel.show();
		QVERIFY(QTest::qWaitForWindowExposed(&panel));
		auto* speaker = panel.findChild<QComboBox*>("svsParameter.track.diffsinger.speaker");
		QVERIFY(speaker);
		QCOMPARE(speaker->currentData().toString(), QString("name0"));
		QTest::qWait(300);
		QVERIFY(panel.screen()->grabWindow(panel.winId()).save("doc/svs/validation/A2-native-speaker.png"));
		panel.close();
		note.phonemes.remove("segments");
		clip->setNotes({note});
		QVERIFY(!clip->notes()[0].phonemes.contains("segments"));
		QCOMPARE(clip->notes()[0].lyric, note.lyric);
	}
	void diffSingerExternalVoiceRendering()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_EXTERNAL_VOICE_ROOT"),
				   vocoders = qEnvironmentVariable("SVS_DIFFSINGER_VOCODER_ROOT");
		if (root.isEmpty() || vocoders.isEmpty())
			QSKIP("Explicit external voice and shared vocoder fixtures required");
		const QString id = "org.lmms.svs.diffsinger", key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		const auto previous = config->value("svsEngineSettings", key);
		auto restore = qScopeGuard([&] { config->setValue("svsEngineSettings", key, previous); });
		const QJsonObject settings{{"diffsinger.voicebankDirectories", QJsonArray{root}},
			{"diffsinger.vocoderDirectories", QJsonArray{vocoders}}, {"diffsinger.renderSteps", 5}};
		config->setValue(
			"svsEngineSettings", key, QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
		auto& registry = svs::Registry::instance();
		QString error;
		QVERIFY2(registry.refreshCatalog(id, settings, error), qPrintable(error));
		svs::Voice voice;
		int count = 0;
		for (const auto& candidate : registry.voices())
			if (candidate.pluginId == id)
			{
				voice = candidate;
				++count;
			}
		QCOMPARE(count, 1);
		auto* song = Engine::getSong();
		const auto tempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto* track = new SVSTrack(song);
		auto cleanup = qScopeGuard([&] {
			delete track;
			song->tempoModel().setValue(tempo);
		});
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "external-la";
		note.lyric = "la";
		note.duration = 96;
		note.pitch = 60;
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() || clip->status().startsWith("Failed"), 180000);
		QVERIFY2(clip->audio(), qPrintable(clip->status()));
		QCOMPARE(clip->status(), QString("Ready"));
		const auto audio = clip->audio();
		QVERIFY(audio->complete);
		double energy = 0;
		for (float value : audio->samples)
		{
			QVERIFY(std::isfinite(value));
			energy += double(value) * value;
		}
		QVERIFY(energy > 1e-5);
		QVERIFY(audio->feedback["pitch"].toArray().size() > 10);
		QCOMPARE(audio->feedback["curves"].toObject().size(), track->capabilities().feedbackParameters.size());
		if (m_guiApplication)
		{
			QCOMPARE(QGuiApplication::platformName(), QString("windows"));
			gui::SVSPianoRoll editor(clip);
			editor.resize(1500, 900);
			editor.show();
			QVERIFY(QTest::qWaitForWindowExposed(&editor));
			auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
			QVERIFY(canvas);
			canvas->setScroll(0, 66);
			canvas->setZoom(4, 1);
			QTest::qWait(700);
			QVERIFY(editor.screen()
					->grabWindow(editor.winId())
					.save("doc/svs/validation/DiffSinger-deepseek-native-ready.png"));
			editor.close();
		}
	}
	void diffSingerCpuAudioPlaybackAndExport()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (root.isEmpty())
			QSKIP("Explicit external DiffSinger fixture required");
		auto& registry = svs::Registry::instance();
		const QString id = "org.lmms.svs.diffsinger";
		QString error;
		QVERIFY2(registry.refreshCatalog(
					 id, {{"diffsinger.voicebankDirectories", QJsonArray{root}}, {"diffsinger.renderSteps", 5}}, error),
			qPrintable(error));
		svs::Voice voice;
		for (const auto& candidate : registry.voices())
			if (candidate.pluginId == id && candidate.name == QString::fromUtf8("芙宁娜"))
				voice = candidate;
		QVERIFY(!voice.id.isEmpty());
		auto* song = Engine::getSong();
		const auto previousTempo = song->getTempo();
		song->tempoModel().setValue(120);
		auto* track = new SVSTrack(song);
		auto cleanup = qScopeGuard([&] {
			song->stopExport();
			delete track;
			song->tempoModel().setValue(previousTempo);
		});
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		clip->setAutoResize(false);
		clip->changeLength(192);
		svs::Note a;
		a.id = "cpu-a";
		a.lyric = QString::fromUtf8("你");
		a.duration = 48;
		a.pitch = 60;
		auto b = a;
		b.id = "cpu-b";
		b.lyric = QString::fromUtf8("好");
		b.tick = 48;
		b.pitch = 62;
		clip->setNotes({a, b});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 180000);
		const auto audio = clip->audio();
		double energy = 0;
		for (float value : audio->samples)
		{
			QVERIFY(std::isfinite(value));
			energy += double(value) * value;
		}
		QVERIFY(energy > 1e-5);
		QVERIFY(audio->feedback["pitch"].toArray().size() > 10);
		QVERIFY(!gui::feedbackPitchCurves(audio->feedback["pitch"].toArray(), audio->mapping).isEmpty());
		QCOMPARE(audio->feedback["curves"].toObject().size(), 3);
		QVERIFY(std::abs(audio->mapping.globalSeconds(0) - 2) < 1e-9);
		if (qEnvironmentVariableIsSet("SVS_DIFFSINGER_GUI_EVIDENCE"))
		{
			QVERIFY(m_guiApplication);
			QCOMPARE(QGuiApplication::platformName(), QString("windows"));
			const auto originalState = clip->editorState();
			auto restoreState = qScopeGuard([&] { clip->setEditorState(originalState); });
			for (const auto& name : QStringList{"breathiness", "voicing", "tension"})
			{
				const QString id = "diffsinger." + name, key = "feedback:" + id;
				auto state = originalState;
				QJsonObject lanes;
				for (const auto& p : track->capabilities().parameters)
					lanes["input:" + p.id] = QJsonObject{{"visible", false}};
				for (const auto& p : track->capabilities().feedbackParameters)
				{
					QVERIFY(!p.writable);
					lanes["feedback:" + p.id] = QJsonObject{{"visible", p.id == id}};
				}
				state["lanes"] = lanes;
				state["selectedParameter"] = key;
				clip->setEditorState(state);
				gui::SVSPianoRoll editor(clip);
				editor.resize(1500, 900);
				editor.show();
				QVERIFY(QTest::qWaitForWindowExposed(&editor));
				auto* notes = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
				QVERIFY(notes);
				notes->setScroll(0, 66);
				notes->setZoom(4, 1);
				auto* tab = editor.findChild<QToolButton*>("svsParameterTab." + key);
				QVERIFY(tab);
				QVERIFY(tab->toolTip().contains("Read-only"));
				auto* tabs = editor.findChild<QScrollArea*>("svsParameterTabs");
				QVERIFY(tabs);
				tabs->ensureWidgetVisible(tab);
				QTest::mouseClick(tab, Qt::LeftButton);
				auto* lane = editor.findChild<gui::SVSCanvas*>("svsParameterLane." + id + ".feedback");
				QVERIFY(lane);
				const auto before = clip->curves();
				lane->setTool(gui::SVSCanvas::Tool::Freehand);
				QTest::mousePress(lane, Qt::LeftButton, Qt::NoModifier, QPoint(160, 60));
				QTest::mouseRelease(lane, Qt::LeftButton, Qt::NoModifier, QPoint(260, 90));
				QCOMPARE(clip->curves(), before);
				QCOMPARE(clip->audio(), audio);
				QTest::qWait(700);
				QVERIFY(editor.screen()
						->grabWindow(editor.winId())
						.save("doc/svs/validation/A3-native-reference-" + name + ".png"));
				editor.close();
			}
		}
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("diffsinger.wav");
		const OutputSettings settings(
			48000, 192, OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QSignalSpy done(&manager, &RenderManager::finished);
			QSignalSpy failed(&manager, &RenderManager::svsExportFailed);
			manager.renderProject();
			QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 180000);
			QCOMPARE(failed.count(), 0);
		}
		const auto exported = wavePCM(path);
		QVERIFY(!exported.isEmpty());
		double exportEnergy = 0;
		for (float value : exported)
		{
			exportEnergy += double(value) * value;
		}
		QVERIFY(exportEnergy > 1e-5);
		QCOMPARE(clip->audio(), audio);
		QDomDocument saved;
		auto state = saved.createElement("test");
		saved.appendChild(state);
		track->saveState(saved, state);
		auto* restored = static_cast<SVSTrack*>(Track::create(state.firstChildElement(), song));
		auto restoredCleanup = qScopeGuard([&] { delete restored; });
		auto* restoredClip = static_cast<SVSClip*>(restored->getClip(0));
		QCOMPARE(restoredClip->notes()[0].lyric, a.lyric);
		QCOMPARE(restoredClip->notes()[1].pitch, b.pitch);
		QTRY_VERIFY_WITH_TIMEOUT(restoredClip->audio() != nullptr, 180000);
		QCOMPARE(restoredClip->audio()->samples, audio->samples);
	}
	void segmentedPlanDependenciesAndAssembly()
	{
		svs::Input input;
		input.clipId = "segments";
		input.voiceId = "test";
		input.rate = 48000;
		input.secondsPerTick = .01;
		input.revision = 2;
		input.document = {{"position", 192}, {"contentOffset", 12}, {"secondsPerTick", .01},
			{"capabilities",
				QJsonObject{{"synthesis",
					QJsonObject{
						{"segmented", QJsonObject{{"split", "rests"}, {"version", 1}, {"paddingSeconds", .65}}}}}}}};
		svs::Note note;
		note.id = "first";
		note.duration = 48;
		input.notes.append(note);
		note.id = "middle";
		note.tick = 144;
		input.notes.append(note);
		note.id = "last";
		note.tick = 288;
		input.notes.append(note);
		QString error;
		svs::TimeMapping mapping;
		QVERIFY(svs::readTimeMapping(input.document, input.secondsPerTick, mapping, error));
		const auto original = svs::planSynthesisSegments(input, mapping, error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(original.size(), 3);
		input.notes[0].pitch += 1;
		auto changed = svs::planSynthesisSegments(input, mapping, error);
		QVERIFY(changed[0].signature != original[0].signature);
		QCOMPARE(changed[1].signature, original[1].signature);
		QCOMPARE(changed[2].signature, original[2].signature);
		input.notes[1].tick = 48;
		QCOMPARE(svs::planSynthesisSegments(input, mapping, error).size(), 2);
		input.notes[1].tick = 144;
		svs::Note rest;
		rest.id = "rest";
		rest.tick = 48;
		rest.duration = 96;
		rest.lyric = "";
		input.notes.append(rest);
		QCOMPARE(svs::planSynthesisSegments(input, mapping, error).size(), 3);
		input.notes.removeLast();
		input.document["clipParameters"] = QJsonObject{{"test", 1}};
		changed = svs::planSynthesisSegments(input, mapping, error);
		for (int i = 0; i < 3; ++i)
			QVERIFY(changed[i].signature != original[i].signature);
		input.document.remove("clipParameters");
		auto segments = svs::planSynthesisSegments(input, mapping, error);
		for (int i = 0; i < segments.size(); ++i)
		{
			auto audio = std::make_shared<svs::Audio>();
			audio->rate = input.rate;
			audio->mapping = mapping;
			audio->startSeconds = mapping.globalSeconds(input.notes[i].tick) - .65;
			audio->startTick = mapping.resultStartTick(audio->startSeconds);
			audio->samples.assign(2 * size_t(1.78 * input.rate), float(i + 1) * .1f);
			segments[i].audio = audio;
		}
		const auto full = svs::assembleSynthesisSegments(input, mapping, segments, error, true);
		QVERIFY2(full, qPrintable(error));
		QVERIFY(full->complete);
		QCOMPARE(full->feedback["segments"].toArray().size(), 3);
		segments[0].audio.reset();
		const auto partial = svs::assembleSynthesisSegments(input, mapping, segments, error);
		QVERIFY(partial);
		QVERIFY(!partial->complete);
		for (int i = 1; i < 3; ++i)
		{
			const auto frame = size_t(
				std::llround((mapping.globalSeconds(input.notes[i].tick) - partial->startSeconds) * input.rate));
			QCOMPARE(partial->samples[frame * 2], full->samples[frame * 2]);
			QVERIFY(partial->samples[frame * 2] > 0);
		}
		QCOMPARE(
			partial->samples[size_t(std::llround((mapping.globalSeconds(0) - partial->startSeconds) * input.rate)) * 2],
			0.f);
	}
	void diffSingerSegmentedRendering()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (root.isEmpty())
			QSKIP("Explicit external DiffSinger fixture required");
		const QString id = "org.lmms.svs.diffsinger",
					  settingsKey = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		const auto previousSettings = config->value("svsEngineSettings", settingsKey);
		auto restoreSettings
			= qScopeGuard([&] { config->setValue("svsEngineSettings", settingsKey, previousSettings); });
		const QJsonObject settings{
			{"diffsinger.voicebankDirectories", QJsonArray{root}}, {"diffsinger.renderSteps", 5}};
		config->setValue("svsEngineSettings", settingsKey,
			QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
		QString error;
		auto& registry = svs::Registry::instance();
		QVERIFY2(registry.refreshCatalog(id, settings, error), qPrintable(error));
		svs::Voice voice;
		for (const auto& candidate : registry.voices())
			if (candidate.pluginId == id && candidate.name == QString::fromUtf8("芙宁娜"))
				voice = candidate;
		QVERIFY(!voice.id.isEmpty());
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		const double initialGender = .01 + double(QDateTime::currentMSecsSinceEpoch() % 1000000) / 10000000.;
		QVERIFY(clip->setParameter("diffsinger.gender", initialGender));
		svs::Note note;
		note.id = "seg-first";
		note.lyric = QString::fromUtf8("你");
		note.duration = 48;
		note.pitch = 60;
		auto middle = note;
		middle.id = "seg-middle";
		middle.tick = 144;
		auto last = note;
		last.id = "seg-last";
		last.tick = 288;
		last.lyric = QString::fromUtf8("好");
		clip->setNotes({note, middle, last});
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		QVERIFY(clip->audio());
		const auto original = clip->audio();
		QVERIFY(original->complete);
		QCOMPARE(original->feedback["segments"].toArray().size(), 3);
		auto compareRetained = [&](const std::shared_ptr<const svs::Audio>& audio) {
			QVERIFY(audio);
			for (const auto& stable : {middle, last})
			{
				const auto from = size_t(std::llround(
							   original->mapping.samplePosition(stable.tick, original->startTick, original->rate))),
						   to = size_t(std::llround(original->mapping.samplePosition(
							   stable.tick + stable.duration, original->startTick, original->rate)));
				QVERIFY(to * 2 <= audio->samples.size());
				QVERIFY(std::equal(original->samples.begin() + from * 2, original->samples.begin() + to * 2,
					audio->samples.begin() + from * 2));
			}
		};
		svs::Cache::instance().clearMemory();
		note.pitch = 61;
		clip->setNotes({note, middle, last});
		compareRetained(clip->audio());
		QVERIFY(!clip->audio()->complete);
		if (m_guiApplication)
		{
			auto* view = m_guiApplication->mainWindow()->findChild<gui::SVSClipView*>();
			QVERIFY(view);
			for (auto* parent = static_cast<QWidget*>(view); parent; parent = parent->parentWidget())
				parent->show();
			QTRY_VERIFY_WITH_TIMEOUT(clip->status().contains("1/3"), 10000);
			QTest::qWait(100);
			QVERIFY(m_guiApplication->mainWindow()
					->screen()
					->grabWindow(m_guiApplication->mainWindow()->winId())
					.save("doc/svs/validation/SVS-segments-native-progress.png"));
		}
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		compareRetained(clip->audio());
		auto states = clip->audio()->feedback["segments"].toArray();
		QVERIFY(!states[0].toObject()["cached"].toBool());
		QVERIFY(states[1].toObject()["cached"].toBool());
		QVERIFY(states[2].toObject()["cached"].toBool());
		svs::Curve curve;
		curve.id = "diffsinger.breathiness.offset";
		curve.type = "float";
		curve.scope = "clip";
		for (const auto& point : QVector<QPair<double, double>>{
				 {0, 0}, {12, 6}, {36, 6}, {48, 0}, {96, 0}, {144, 0}, {192, 0}, {240, 0}, {288, 0}, {336, 0}})
			curve.insert(point.first, point.second);
		clip->setEditorData(clip->notes(), {{curve.id, curve}});
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		curve.insert(12, 8);
		clip->setEditorData(clip->notes(), {{curve.id, curve}});
		QVERIFY(clip->audio());
		QVERIFY(!clip->audio()->complete);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		states = clip->audio()->feedback["segments"].toArray();
		QVERIFY(!states[0].toObject()["cached"].toBool());
		QVERIFY(states[1].toObject()["cached"].toBool());
		QVERIFY(states[2].toObject()["cached"].toBool());
		QVERIFY(clip->setParameter("diffsinger.gender", initialGender + .2));
		QVERIFY(!clip->audio());
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		for (const auto& state : clip->audio()->feedback["segments"].toArray())
			QVERIFY(!state.toObject()["cached"].toBool());
		note.pitch = 62;
		clip->setNotes({note, middle, last});
		const auto retained = clip->audio();
		QVERIFY(retained);
		clip->cancelSynthesis();
		QTest::qWait(500);
		QCOMPARE(clip->status(), QString("Cancelled"));
		QCOMPARE(clip->audio(), retained);
		clip->synthesize();
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		QVERIFY(clip->audio()->complete);
		note.phonemes = QJsonObject{{"symbols", QJsonArray{"invalid-segment-phoneme"}}};
		clip->setNotes({note, middle, last});
		QVERIFY(clip->audio());
		QTRY_VERIFY_WITH_TIMEOUT(clip->status().startsWith("Failed:"), 30000);
		QVERIFY(clip->status().contains("segment=1/3"));
		QVERIFY(clip->audio());
		QVERIFY(!clip->audio()->complete);
		note.phonemes = {};
		clip->setNotes({note, middle, last});
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		const auto frozenAudio = clip->audio();
		auto regions = svs::ExportSnapshot::capture(*Engine::getSong(), frozenAudio->rate);
		regions.erase(std::remove_if(regions.begin(), regions.end(),
						  [track](const auto& region) { return region.track != track; }),
			regions.end());
		svs::ExportSnapshot frozen(regions);
		frozen.prepare();
		note.pitch = 63;
		clip->setNotes({note, middle, last});
		QTRY_COMPARE_WITH_TIMEOUT(frozen.state(), svs::ExportSnapshot::State::Ready, 180000);
		QCOMPARE(frozen.regions().size(), 1);
		QVERIFY(frozen.regions()[0].audio->complete);
		QCOMPARE(frozen.regions()[0].audio->samples, frozenAudio->samples);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Ready", 180000);
		QSet<QString> colors;
		for (const auto& parameter : track->capabilities().parameters)
			if (parameter.curve)
			{
				QVERIFY(QColor(parameter.color).isValid());
				QVERIFY(!colors.contains(parameter.color));
				colors.insert(parameter.color);
			}
		if (m_guiApplication)
		{
			gui::SVSPianoRoll piano(clip);
			piano.resize(1100, 700);
			piano.show();
			QTest::qWait(600);
			QVERIFY(
				piano.screen()->grabWindow(piano.winId()).save("doc/svs/validation/SVS-segments-native-colors.png"));
			piano.close();
		}
	}
	void diffSingerPresentationAndSettings()
	{
		const auto root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (root.isEmpty())
			QSKIP("Explicit external DiffSinger fixture required");
		if (!m_guiApplication)
			QSKIP("Native embedded GUI required");
		auto* config = ConfigManager::inst();
		const QString id = "org.lmms.svs.diffsinger", key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		const auto oldSettings = config->value("svsEngineSettings", key),
				   oldRanges = config->value("svs", "showVoicePitchRanges", "1"),
				   oldWave = config->value("svs", "showBackgroundWaveform", "0");
		auto restoreConfig = qScopeGuard([&] {
			config->setValue("svsEngineSettings", key, oldSettings);
			config->setValue("svs", "showVoicePitchRanges", oldRanges);
			config->setValue("svs", "showBackgroundWaveform", oldWave);
		});
		QJsonObject settings{{"diffsinger.voicebankDirectories", QJsonArray{root}}, {"diffsinger.renderSteps", 5},
			{"diffsinger.showPhonemeLanguagePrefix", true}};
		config->setValue(
			"svsEngineSettings", key, QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
		config->setValue("svs", "showVoicePitchRanges", "1");
		config->setValue("svs", "showBackgroundWaveform", "0");
		QString error;
		auto& registry = svs::Registry::instance();
		QVERIFY2(registry.refreshCatalog(id, settings, error), qPrintable(error));
		svs::Voice voice, missing;
		for (const auto& candidate : registry.voices())
		{
			if (candidate.pluginId == id && candidate.name == QString::fromUtf8("芙宁娜"))
				voice = candidate;
			if (candidate.pluginId == "org.lmms.svs.example" && candidate.id == "minimal")
				missing = candidate;
		}
		QVERIFY(!voice.id.isEmpty());
		QVERIFY(!missing.id.isEmpty());
		QVERIFY(!missing.metadata["metadata"].toObject().contains("pitchRanges"));
		const auto ranges = gui::SVSPitchRanges::fromMetadata(voice.metadata);
		QVERIFY(ranges.present);
		QVERIFY(ranges.available.contains(85));
		QVERIFY(ranges.comfortable.contains(37));
		QVERIFY(ranges.weak.contains(48));
		QVERIFY(ranges.invalid.contains("7"));
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note a;
		a.id = "display-ni";
		a.lyric = QString::fromUtf8("你");
		a.pitch = 60;
		a.duration = 48;
		auto b = a;
		b.id = "display-hao";
		b.lyric = QString::fromUtf8("好");
		b.tick = 48;
		b.pitch = 62;
		clip->setNotes({a, b});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 180000);
		const auto audio = clip->audio();
		QCOMPARE(audio->feedback["pronunciations"].toObject()[a.id].toObject()["text"].toString(), QString("ni"));
		QCOMPARE(audio->feedback["pronunciations"].toObject()[b.id].toObject()["text"].toString(), QString("hao"));
		gui::SVSPianoRoll editor(clip);
		editor.resize(1500, 900);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		auto* canvas = editor.findChild<gui::SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas);
		canvas->setScroll(0, 66);
		canvas->setZoom(4, 1);
		auto* portrait = editor.findChild<QCheckBox*>("svsPortraitVisible");
		QVERIFY(portrait);
		portrait->setChecked(false);
		auto* rangeLabel = editor.findChild<QLabel*>("svsPitchRanges");
		QVERIFY(rangeLabel);
		QVERIFY(rangeLabel->isVisible());
		QVERIFY(rangeLabel->text().contains("C#2-C#6"));
		auto* strip = editor.findChild<gui::SVSResultStrip*>("svsWaveformPhonemes");
		QVERIFY(strip);
		auto capture = [&] {
			editor.raise();
			editor.activateWindow();
			QTest::qWait(500);
			return editor.screen()->grabWindow(editor.winId());
		};
		const auto on = capture();
		QVERIFY(on.save("doc/svs/validation/A4-native-ranges-pinyin-prefix-on.png"));
		const QRect stripRect(strip->mapTo(&editor, QPoint()), strip->size());
		const auto cacheKey = svs::Cache::key(clip->captureInput(48000), "presentation");
		gui::SVSSettingsPage page;
		auto* engineTabs = page.findChild<QTabWidget*>("svsEngineTabs");
		QVERIFY(engineTabs);
		auto* engineBody = page.findChild<QWidget*>("svsEnginePage." + id);
		QVERIFY(engineBody);
		engineTabs->setCurrentWidget(engineBody);
		QTRY_VERIFY_WITH_TIMEOUT(
			engineBody->findChild<QCheckBox*>("svsParameter.track.diffsinger.showPhonemeLanguagePrefix"), 10000);
		auto* prefix = engineBody->findChild<QCheckBox*>("svsParameter.track.diffsinger.showPhonemeLanguagePrefix");
		QVERIFY(prefix->isChecked());
		page.resize(900, 700);
		page.show();
		QVERIFY(QTest::qWaitForWindowExposed(&page));
		QTest::qWait(500);
		QVERIFY(page.screen()->grabWindow(page.winId()).save("doc/svs/validation/A4-native-settings.png"));
		QTest::mouseClick(prefix, Qt::LeftButton, Qt::NoModifier, QPoint(8, prefix->height() / 2));
		QVERIFY(!prefix->isChecked());
		page.save();
		page.close();
		editor.raise();
		editor.activateWindow();
		QTRY_VERIFY_WITH_TIMEOUT(!QJsonDocument::fromJson(config->value("svsEngineSettings", key).toUtf8())
									 .object()["diffsinger.showPhonemeLanguagePrefix"]
									 .toBool(),
			5000);
		const auto off = capture();
		QVERIFY(off.save("doc/svs/validation/A4-native-ranges-pinyin-prefix-off.png"));
		QVERIFY(on.toImage().copy(stripRect) != off.toImage().copy(stripRect));
		QCOMPARE(clip->audio(), audio);
		QCOMPARE(svs::Cache::key(clip->captureInput(48000), "presentation"), cacheKey);
		QCOMPARE(clip->notes()[0].lyric, a.lyric);
		QCOMPARE(clip->notes()[0].phonemes, a.phonemes);
		gui::SVSSettingsPage reopened;
		auto* reopenedTabs = reopened.findChild<QTabWidget*>("svsEngineTabs");
		reopenedTabs->setCurrentWidget(reopened.findChild<QWidget*>("svsEnginePage." + id));
		QTRY_VERIFY_WITH_TIMEOUT(
			reopened.findChild<QCheckBox*>("svsParameter.track.diffsinger.showPhonemeLanguagePrefix"), 10000);
		QVERIFY(
			!reopened.findChild<QCheckBox*>("svsParameter.track.diffsinger.showPhonemeLanguagePrefix")->isChecked());
		auto* showRanges = reopened.findChild<QCheckBox*>("svsShowVoicePitchRanges");
		QVERIFY(showRanges);
		showRanges->setChecked(false);
		reopened.save();
		QTRY_VERIFY(!rangeLabel->isVisible());
		const auto hidden = capture();
		QVERIFY(hidden.save("doc/svs/validation/A4-native-ranges-hidden.png"));
		QVERIFY(on.toImage().copy(QRect(canvas->mapTo(&editor, QPoint()), QSize(60, canvas->height())))
			!= hidden.toImage().copy(QRect(canvas->mapTo(&editor, QPoint()), QSize(60, canvas->height()))));
		QCOMPARE(clip->audio(), audio);
		auto* background = reopened.findChild<QCheckBox*>("svsShowBackgroundWaveform");
		QVERIFY(background);
		QVERIFY(!background->isChecked());
		auto* mainWindow = m_guiApplication->mainWindow();
		mainWindow->show();
		mainWindow->raise();
		mainWindow->activateWindow();
		QTest::keyClick(mainWindow, Qt::Key_1, Qt::ControlModifier);
		QTRY_VERIFY(mainWindow->findChild<gui::SVSClipView*>());
		auto* clipView = mainWindow->findChild<gui::SVSClipView*>();
		QTRY_VERIFY(clipView->isVisible());
		QTest::qWait(500);
		const auto plainClip = clipView->grab().toImage();
		background->setChecked(true);
		reopened.save();
		QTest::qWait(500);
		QVERIFY(clipView->grab().toImage() != plainClip);
		QVERIFY(mainWindow->screen()
				->grabWindow(mainWindow->winId())
				.save("doc/svs/validation/A4-native-background-waveform.png"));
		QVERIFY(capture().toImage().copy(QRect(canvas->mapTo(&editor, QPoint()), canvas->size()))
			== hidden.toImage().copy(QRect(canvas->mapTo(&editor, QPoint()), canvas->size())));
		QCOMPARE(clip->audio(), audio);
		QCOMPARE(svs::Cache::key(clip->captureInput(48000), "presentation"), cacheKey);
		gui::SVSSettingsPage waveReopened;
		QVERIFY(waveReopened.findChild<QCheckBox*>("svsShowBackgroundWaveform")->isChecked());
		background->setChecked(false);
		reopened.save();
		QTest::qWait(200);
		QCOMPARE(clipView->grab().toImage(), plainClip);
		editor.raise();
		editor.activateWindow();
		config->saveConfigFile();
		QFile saved(m_configuration.filePath("svs-test-config.xml"));
		QVERIFY(saved.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(saved.readAll()));
		QCOMPARE(document.elementsByTagName("svs").at(0).toElement().attribute("showVoicePitchRanges"), QString("0"));
		QVERIFY(!QJsonDocument::fromJson(
			document.elementsByTagName("svsEngineSettings").at(0).toElement().attribute(key).toUtf8())
				.object()["diffsinger.showPhonemeLanguagePrefix"]
				.toBool(true));
		config->setValue("svs", "showVoicePitchRanges", "1");
		track->bindVoice(missing.pluginId, missing.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QTRY_VERIFY(!rangeLabel->isVisible());
		QVERIFY(capture().save("doc/svs/validation/A4-native-no-ranges.png"));
		editor.close();
	}
	void diffSingerReleaseWindow()
	{
#ifdef Q_OS_WIN
		const auto executable = qEnvironmentVariable("LMMS_SVS_RELEASE_EXECUTABLE"),
				   root = qEnvironmentVariable("SVS_DIFFSINGER_FIXTURE_ROOT");
		if (executable.isEmpty() || root.isEmpty())
			QSKIP("Explicit development Release executable and external voicebanks required");
		QVERIFY(m_guiApplication);
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		QVERIFY(QFileInfo(executable).exists());
		const QString id = "org.lmms.svs.diffsinger", key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
		auto* config = ConfigManager::inst();
		const auto previous = config->value("svsEngineSettings", key);
		auto restore = qScopeGuard([&] { config->setValue("svsEngineSettings", key, previous); });
		const QJsonObject settings{{"diffsinger.voicebankDirectories",
									   QJsonArray {
										   root
									   }},
			{"diffsinger.renderSteps", 5}};
		config->setValue(
			"svsEngineSettings", key, QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
		QString error;
		QVERIFY2(svs::Registry::instance().refreshCatalog(id, settings, error), qPrintable(error));
		svs::Voice voice;
		for (const auto& item : svs::Registry::instance().voices())
			if (item.pluginId == id && item.name == QString::fromUtf8("芙宁娜"))
				voice = item;
		QVERIFY(!voice.id.isEmpty());
		auto* track = new SVSTrack(Engine::getSong());
		auto cleanup = qScopeGuard([&] { delete track; });
		track->bindVoice(id, voice.id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "release-ni";
		note.lyric = QString::fromUtf8("你");
		note.pitch = 60;
		note.duration = 48;
		auto second = note;
		second.id = "release-hao";
		second.lyric = QString::fromUtf8("好");
		second.tick = 48;
		clip->setNotes({note, second});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio(), 180000);
		auto* releaseClipView = m_guiApplication->mainWindow()->findChild<gui::SVSClipView*>();
		QVERIFY(releaseClipView);
		for (auto* parent = static_cast<QWidget*>(releaseClipView); parent; parent = parent->parentWidget())
			parent->show();
		QTest::qWait(500);
		QVERIFY(releaseClipView->isVisible());
		const auto project = m_configuration.filePath("diffsinger-release.mmp");
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		config->saveConfigFile();
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		connect(&process, &QProcess::readyReadStandardOutput, this,
			[&] { qInfo().noquote() << process.readAllStandardOutput(); });
		process.start(executable, {"--config", m_configuration.filePath("svs-test-config.xml"), project});
		QVERIFY(process.waitForStarted(5000));
		auto stop = qScopeGuard([&] {
			if (process.state() != QProcess::NotRunning)
			{
				process.kill();
				process.waitForFinished(5000);
			}
		});
		struct Window
		{
			DWORD pid;
			HWND handle = nullptr;
		};
		Window window{DWORD(process.processId())};
		auto findWindow = [&] {
			window.handle = nullptr;
			EnumWindows(
				[](HWND handle, LPARAM value) -> BOOL {
					auto* target = reinterpret_cast<Window*>(value);
					DWORD pid = 0;
					GetWindowThreadProcessId(handle, &pid);
					RECT rect{};
					GetWindowRect(handle, &rect);
					if (pid == target->pid && IsWindowVisible(handle) && rect.right - rect.left > 900
						&& rect.bottom - rect.top > 600)
					{
						target->handle = handle;
						return FALSE;
					}
					return TRUE;
				},
				reinterpret_cast<LPARAM>(&window));
			return window.handle != nullptr;
		};
		QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning || findWindow(), 30000);
		QVERIFY2(window.handle, "Development Release exited before exposing its native main window");
		auto title = [&] {
			wchar_t value[512]{};
			GetWindowTextW(window.handle, value, 512);
			return QString::fromWCharArray(value);
		};
		QTRY_VERIFY_WITH_TIMEOUT(title().contains("diffsinger-release"), 45000);
		QTest::qWait(1500);
		QCOMPARE(process.state(), QProcess::Running);
		SetForegroundWindow(window.handle);
		QTest::qWait(500);
		auto capture = QGuiApplication::primaryScreen()->grabWindow(WId(window.handle));
		auto bluePixels = [](const QPixmap& pixmap) {
			const auto pixels = pixmap.toImage();
			int blue = 0;
			for (int y = 0; y < pixels.height(); ++y)
				for (int x = 0; x < pixels.width(); ++x)
					if (pixels.pixelColor(x, y) == QColor("#3C6DA6"))
						++blue;
			return blue;
		};
		QVERIFY(capture.save("doc/svs/validation/A4-native-release-main.png"));
		QVERIFY2(bluePixels(capture) > 20, "SVS default clip blue is missing from the real Song Editor window");
		const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, window.pid);
		QVERIFY(snapshot != INVALID_HANDLE_VALUE);
		auto closeSnapshot = qScopeGuard([&] { CloseHandle(snapshot); });
		MODULEENTRY32W module{};
		module.dwSize = sizeof(module);
		bool plugin = false, runtime = false;
		for (BOOL valid = Module32FirstW(snapshot, &module); valid; valid = Module32NextW(snapshot, &module))
		{
			const auto path = QString::fromWCharArray(module.szExePath).replace('\\', '/');
			if (QString::fromWCharArray(module.szModule) == "SVSDiffSinger.dll")
			{
				plugin = true;
				QCOMPARE(path, QFileInfo(executable).absolutePath() + "/svs/SVSDiffSinger/SVSDiffSinger.dll");
			}
			if (QString::fromWCharArray(module.szModule) == "onnxruntime.dll")
			{
				runtime = true;
				QCOMPARE(path, QFileInfo(executable).absolutePath() + "/svs/SVSDiffSinger/onnxruntime.dll");
			}
		}
		// Startup catalog discovery may still be active after the window appears.
		// Native catalog callbacks have no cancellation ABI; shutdown joins them.
		QVERIFY(plugin);
		QVERIFY(runtime);
		PostMessageW(window.handle, WM_CLOSE, 0, 0);
		QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 30000);
		QCOMPARE(process.exitStatus(), QProcess::NormalExit);
		QCOMPARE(process.exitCode(), 0);
		QFile originalConfig(m_configuration.filePath("svs-test-config.xml"));
		QVERIFY(originalConfig.open(QIODevice::ReadOnly));
		QDomDocument emptyDocument;
		QVERIFY(emptyDocument.setContent(originalConfig.readAll()));
		auto emptySettings = settings;
		emptySettings["diffsinger.voicebankDirectories"] = QJsonArray{};
		emptyDocument.elementsByTagName("svsEngineSettings")
			.at(0)
			.toElement()
			.setAttribute(key, QString::fromUtf8(QJsonDocument(emptySettings).toJson(QJsonDocument::Compact)));
		const auto emptyPath = m_configuration.filePath("diffsinger-empty-config.xml");
		QFile emptyConfig(emptyPath);
		QVERIFY(emptyConfig.open(QIODevice::WriteOnly));
		emptyConfig.write(emptyDocument.toByteArray());
		emptyConfig.close();
		process.start(executable, {"--config", emptyPath});
		QVERIFY(process.waitForStarted(5000));
		window.pid = DWORD(process.processId());
		QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning || findWindow(), 30000);
		QVERIFY(window.handle);
		QTest::qWait(2000);
		QCOMPARE(process.state(), QProcess::Running);
		QVERIFY(QGuiApplication::primaryScreen()
				->grabWindow(WId(window.handle))
				.save("doc/svs/validation/A4-native-empty-release.png"));
		PostMessageW(window.handle, WM_CLOSE, 0, 0);
		QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 10000);
		QCOMPARE(process.exitStatus(), QProcess::NormalExit);
		QCOMPARE(process.exitCode(), 0);
#else
		QSKIP("Native Windows Release validation");
#endif
	}
	void engineCacheDirectoriesAndLegacyRead()
	{
		QTemporaryDir directory, legacy;
		QVERIFY(directory.isValid());
		QVERIFY(legacy.isValid());
		svs::Cache cache(directory.path(), 8192, 32768, legacy.path());
		svs::Input input;
		input.voiceId = "cache-voice";
		input.rate = 48000;
		input.secondsPerTick = .01;
		input.document = {{"pluginId", "org.lmms.svs.diffsinger"}, {"position", 0}, {"contentOffset", 0}};
		auto audio = std::make_shared<svs::Audio>();
		audio->rate = 48000;
		audio->samples.assign(256, .1f);
		audio->waveform.build(audio->samples);
		const auto key = svs::Cache::key(input, "native-version");
		QCOMPARE(cache.engineDirectory(input.document["pluginId"].toString()),
			QDir(directory.path()).filePath("DiffSinger"));
		input.document["cacheDirectory"] = "other-machine/cache/SVS/DiffSinger";
		QCOMPARE(svs::Cache::key(input, "native-version"), key);
		cache.put(key, input, audio);
		const auto current
			= QDir(cache.engineDirectory(input.document["pluginId"].toString())).filePath(key + ".svsmeta");
		QFile index(current);
		QVERIFY(index.open(QIODevice::ReadOnly));
		const auto info = QJsonDocument::fromJson(index.readAll()).object();
		index.close();
		const auto sha = info["audioSHA256"].toString();
		QFile wave(QDir(cache.engineDirectory(input.document["pluginId"].toString())).filePath(sha + ".wav"));
		QVERIFY(wave.open(QIODevice::ReadOnly));
		QCOMPARE(
			QString::fromLatin1(QCryptographicHash::hash(wave.readAll(), QCryptographicHash::Sha256).toHex()), sha);
		wave.close();
		QVERIFY(!QFileInfo::exists(directory.filePath(key + ".wav")));
		// A previous-version combined cache remains readable without migration.
		auto legacyInfo = info;
		legacyInfo.remove("audioSHA256");
		const auto metadata = QJsonDocument(legacyInfo).toJson(QJsonDocument::Compact);
		QByteArray payload;
		QDataStream stream(&payload, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_6_0);
		stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
		stream << quint32(0x53565331) << quint32(metadata.size());
		stream.writeRawData(metadata.constData(), metadata.size());
		stream << quint64(audio->samples.size());
		for (float value : audio->samples)
			stream << value;
		QFile old(legacy.filePath(key + ".svscache"));
		QVERIFY(old.open(QIODevice::WriteOnly));
		const auto checksum = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
		QCOMPARE(old.write(checksum), qint64(checksum.size()));
		QCOMPARE(old.write(payload), qint64(payload.size()));
		old.close();
		QVERIFY(QFile::remove(current));
		cache.clearMemory();
		QVERIFY(cache.get(key, input));
		auto example = input;
		example.document["pluginId"] = "org.lmms.svs.example";
		cache.put(svs::Cache::key(example, "native-version"), example, audio);
		QVERIFY(cache.engineDirectory(example.document["pluginId"].toString()).endsWith("/SVSExample"));
		QVERIFY(QFileInfo::exists(legacy.filePath(key + ".svscache")));
	}
	void parameterPanelStateAndFocus()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto plugin = svs::Registry::instance().plugin(voice.pluginId);
		QString error;
		svs::Capabilities cap;
		QVERIFY(svs::Capabilities::parse(plugin->capabilities("full", {}, error), cap, error));
		gui::SVSParameterPanel panel;
		panel.resize(300, 300);
		QString changed;
		QJsonValue committed;
		auto setter = [&](const QString& id, const QJsonValue& value) {
			changed = id;
			committed = value;
		};
		panel.refresh(cap.parameters, "note", {QJsonObject{}, QJsonObject{}}, {}, setter);
		auto* label = panel.findChild<QLineEdit*>("svsParameter.note.example.label");
		QVERIFY(label);
		QCOMPARE(label->placeholderText(), QString("Unset"));
		auto* soft = panel.findChild<QCheckBox*>("svsParameter.note.example.soft");
		QVERIFY(soft);
		QCOMPARE(soft->checkState(), Qt::PartiallyChecked);
		panel.refresh(cap.parameters, "note",
			{QJsonObject{{"example.label", "one"}}, QJsonObject{{"example.label", "two"}}}, {}, setter);
		QCOMPARE(label->placeholderText(), QString("Mixed"));
		panel.show();
		panel.activateWindow();
		label->setFocus();
		QTRY_VERIFY(label->hasFocus());
		QTest::keyClicks(label, "pending");
		panel.refresh(cap.parameters, "note", {QJsonObject{{"example.label", "old"}}}, {}, setter);
		QCOMPARE(panel.findChild<QLineEdit*>("svsParameter.note.example.label"), label);
		QCOMPARE(label->text(), QString("pending"));
		QTest::keyClick(label, Qt::Key_Return);
		QCOMPARE(changed, QString("example.label"));
		QCOMPARE(committed.toString(), QString("pending"));
		gui::SVSParameterPanel readOnly;
		readOnly.refresh(cap.feedbackParameters, "clip", {QJsonObject{{"example.energy", 42}}}, {}, setter);
		auto* energy = readOnly.findChild<QWidget*>("svsParameter.clip.example.energy");
		QVERIFY(energy);
		QVERIFY(!energy->isEnabled());
	}
	void capabilitiesAndDictionaries()
	{
		const auto& voices = svs::Registry::instance().voices();
		QVERIFY(!voices.isEmpty());
		auto plugin = svs::Registry::instance().plugin(voices[0].pluginId);
		QVERIFY(plugin);
		QString error;
		svs::Capabilities full, minimal;
		QVERIFY2(svs::Capabilities::parse(plugin->capabilities("full", {}, error), full, error), qPrintable(error));
		QVERIFY2(
			svs::Capabilities::parse(plugin->capabilities("minimal", {}, error), minimal, error), qPrintable(error));
		QCOMPARE(full.parameter("example.tension", "clip")->color, QString("#AFD867"));
		auto colored = full.original;
		auto colorParameters = colored["parameters"].toArray();
		auto badColor = colorParameters[0].toObject();
		badColor["color"] = "red";
		colorParameters[0] = badColor;
		colored["parameters"] = colorParameters;
		svs::Capabilities colorInvalid;
		QVERIFY(!svs::Capabilities::parse(colored, colorInvalid, error));
		QCOMPARE(full.languages.size(), 3);
		QCOMPARE(minimal.languages.size(), 1);
		QVERIFY(full.phonemeTiming);
		QVERIFY(!minimal.phonemeTiming);
		auto gain = full.parameter("example.gain", "track");
		QVERIFY(gain);
		QVERIFY(gain->accepts(1));
		QVERIFY(!gain->accepts(0));
		auto mode = full.parameter("example.mode", "track");
		QVERIFY(mode);
		QVERIFY(mode->accepts("advanced"));
		QVERIFY(!mode->accepts(1));
		auto breath = full.parameter("example.breath", "clip");
		QVERIFY(breath);
		QVERIFY(!breath->isVisible({}));
		QVERIFY(breath->isVisible({{"example.mode", "advanced"}}));
		QVERIFY(!full.feedbackParameters.isEmpty());
		QVERIFY(!full.feedbackParameters[0].writable);
		auto schema = full.original;
		auto parameters = schema["parameters"].toArray();
		parameters.append(parameters[0]);
		schema["parameters"] = parameters;
		svs::Capabilities invalid;
		QVERIFY(!svs::Capabilities::parse(schema, invalid, error));
		auto resources = full.original;
		auto resourceParameters = resources["parameters"].toArray();
		resourceParameters.append(QJsonObject{{"id", "test.resource"}, {"name", "Resource"}, {"scope", "clip"},
			{"type", "string"}, {"default", "first"}, {"resourceIds", QJsonArray{"first", "second"}}});
		resources["parameters"] = resourceParameters;
		svs::Capabilities constrained;
		QVERIFY(svs::Capabilities::parse(resources, constrained, error));
		auto resource = constrained.parameter("test.resource", "clip");
		QVERIFY(resource);
		QVERIFY(resource->accepts("first"));
		QVERIFY(!resource->accepts("../external"));
		gui::SVSParameterPanel resourcePanel;
		resourcePanel.refresh(constrained.parameters, "clip", {QJsonObject{}}, {}, {});
		auto* selector = resourcePanel.findChild<QComboBox*>("svsParameter.clip.test.resource");
		QVERIFY(selector);
		QCOMPARE(selector->itemData(1).toString(), QString("second"));
		QCOMPARE(svs::selectedValue({QJsonObject{}, QJsonObject{}}, "x").state, svs::ValueState::Unset);
		QCOMPARE(
			svs::selectedValue({QJsonObject{{"x", 1}}, QJsonObject{{"x", 1}}}, "x").state, svs::ValueState::Common);
		QCOMPARE(svs::selectedValue({QJsonObject{{"x", 1}}, QJsonObject{}}, "x").state, svs::ValueState::Mixed);
		QFile dictionaryFile(voices[0].package + "/zh.json");
		QVERIFY(dictionaryFile.open(QIODevice::ReadOnly));
		auto bytes = dictionaryFile.readAll();
		svs::Dictionary dictionary;
		QVERIFY2(svs::Dictionary::parse(bytes, full.phonemeSet, dictionary, error), qPrintable(error));
		svs::Note note;
		note.id = "reading";
		note.lyric = QString::fromUtf8("重");
		note.language = "zh";
		auto result = svs::resolvePronunciation(note, full, {dictionary}, {}, "en");
		QVERIFY(result.generated);
		QCOMPARE(result.candidates.size(), 2);
		QCOMPARE(result.source, QString("voiceDictionary"));
		note.pronunciation = "chong";
		result = svs::resolvePronunciation(note, full, {dictionary}, {}, "en");
		QCOMPARE(result.text, QString("chong"));
		QCOMPARE(result.source, QString("manualPronunciation"));
		note.phonemes = {{"symbols", QJsonArray{"l", "a"}}};
		result = svs::resolvePronunciation(note, full, {dictionary}, {}, "en");
		QCOMPARE(result.source, QString("manualPhonemes"));
		auto projectDictionary = dictionary;
		projectDictionary.id = "project.zh";
		projectDictionary.entries[note.lyric]
			= QJsonArray{QJsonObject{{"reading", "project"}, {"phonemes", QJsonArray{"h", "ao"}}}};
		note.phonemes = {};
		note.pronunciation.clear();
		result = svs::resolvePronunciation(note, full, {dictionary}, {projectDictionary}, "en");
		QCOMPARE(result.source, QString("projectDictionary"));
		QCOMPARE(result.text, QString("project"));
		QFile japaneseFile(voices[0].package + "/ja.json");
		QVERIFY(japaneseFile.open(QIODevice::ReadOnly));
		svs::Dictionary japanese;
		QVERIFY(svs::Dictionary::parse(japaneseFile.readAll(), full.phonemeSet, japanese, error));
		note.language = "ja";
		note.lyric = QString::fromUtf8("ら");
		result = svs::resolvePronunciation(note, full, {japanese}, {}, "en");
		QVERIFY(result.generated);
		QCOMPARE(result.text, QString("ra"));
		note.language = "zh";
		auto corrupt = QJsonDocument::fromJson(bytes).object();
		auto entries = corrupt["entries"].toArray();
		entries.append(entries[0]);
		corrupt["entries"] = entries;
		QVERIFY(!svs::Dictionary::parse(QJsonDocument(corrupt).toJson(), full.phonemeSet, dictionary, error));
		QVERIFY(error.contains("entries["));
		note.phonemes = {};
		note.pronunciation.clear();
		note.lyric = "unknown";
		result = svs::resolvePronunciation(note, full, {}, {}, "en");
		QVERIFY(!result.generated);
		QCOMPARE(result.text, note.lyric);
		QVERIFY(!result.diagnostic.isEmpty());
		note.lyric = "-";
		result = svs::resolvePronunciation(note, full, {}, {}, "en");
		QVERIFY(!result.continuation);
		QVERIFY(!result.diagnostic.isEmpty());
		auto fallback = plugin->pronunciation("full", {{"lyric", "l a"}}, error);
		QVERIFY(fallback["generated"].toBool());
		QCOMPARE(fallback["phonemes"].toArray().size(), 2);
	}
	void verticalSlice()
	{
		QCOMPARE(int(Track::Type::Instrument), 0);
		QCOMPARE(int(Track::Type::HiddenAutomation), 6);
		QCOMPARE(int(Track::Type::SVS), 7);
		QVERIFY(!Track::create(Track::Type::SVS, Engine::patternStore()));
		const auto& voices = svs::Registry::instance().voices();
		QVERIFY2(voices.size() >= 2, qPrintable(svs::Registry::instance().diagnostics().join('\n')));
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voices[0].pluginId, voices[0].id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->name(), voices[0].name);
		QVERIFY(QFileInfo::exists(track->voice().avatar));
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		{
			gui::SVSPianoRoll editor(clip, nullptr);
			auto* canvas = editor.findChild<QWidget*>("svsNoteCanvas");
			QVERIFY(canvas);
			QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(108, 222));
			QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(108, 222));
			QCOMPARE(clip->notes().size(), 1);
			QCOMPARE(clip->notes()[0].tick, 24.);
			QCOMPARE(clip->notes()[0].pitch, 64.);
			QCOMPARE(clip->notes()[0].lyric, voices[0].defaultLyric);
		}
		svs::Note note;
		note.id = "note-fixture";
		note.tick = 24;
		note.duration = 96;
		note.pitch = 64;
		note.lyric = QString::fromUtf8("测试");
		note.parameters = {{"unknown.parameter", 42}};
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto audio = clip->audio();
		QVERIFY(!audio->samples.empty());
		double energy = 0;
		for (float sample : audio->samples)
		{
			QVERIFY(std::isfinite(sample));
			energy += sample * sample;
		}
		QVERIFY(energy > 1);
		track->setName("Custom SVS");
		track->bindVoice(voices[1].pluginId, voices[1].id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(track->name(), QString("Custom SVS"));
		track->bindVoice(voices[0].pluginId, voices[0].id);
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		QDomDocument doc;
		auto parent = doc.createElement("test");
		doc.appendChild(parent);
		track->saveState(doc, parent);
		auto* restored = static_cast<SVSTrack*>(Track::create(parent.firstChildElement(), Engine::getSong()));
		QCOMPARE(restored->name(), QString("Custom SVS"));
		QCOMPARE(restored->voiceId(), track->voiceId());
		QCOMPARE(restored->numOfClips(), 1);
		auto* saved = static_cast<SVSClip*>(restored->getClip(0));
		QCOMPARE(saved->startPosition(), TimePos(192));
		QCOMPARE(saved->notes().size(), 1);
		QCOMPARE(saved->notes()[0].lyric, note.lyric);
		QCOMPARE(saved->notes()[0].parameters, note.parameters);
		auto* copy = static_cast<SVSClip*>(clip->clone());
		QVERIFY(copy->id() != clip->id());
		QVERIFY(copy->notes()[0].id != clip->notes()[0].id);
		copy->movePosition(960);
		QCOMPARE(clip->startPosition(), TimePos(192));
		QCOMPARE(copy->notes()[0].tick, note.tick);
		delete copy;
		delete restored;
		auto* mixer = Engine::mixer();
		mixer->createChannel();
		mixer->createChannel();
		track->mixerChannelModel()->setRange(0, 2, 1);
		track->mixerChannelModel()->setValue(2);
		QVERIFY(mixer->isChannelInUse(2));
		mixer->moveChannelLeft(2);
		QCOMPARE(track->mixerChannelModel()->value(), 1);
		mixer->deleteChannel(1);
		QCOMPARE(track->mixerChannelModel()->value(), 0);
		Engine::getSong()->getTimeline(Song::PlayMode::Song).setTicks(192);
		Engine::getSong()->playSong();
		double mixedEnergy = 0;
		for (int period = 0; period < 220; ++period)
		{
			auto buffer = Engine::audioEngine()->renderNextPeriod();
			for (const auto& sample : buffer)
				mixedEnergy += sample[0] * sample[0] + sample[1] * sample[1];
		}
		Engine::getSong()->stop();
		QVERIFY2(mixedEnergy > 0.01, "SVS PCM did not reach LMMS mixed output");
		delete track;
	}
	void renderedParametersAndPersistence()
	{
		const auto& voice = svs::Registry::instance().voices()[0];
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVERIFY2(track->capabilityDiagnostics().isEmpty(), qPrintable(track->capabilityDiagnostics().join('\n')));
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "parameter-note";
		note.lyric = "la";
		clip->setNotes({note});
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto first = clip->audio();
		double firstEnergy = 0;
		for (float value : first->samples)
			firstEnergy += value * value;
		QVERIFY(track->setParameter("example.gain", .5));
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto quiet = clip->audio();
		double quietEnergy = 0;
		for (float value : quiet->samples)
			quietEnergy += value * value;
		QVERIFY(std::abs(quietEnergy / firstEnergy - .25) < .01);
		QVERIFY(!track->setParameter("example.gain", 9));
		QVERIFY(!clip->setParameter("example.energy", 1));
		QVERIFY(clip->setParameter("example.breath", .7));
		QVERIFY(track->setParameter("example.mode", "basic"));
		QCOMPARE(clip->parameters()["example.breath"].toDouble(), .7);
		QVERIFY(clip->setNoteParameter({note.id}, "example.phonemeGain", .5, true));
		QVERIFY(!clip->setNoteParameter({note.id}, "example.phonemeGain", 3, true));
		track->bindVoice(voice.pluginId, "minimal");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(clip->parameters()["example.breath"].toDouble(), .7);
		QCOMPARE(track->parameters()["example.gain"].toDouble(), .5);
		QVERIFY(!track->capabilities().parameter("example.breath", "clip"));
		QVERIFY(!clip->setParameter("example.breath", .9));
		track->bindVoice(voice.pluginId, "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QCOMPARE(clip->parameters()["example.breath"].toDouble(), .7);
		QDomDocument doc;
		auto root = doc.createElement("test");
		doc.appendChild(root);
		track->saveState(doc, root);
		auto* restored = static_cast<SVSTrack*>(Track::create(root.firstChildElement(), Engine::getSong()));
		QCOMPARE(restored->parameters(), track->parameters());
		QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->parameters(), clip->parameters());
		QTRY_VERIFY_WITH_TIMEOUT(clip->audio() != nullptr, 10000);
		auto reading = clip->audio()->feedback["pronunciations"].toObject()[note.id].toObject();
		QVERIFY(reading["generated"].toBool());
		QCOMPARE(reading["source"].toString(), QString("voiceDictionary"));
		delete restored;
		delete track;
	}
};
QTEST_MAIN(SVSIntegrationTest)
#include "SVSIntegrationTest.moc"
