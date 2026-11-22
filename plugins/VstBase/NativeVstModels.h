#ifndef LMMS_NATIVE_VST_MODELS_H
#define LMMS_NATIVE_VST_MODELS_H

#include <QComboBox>
#include <QDomDocument>
#include <QMap>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "AutomatableModel.h"
#include "Knob.h"
#include "VstPlugin.h"

namespace lmms {

class NativeVstModels
{
public:
	void initialize(VstPlugin& plugin, Model* owner)
	{
		for (const auto& parameter : plugin.automationParameters())
		{
			if (!parameter.automatable)
			{
				m_editorOnly.append(parameter.name);
				continue;
			}
			auto* model = new FloatModel(parameter.value, 0.f, 1.f, parameter.step, owner, parameter.name);
			plugin.bindParameterModel(parameter.index, model);
			QObject::connect(model, &FloatModel::dataChanged, &plugin,
				[&plugin, model, index = parameter.index] { plugin.setParam(index, model->value()); },
				Qt::DirectConnection);
			m_models.insert(parameter.key, model);
		}
	}

	QMap<QString, AutomatableModel*> models() const { return m_models; }

	void addControls(QVBoxLayout* layout) const
	{
		if (m_models.isEmpty() && m_editorOnly.isEmpty()) { return; }

		auto* selector = new QComboBox(layout->parentWidget());
		selector->setAccessibleName(VstPlugin::tr("VST parameter"));
		auto* knob = new gui::Knob(gui::KnobType::Small17, layout->parentWidget());
		for (auto it = m_models.cbegin(); it != m_models.cend(); ++it)
		{
			selector->addItem(it.value()->displayName(), it.key());
		}
		for (const auto& name : m_editorOnly)
		{
			selector->addItem(VstPlugin::tr("%1 (plug-in editor only)").arg(name));
		}
		const auto selectParameter = [models = m_models, selector, knob](int index)
		{
			auto* model = models.value(selector->itemData(index).toString());
			if (model) { knob->setModel(model); }
			knob->setVisible(model != nullptr);
		};
		QObject::connect(selector, &QComboBox::currentIndexChanged, knob, selectParameter);
		selectParameter(selector->currentIndex());
		layout->addWidget(selector);
		layout->addWidget(knob);
	}

	void load(VstPlugin& plugin, const QDomElement& state)
	{
		for (const auto& parameter : plugin.automationParameters())
		{
			if (auto* model = m_models.value(parameter.key))
			{
				const QSignalBlocker block(model);
				model->setValue(parameter.value);
			}
		}
		const auto saved = state.firstChildElement("automationparameters");
		for (auto it = m_models.cbegin(); it != m_models.cend(); ++it)
		{
			if (saved.hasAttribute(it.key()) || !saved.firstChildElement(it.key()).isNull())
			{
				it.value()->loadSettings(saved, it.key());
			}
		}
	}

	void save(QDomDocument& document, QDomElement& state)
	{
		auto saved = document.createElement("automationparameters");
		for (auto it = m_models.cbegin(); it != m_models.cend(); ++it)
		{
			it.value()->saveSettings(document, saved, it.key());
		}
		state.appendChild(saved);
	}

private:
	QMap<QString, AutomatableModel*> m_models;
	QStringList m_editorOnly;
};

} // namespace lmms

#endif
