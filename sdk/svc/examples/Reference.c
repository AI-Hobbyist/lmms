#include <stdlib.h>
#include <string.h>

#include "svc.h"

typedef struct reference_job
{
	svc_request request;
	char* selection;
	svc_status status;
	unsigned stage;
	uint64_t offset;
	uint8_t input[SVC_MAX_FEED];
} reference_job;

static const char* capabilities(void* engine)
{
	(void)engine;
	return "{\"schema_version\":1,\"engine_id\":\"reference\","
		   "\"models\":[{\"id\":\"identity\",\"name\":\"Reference identity\","
		   "\"speakers\":[{\"id\":\"0\",\"name\":\"Speaker 0\"}]}],"
		   "\"parameters\":[{\"id\":\"gain\",\"name\":\"Gain\",\"type\":\"number\","
		   "\"scope\":\"request\",\"unit\":\"dB\",\"default\":0,\"step\":1}],"
		   "\"limits\":{\"max_upload_bytes\":104857600,\"max_seconds\":600,\"min_seconds\":0.1}}";
}

static void* start(void* engine, const svc_request* request)
{
	reference_job* job;
	size_t length;
	(void)engine;
	if (!request || request->size < sizeof(*request) || request->abi_version != SVC_ABI_VERSION || !request->read
		|| !request->callback || !request->selection_json)
	{
		return NULL;
	}
	length = strlen(request->selection_json);
	if (length > SVC_MAX_HEADER) { return NULL; }
	job = (reference_job*)calloc(1, sizeof(*job));
	if (!job) { return NULL; }
	job->selection = (char*)malloc(length + 1);
	if (!job->selection)
	{
		free(job);
		return NULL;
	}
	memcpy(job->selection, request->selection_json, length + 1);
	job->request = *request;
	job->request.selection_json = job->selection;
	return job;
}

static svc_status pump(void* pointer)
{
	reference_job* job = (reference_job*)pointer;
	svc_event event = {0};
	int64_t count;
	if (!job) { return SVC_INVALID; }
	if (job->status != SVC_OK) { return job->status; }
	event.size = sizeof(event);
	event.generation_id = job->request.generation_id;
	event.segment_id = job->request.segment_id;
	event.request_id = "reference-1";
	event.sample_rate = 16000;
	event.channels = 1;
	if (job->stage == 0)
	{
		event.type = SVC_START;
		job->stage = 1;
	}
	else
	{
		/* Reference input is mono PCM16; production adapters consume WAV bytes. */
		count = job->request.read(job->request.input_user, job->input, sizeof(job->input));
		if (count < 0 || count > (int64_t)sizeof(job->input) || count % 2)
		{
			job->status = SVC_FAILED;
			return job->status;
		}
		if (!count)
		{
			event.type = SVC_DONE;
			event.sample_count = job->offset;
			job->status = SVC_COMPLETE;
		}
		else
		{
			event.type = SVC_AUDIO;
			event.chunk_index = job->stage++;
			event.sample_offset = job->offset;
			event.sample_count = (uint64_t)count / 2;
			event.bytes = job->input;
			event.byte_count = (size_t)count;
			job->offset += event.sample_count;
		}
	}
	if (job->request.callback(job->request.callback_user, &event)) { job->status = SVC_CANCELLED; }
	return job->status;
}

static void cancel(void* pointer)
{
	reference_job* job = (reference_job*)pointer;
	if (job && job->status == SVC_OK) { job->status = SVC_CANCELLED; }
}

static void destroy(void* pointer)
{
	reference_job* job = (reference_job*)pointer;
	if (job)
	{
		cancel(job);
		free(job->selection);
		free(job);
	}
}

const svc_engine* svc_reference_engine(uint32_t requested_version)
{
	static const svc_engine engine
		= {sizeof(svc_engine), SVC_ABI_VERSION, "reference", capabilities, start, pump, cancel, destroy};
	return requested_version == SVC_ABI_VERSION ? &engine : NULL;
}
