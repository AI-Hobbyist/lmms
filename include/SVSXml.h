#ifndef LMMS_SVS_XML_H
#define LMMS_SVS_XML_H
#include <QDomDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTextStream>
namespace lmms::svs {
inline QJsonObject xmlExtras(
	const QDomElement& node, const QSet<QString>& knownAttributes = {}, const QSet<QString>& knownChildren = {})
{
	QJsonObject attributes;
	const auto source = node.attributes();
	for (int i = 0; i < source.count(); ++i)
		if (!knownAttributes.contains(source.item(i).nodeName()))
			attributes[source.item(i).nodeName()] = source.item(i).nodeValue();
	QString children;
	QTextStream stream(&children);
	for (auto child = node.firstChild(); !child.isNull(); child = child.nextSibling())
		if (!knownChildren.contains(child.nodeName()))
			child.save(stream, 0);
	if (attributes.isEmpty() && children.isEmpty())
		return {};
	return {{"attributes", attributes}, {"children", children}};
}
inline void applyXmlExtras(QDomDocument& doc, QDomElement& node, const QJsonObject& extras)
{
	const auto attributes = extras["attributes"].toObject();
	for (auto i = attributes.begin(); i != attributes.end(); ++i)
		node.setAttribute(i.key(), i.value().toString());
	QDomDocument wrapper;
	if (wrapper.setContent("<svsExtras>" + extras["children"].toString() + "</svsExtras>"))
		for (auto child = wrapper.documentElement().firstChild(); !child.isNull(); child = child.nextSibling())
			node.appendChild(doc.importNode(child, true));
}
inline void copyXml(QDomDocument& doc, QDomElement& destination, const QDomElement& source)
{
	applyXmlExtras(doc, destination, xmlExtras(source));
}
inline bool jsonObjectAttribute(const QDomElement& node, const QString& name, QJsonObject& value, QString& diagnostic)
{
	const auto bytes = node.attribute(name).toUtf8();
	value = {};
	if (bytes.isEmpty())
		return true;
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
	{
		diagnostic = "Invalid " + name + " JSON; original SVS node preserved";
		return false;
	}
	value = document.object();
	return true;
}
inline bool jsonArrayAttribute(const QDomElement& node, const QString& name, QJsonArray& value, QString& diagnostic)
{
	const auto bytes = node.attribute(name).toUtf8();
	value = {};
	if (bytes.isEmpty())
		return true;
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isArray())
	{
		diagnostic = "Invalid " + name + " JSON; original SVS node preserved";
		return false;
	}
	value = document.array();
	return true;
}
inline bool supportedXmlSchema(const QDomElement& node, QString& diagnostic)
{
	bool valid = false;
	const auto version = node.attribute("schemaVersion", "0").toInt(&valid);
	if (!valid || version < 0 || version > 1)
	{
		diagnostic = "Unsupported SVS schema " + node.attribute("schemaVersion") + "; original node preserved";
		return false;
	}
	return true;
}
}
#endif
