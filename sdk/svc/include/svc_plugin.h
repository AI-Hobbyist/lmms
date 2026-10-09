#ifndef LMMS_SVC_PLUGIN_H
#define LMMS_SVC_PLUGIN_H
#include "svc.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional module entry point, resolved as svc_plugin_entry_v1. All strings
 * are UTF-8. create_context copies address/token and performs no network I/O.
 * capabilities runs on the discovery worker and returns bounded JSON owned
 * by the context. Jobs copy connection settings; destroy_context follows all
 * jobs. The host keeps the module loaded until every context is released. */
typedef struct svc_plugin
{
	uint32_t size;
	uint32_t abi_version;
	const char* name;
	const char* default_address;
	const svc_engine* engine;
	void* (*create_context)(const char* address, const char* token);
	void (*destroy_context)(void* context);
	const char* (*error)(void* context);
} svc_plugin;
typedef const svc_plugin* (*svc_plugin_entry)(uint32_t requested_version);
#ifdef __cplusplus
}
#endif
#endif
