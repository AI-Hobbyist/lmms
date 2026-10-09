#include <stdio.h>
#include <string.h>

#include "svc.h"

static int64_t read_input(void* user, uint8_t* destination, size_t capacity)
{
	unsigned* reads = (unsigned*)user;
	if ((*reads)++) { return 0; }
	if (capacity < 4) { return -1; }
	memset(destination, 0, 4);
	return 4;
}

static int event(void* user, const svc_event* value)
{
	unsigned* events = (unsigned*)user;
	++*events;
	printf("SVC event %d generation %llu samples %llu\n", (int)value->type, (unsigned long long)value->generation_id,
		(unsigned long long)value->sample_count);
	return 0;
}

int main(void)
{
	const svc_engine* engine = svc_reference_engine(SVC_ABI_VERSION);
	unsigned reads = 0;
	unsigned events = 0;
	svc_request request = {
		sizeof(svc_request), SVC_ABI_VERSION, 7, 1, "{\"model_id\":\"identity\"}", read_input, &reads, event, &events};
	char error[256];
	const char* description = engine->capabilities(NULL);
	void* job;
	svc_status status;
	if (svc_validate_capabilities(description, strlen(description), error, sizeof(error)) != SVC_OK)
	{
		puts(error);
		return 1;
	}
	job = engine->start(NULL, &request);
	if (!job) { return 2; }
	do
	{
		status = engine->pump(job);
	} while (status == SVC_OK);
	engine->destroy(job);
	return status == SVC_COMPLETE && events == 3 ? 0 : 3;
}
