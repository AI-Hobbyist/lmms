#ifndef LMMS_VSTHOST_CATALOG_LOCATOR_CODEC_H
#define LMMS_VSTHOST_CATALOG_LOCATOR_CODEC_H
#include <QByteArray>
#include <QString>

namespace lmms::vsthost {
// A present fingerprint is exactly one SHA-256 value. QByteArray::fromHex
// alone silently ignores invalid characters, which could turn a damaged pin
// into an empty optional locator field.
inline bool decodeCatalogFingerprint(const QString& text, QByteArray& decoded)
{
	decoded.clear();
	if (text.size() != 64)
	{
		return false;
	}
	for (const auto character : text)
	{
		const auto code = character.unicode();
		if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F')))
		{
			return false;
		}
	}
	decoded = QByteArray::fromHex(text.toLatin1());
	return decoded.size() == 32;
}
} // namespace lmms::vsthost
#endif
