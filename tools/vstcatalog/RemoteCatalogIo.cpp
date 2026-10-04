#include "CatalogFilesystem.h"
#include "vsthost/ControlChannel.h"
#include "vsthost/SharedRegion.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <stdexcept>

using namespace lmms::vsthost;
int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG {
		TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
		return EXCEPTION_EXECUTE_HANDLER;
	});
	if (argc != 7 || std::wstring_view(argv[1]) != L"--host-session") { return 2; }
	int qtArgc = 1; char name[] = "RemoteCatalogIo"; char* qtArgv[]{name, nullptr}; QCoreApplication app(qtArgc, qtArgv);
	bool valid = false;
	const auto session = QString::fromWCharArray(argv[4]).toULongLong(&valid); if (!valid || !session) { return 2; }
	const auto generation = QString::fromWCharArray(argv[5]).toULongLong(&valid); if (!valid || !generation) { return 2; }
	const auto capacity = QString::fromWCharArray(argv[6]).toUInt(&valid);
	if (!valid || !ControlChannel::storageBytes(capacity)) { return 2; }
	SharedRegion region;
	if (!region.attach(argv[2], ControlChannel::storageBytes(capacity))) { return 3; }
	ControlChannel channel(region.bytes(), ControlChannel::Side::Helper, capacity);
	bool hello = false; std::uint64_t sequence = 0;
	for (;;)
	{
		ControlChannel::Frame frame;
		if (channel.wait(frame, 300000, [] { return true; }) != Error::None || !matches(frame.header, session, generation, ++sequence)) { return 4; }
		Error error = Error::None; std::vector<std::uint8_t> payload;
		if (!hello && frame.header.type == MessageType::Hello && frame.payload.empty()) { hello = true; }
		else if (hello && frame.header.type == MessageType::Scan)
		{
			try
			{
				QJsonParseError parse;
				const auto document = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size()), &parse);
				if (parse.error != QJsonParseError::NoError || !document.isObject()) { throw std::invalid_argument("JSON"); }
					const auto request = document.object();
#ifdef LMMS_CATALOG_IO_FIXTURE
					auto path = request.value("path").toString();
					if (request.value("op") == "discover") {
						path = request.value("roots").toObject().value("roots").toArray().first().toObject().value("path").toString();
					}
					if (path.endsWith("__hang_io__")) {
						const auto eventName = QStringLiteral("Local\\LMMS-CatalogIoFixture-") + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
						if (auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.toStdWString().c_str())) { SetEvent(event); CloseHandle(event); }
						Sleep(INFINITE);
					}
					if (path.endsWith("__crash_io__")) { RaiseException(0xe0000065, 0, 0, nullptr); }
#endif
					auto reply = QJsonObject{};
#ifdef LMMS_CATALOG_IO_FIXTURE
					if (path.endsWith("__bad_io__")) { reply = {{"schema", 1}, {"modules", "invalid"}, {"failures", QJsonArray{}}}; }
					else
#endif
					{ reply = catalogFileOperation(request); }
					const auto bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact);
				if (bytes.size() > capacity) { throw std::invalid_argument("Reply capacity"); }
				payload.assign(bytes.begin(), bytes.end());
			}
			catch (const std::invalid_argument&) { error = Error::InvalidMessage; }
			catch (...) { error = Error::LoadFailed; }
		}
		else if (!(hello && frame.header.type == MessageType::Close && frame.payload.empty())) { error = Error::InvalidMessage; }
		if (error != Error::None) { payload.resize(4); put(payload, 0, static_cast<unsigned>(error), 4); }
		const auto type = error != Error::None ? MessageType::Fault : frame.header.type == MessageType::Scan ? MessageType::ScanResult : frame.header.type;
		if (channel.send({type, session, generation, sequence, static_cast<std::uint32_t>(payload.size())}, payload) != Error::None) { return 5; }
		if (error != Error::None || frame.header.type == MessageType::Close) { return error == Error::None ? 0 : 6; }
	}
}
