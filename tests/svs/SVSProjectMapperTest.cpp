#include "SVSProjectMapper.h"
#include "svs_curve.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace lmms::svs;
class SVSProjectMapperTest : public QObject
{
	Q_OBJECT
	static QJsonObject fixture()
	{
		return QJsonDocument::fromJson(
			R"({"song_tempo_list":[{"position":0,"bpm":120},{"position":960,"bpm":150}],"time_signature_list":[{"bar_index":0,"numerator":4,"denominator":4}],"track_list":[{"type_":"Singing","title":"中文 / 日本語 / 한국어","mute":true,"solo":true,"volume":0.5,"pan":-0.25,"note_list":[{"start_pos":961,"length":479,"key_number":60,"lyric":"你","pronunciation":"ni"},{"start_pos":1200,"length":480,"key_number":64,"lyric":""}],"edited_params":{"pitch":{"points":[[-192000,-100],[2881,-100],[2881,6000],[3121,6050],[3361,5980],[3361,-100],[3600,-100],[3600,6400],[3840,6420],[3840,-100],[1073741823,-100]]}}},{"type_":"Singing","title":"","note_list":[],"edited_params":{}}]})")
			.object();
	}
	static ProjectVoice voice() { return {"engine-stable-id", "voice-stable-id", QStringLiteral("默认声库"), "zh"}; }
private slots:
	void pitchTimeAndVoice()
	{
		const auto result = ProjectMapper::prepareImport(fixture(), voice(), {});
		QVERIFY2(result.valid(), qPrintable(result.error));
		QVERIFY(result.losses.isEmpty());
		const auto tracks = result.document.elementsByTagName("track");
		QCOMPARE(tracks.size(), 2);
		for (int i = 0; i < tracks.size(); ++i)
		{
			const auto settings = tracks.at(i).firstChildElement("svstrack");
			QCOMPARE(settings.attribute("pluginId"), voice().pluginId);
			QCOMPARE(settings.attribute("voiceId"), voice().voiceId);
		}
		QCOMPARE(tracks.at(1).toElement().attribute("name"), voice().name);
		const auto track = tracks.at(0).toElement();
		QCOMPARE(track.attribute("muted"), QString("1"));
		QCOMPARE(track.attribute("solo"), QString("1"));
		QCOMPARE(track.firstChildElement("svstrack").attribute("vol").toDouble(), 50.);
		QCOMPARE(track.firstChildElement("svstrack").attribute("pan").toDouble(), -25.);
		const auto clip = track.firstChildElement("svsclip");
		const auto note = clip.firstChildElement("notes").firstChildElement("note");
		QVERIFY(std::abs(note.attribute("tick").toDouble() - 96.1) < 1e-9);
		QVERIFY(std::abs(note.attribute("duration").toDouble() - 47.9) < 1e-9);
		QCOMPARE(note.attribute("pronunciation"), QString("ni"));
		QCOMPARE(note.nextSiblingElement().attribute("lyric"), QString{});
		const auto curves = QJsonDocument::fromJson(clip.attribute("curves").toUtf8()).object();
		const auto pitch = curves["svs.pitch"].toObject();
		QCOMPARE(pitch["mode"].toString(), QString("absolute"));
		svs_sdk::Curve curve;
		for (const auto& value : pitch["points"].toArray())
		{
			const auto p = value.toObject();
			svs_sdk::CurvePoint point;
			point.tick = p["tick"].toDouble();
			point.value = p["value"].toDouble();
			point.breakAfter = p["breakAfter"].toBool();
			curve.points.push_back(point);
		}
		QVERIFY(std::abs(curve.evaluate(108.1).value - 60.25) < 1e-6);
		QVERIFY(std::abs(curve.evaluate(132.1).value - 60.15) < 1e-6);
		QVERIFY(!curve.evaluate(155).covered);
		QVERIFY(curve.evaluate(168).covered);
		QVERIFY(!curve.evaluate(193).covered);
		QDomDocument reopened;
		QVERIFY(reopened.setContent(result.document.toByteArray()));
		QCOMPARE(reopened.elementsByTagName("svsclip").at(0).toElement().attribute("curves"), clip.attribute("curves"));
		const auto second = ProjectMapper::prepareImport(fixture(), voice(), {});
		QVERIFY(second.document.elementsByTagName("note").at(0).toElement().attribute("id") != note.attribute("id"));
		QCOMPARE(
			result.document.elementsByTagName("journalRuntime").at(0).toElement().attribute("fileName"), QString{});
	}
	void rejectedStructures()
	{
		auto project = fixture();
		auto meters = project["time_signature_list"].toArray();
		meters.append(QJsonObject{{"bar_index", 2}, {"numerator", 3}, {"denominator", 4}});
		project["time_signature_list"] = meters;
		auto result = ProjectMapper::prepareImport(project, voice(), {});
		QVERIFY(!result.valid());
		QVERIFY(result.document.isNull());
		QVERIFY(result.error.contains(QStringLiteral("changing time signatures")));
		project = fixture();
		auto tempos = project["song_tempo_list"].toArray();
		tempos.insert(1, QJsonObject{{"position", 1}, {"bpm", 140}});
		project["song_tempo_list"] = tempos;
		QVERIFY(!ProjectMapper::prepareImport(project, voice(), {}).valid());
		auto invalidVoice = voice();
		invalidVoice.voiceId.clear();
		QVERIFY(!ProjectMapper::prepareImport(fixture(), invalidVoice, {}).valid());
	}
	void audioAndQuantizationLosses()
	{
		QVERIFY(std::abs(ProjectMapper::audioLengthTicks(fixture(), 48, 1) - 108) < 1e-9);
		QVERIFY(std::abs(ProjectMapper::audioLengthTicks(fixture(), 144, 1) - 120) < 1e-9);
		auto project = fixture();
		auto tracks = project["track_list"].toArray();
		tracks.append(QJsonObject{{"type_", "Instrumental"}, {"title", QStringLiteral("伴奏")},
			{"audio_file_path", "source.wav"}, {"offset", 481}});
		project["track_list"] = tracks;
		auto missing = ProjectMapper::prepareImport(project, voice(), {});
		QVERIFY(missing.valid());
		QCOMPARE(missing.document.elementsByTagName("track").size(), 2);
		QVERIFY(missing.losses.join('\n').contains("source.wav"));
		auto prepared = ProjectMapper::prepareImport(project, voice(), {{"source.wav", {"durable.wav", 96.25}}});
		QVERIFY(prepared.valid());
		auto sample = prepared.document.elementsByTagName("sampleclip").at(0).toElement();
		QCOMPARE(sample.attribute("pos").toInt(), 48);
		QCOMPARE(sample.attribute("len").toInt(), 97);
		QCOMPARE(sample.attribute("src"), QString("durable.wav"));
		QVERIFY(!prepared.losses.isEmpty());
		auto tempos = project["song_tempo_list"].toArray();
		auto tempo = tempos.at(1).toObject();
		tempo["bpm"] = 150.3;
		tempos.replace(1, tempo);
		project["song_tempo_list"] = tempos;
		QVERIFY(ProjectMapper::prepareImport(project, voice(), {}).losses.join('\n').contains("150.3"));
	}
};
QTEST_GUILESS_MAIN(SVSProjectMapperTest)
#include "SVSProjectMapperTest.moc"
