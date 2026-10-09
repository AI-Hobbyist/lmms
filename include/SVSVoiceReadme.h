#ifndef LMMS_SVS_VOICE_README_H
#define LMMS_SVS_VOICE_README_H

#include "SVSModel.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

namespace lmms::gui {

inline QStringList voiceReadmeFiles(const svs::Voice& voice)
{
	const auto directory = voice.metadata.value("voicebankPath").toString();
	QStringList files;
	if (directory.isEmpty())
	{
		return files;
	}
	for (const auto& file : QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::Name))
	{
		if (file.fileName().compare("readme.md", Qt::CaseInsensitive) == 0
			|| file.fileName().compare("readme.txt", Qt::CaseInsensitive) == 0)
		{
			files << file.absoluteFilePath();
		}
	}
	return files;
}

inline QDialog* openVoiceReadme(const svs::Voice& voice, QWidget* parent)
{
	const auto files = voiceReadmeFiles(voice);
	if (files.isEmpty())
	{
		return nullptr;
	}
	auto* dialog = new QDialog(parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setObjectName("svsVoiceReadmeDialog");
	dialog->setWindowTitle(
		QCoreApplication::translate("lmms::gui::SVSPianoRoll", "Voicebank README: %1").arg(voice.name));
	dialog->resize(760, 560);
	auto* layout = new QVBoxLayout(dialog);
	auto* tabs = new QTabWidget(dialog);
	layout->addWidget(tabs);
	for (const auto& path : files)
	{
		auto* viewer = new QTextEdit(tabs);
		viewer->setReadOnly(true);
		QFile file(path);
		if (file.open(QIODevice::ReadOnly))
		{
			QTextStream stream(&file);
			const auto text = stream.readAll();
			if (QFileInfo(path).suffix().compare("md", Qt::CaseInsensitive) == 0)
			{
				viewer->setMarkdown(text);
			}
			else
			{
				viewer->setPlainText(text);
			}
		}
		else
		{
			viewer->setPlainText(
				QCoreApplication::translate("lmms::gui::SVSPianoRoll", "Cannot read README: %1").arg(path));
		}
		tabs->addTab(viewer, QFileInfo(path).fileName());
	}
	dialog->show();
	return dialog;
}

} // namespace lmms::gui

#endif
