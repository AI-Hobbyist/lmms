/* Minimal C11 plugin: required ABI prefix, one voice, no optional capabilities. */
#include "svs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef struct
{
	double start, duration, pitch;
} Note;
typedef struct
{
	svs_host host;
} Engine;
typedef struct
{
	Engine* engine;
	Note* notes;
	uint32_t count, rate;
	double duration, origin;
	uint64_t request;
	float* audio;
	atomic_int cancelled;
} Session;
/* Read an unescaped top-level numeric key, skipping strings and nested values. */
static int numberField(const char* json, const char* key, double* value)
{
	const char* p = json;
	int depth = 0;
	if (!p)
		return 0;
	while (*p)
	{
		if (*p == '"')
		{
			const char* begin = ++p;
			int escaped = 0;
			while (*p && *p != '"')
			{
				if (*p == '\\' && p[1])
				{
					escaped = 1;
					p += 2;
				}
				else
					++p;
			}
			if (!*p)
				return 0;
			if (depth == 1 && !escaped && (size_t)(p - begin) == strlen(key)
				&& !strncmp(begin, key, (size_t)(p - begin)))
			{
				char* end;
				++p;
				while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
					++p;
				if (*p++ != ':')
					return 0;
				*value = strtod(p, &end);
				if (end == p || !isfinite(*value))
					return 0;
				while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
					++end;
				return *end == ',' || *end == '}';
			}
			++p;
		}
		else
		{
			if (*p == '{' || *p == '[')
				++depth;
			if (*p == '}' || *p == ']')
				--depth;
			++p;
		}
	}
	return 0;
}
static svs_status text(const char* source, const char** out)
{
	size_t size;
	if (!out)
		return SVS_INVALID_INPUT;
	size = strlen(source) + 1;
	*out = (char*)malloc(size);
	if (!*out)
		return SVS_FAILED;
	memcpy((void*)*out, source, size);
	return SVS_OK;
}
static svs_status SVS_CALL create(const svs_host* host, svs_engine* out)
{
	Engine* engine;
	if (!out)
		return SVS_INVALID_INPUT;
	engine = (Engine*)calloc(1, sizeof(*engine));
	if (!engine)
		return SVS_FAILED;
	if (host && host->size >= offsetof(svs_host, allocate_buffer))
		memcpy(&engine->host, host, offsetof(svs_host, allocate_buffer));
	*out = engine;
	return SVS_OK;
}
static void SVS_CALL destroy(svs_engine engine)
{
	free(engine);
}
static svs_status SVS_CALL catalog(svs_engine engine, const char** out)
{
	if (!engine)
		return SVS_INVALID_INPUT;
	return text(
		"{\"voices\":[{\"id\":\"minimal\",\"name\":\"SVS Minimal C\",\"version\":\"0.1.0\",\"author\":\"LMMS SVS contributors\",\"description\":\"Required-prefix C SDK example\",\"languages\":[\"en\"],\"defaultLanguage\":\"en\",\"defaultLyric\":\"la\",\"license\":\"GPL-2.0-or-later\"}]}",
		out);
}
static svs_status SVS_CALL capabilities(svs_engine engine, const char* voice, const char* context, const char** out)
{
	(void)context;
	if (!engine || !voice || strcmp(voice, "minimal"))
		return SVS_INVALID_INPUT;
	return text(
		"{\"schemaVersion\":1,\"languages\":[\"en\"],\"defaultLanguage\":\"en\",\"parameters\":[],\"feedbackParameters\":[],\"synthesis\":{\"cancel\":true,\"concurrent\":false,\"segmented\":false,\"channels\":2,\"format\":\"float32\"}}",
		out);
}
static void SVS_CALL releaseString(svs_engine engine, const char* value)
{
	(void)engine;
	free((void*)value);
}
static svs_status SVS_CALL createSession(svs_engine engine, const char* voice, svs_session* out)
{
	Session* session;
	if (!engine || !out || !voice || strcmp(voice, "minimal"))
		return SVS_INVALID_INPUT;
	session = (Session*)calloc(1, sizeof(*session));
	if (!session)
		return SVS_FAILED;
	session->engine = (Engine*)engine;
	atomic_init(&session->cancelled, 0);
	*out = session;
	return SVS_OK;
}
static void SVS_CALL destroySession(svs_session handle)
{
	Session* session = (Session*)handle;
	if (session)
	{
		free(session->notes);
		free(session->audio);
		free(session);
	}
}
static svs_status SVS_CALL submit(svs_session handle, const svs_snapshot* input)
{
	Session* session = (Session*)handle;
	Note* notes;
	uint32_t i;
	double origin = 0, position = 0, offset = 0;
	if (!session || !input || input->size < sizeof(*input) || !input->voice_id || strcmp(input->voice_id, "minimal")
		|| input->sample_rate < 8000 || input->sample_rate > 192000 || input->note_count > 100000
		|| (input->note_count && !input->notes) || !isfinite(input->duration_seconds) || input->duration_seconds < 0
		|| input->duration_seconds > 60)
		return SVS_INVALID_INPUT;
	if (!numberField(input->input_json, "originSeconds", &origin))
	{
		numberField(input->input_json, "position", &position);
		numberField(input->input_json, "contentOffset", &offset);
		if (position != 0 || offset != 0)
			return SVS_INVALID_INPUT;
	}
	notes = input->note_count ? (Note*)calloc(input->note_count, sizeof(*notes)) : NULL;
	if (input->note_count && !notes)
		return SVS_FAILED;
	for (i = 0; i < input->note_count; ++i)
	{
		const svs_note* note = &input->notes[i];
		if (note->size < sizeof(*note) || !isfinite(note->start_seconds) || !isfinite(note->duration_seconds)
			|| !isfinite(note->pitch) || note->duration_seconds <= 0 || note->pitch < 0 || note->pitch > 127)
		{
			free(notes);
			return SVS_INVALID_INPUT;
		}
		notes[i].start = note->start_seconds;
		notes[i].duration = note->duration_seconds;
		notes[i].pitch = note->pitch;
	}
	free(session->notes);
	free(session->audio);
	session->audio = NULL;
	session->notes = notes;
	session->count = input->note_count;
	session->duration = input->duration_seconds;
	session->origin = origin;
	session->rate = input->sample_rate;
	session->request = input->request_id;
	atomic_store(&session->cancelled, 0);
	return SVS_OK;
}
static svs_status SVS_CALL render(svs_session handle, svs_result* out)
{
	Session* session = (Session*)handle;
	uint64_t frames, frame;
	uint32_t index;
	svs_result result = {0};
	if (!session || !out || out->size < sizeof(*out) || session->rate == 0)
		return SVS_INVALID_INPUT;
	frames = (uint64_t)ceil(session->duration * session->rate);
	if (frames > 16u * 1024 * 1024)
		return SVS_INVALID_INPUT;
	free(session->audio);
	session->audio = (float*)calloc((size_t)(frames ? frames : 1) * 2, sizeof(float));
	if (!session->audio)
		return SVS_FAILED;
	if (session->engine->host.progress)
		session->engine->host.progress(session->engine->host.context, session->request, 0, "Rendering");
	for (frame = 0; frame < frames; ++frame)
	{
		double seconds = (double)frame / session->rate, sample = 0;
		if (frame % 1024 == 0 && atomic_load(&session->cancelled))
			return SVS_CANCELLED;
		for (index = 0; index < session->count; ++index)
		{
			const Note* note = &session->notes[index];
			double relative = seconds - note->start;
			if (relative >= 0 && relative < note->duration)
			{
				double envelope = fmin(1, fmin(relative / .01, (note->duration - relative) / .03));
				sample += .15 * envelope * sin(6.283185307179586 * 440 * pow(2, (note->pitch - 69) / 12) * relative);
			}
		}
		session->audio[frame * 2] = session->audio[frame * 2 + 1] = (float)sample;
	}
	result.size = sizeof(result);
	result.sample_rate = session->rate;
	result.channels = 2;
	result.frame_count = frames;
	result.start_seconds = session->origin;
	result.audio = session->audio;
	result.feedback_json = "{}";
	result.error_json = "{}";
	*out = result;
	if (session->engine->host.progress)
		session->engine->host.progress(session->engine->host.context, session->request, 1, "Ready");
	return SVS_OK;
}
static void SVS_CALL cancel(svs_session handle)
{
	if (handle)
		atomic_store(&((Session*)handle)->cancelled, 1);
}
static void SVS_CALL releaseResult(svs_session handle, svs_result* result)
{
	Session* session = (Session*)handle;
	if (session)
	{
		free(session->audio);
		session->audio = NULL;
	}
	if (result)
	{
		memset(result, 0, sizeof(*result));
		result->size = sizeof(*result);
	}
}
SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* out)
{
	svs_api api = {0};
	(void)minor;
	if (major != SVS_ABI_MAJOR || !out || size < SVS_API_REQUIRED_SIZE)
		return SVS_BAD_ABI;
	api.size = SVS_API_REQUIRED_SIZE;
	api.major = SVS_ABI_MAJOR;
	api.minor = 0;
	api.create_engine = create;
	api.destroy_engine = destroy;
	api.catalog = catalog;
	api.capabilities = capabilities;
	api.release_string = releaseString;
	api.create_session = createSession;
	api.destroy_session = destroySession;
	api.submit = submit;
	api.render = render;
	api.cancel = cancel;
	api.release_result = releaseResult;
	memcpy(out, &api, SVS_API_REQUIRED_SIZE);
	return SVS_OK;
}
