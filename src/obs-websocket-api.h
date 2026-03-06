/*
 * obs-websocket-api.h — vendored from obsproject/obs-websocket
 * https://github.com/obsproject/obs-websocket/blob/master/src/obs-websocket-api.h
 * License: GPLv2
 *
 * Copied into src/ so the header is available in headless CI builds where
 * obs-websocket's own headers are not on the include path.
 */

#pragma once

#include <obs-module.h>
#include <util/threading.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct obs_websocket_vendor obs_websocket_vendor_t;
typedef void (*obs_websocket_request_callback_function)(
	obs_data_t *request_data, obs_data_t *response_data, void *priv_data);

static obs_websocket_vendor_t *
obs_websocket_register_vendor(const char *vendor_name)
{
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph)
		return NULL;

	calldata_t cd = {0};
	calldata_set_string(&cd, "vendor_name", vendor_name);
	if (!proc_handler_call(ph, "obs_websocket_register_vendor", &cd)) {
		calldata_free(&cd);
		return NULL;
	}
	obs_websocket_vendor_t *vendor =
		(obs_websocket_vendor_t *)calldata_ptr(&cd, "vendor");
	calldata_free(&cd);
	return vendor;
}

static bool obs_websocket_vendor_register_request(
	obs_websocket_vendor_t *vendor, const char *request_type,
	obs_websocket_request_callback_function request_callback,
	void *priv_data)
{
	if (!vendor)
		return false;
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph)
		return false;

	calldata_t cd = {0};
	calldata_set_ptr(&cd, "vendor", vendor);
	calldata_set_string(&cd, "request_type", request_type);
	calldata_set_ptr(&cd, "request_callback", (void *)request_callback);
	calldata_set_ptr(&cd, "private_data", priv_data);
	bool success = proc_handler_call(
		ph, "obs_websocket_vendor_register_request", &cd);
	calldata_free(&cd);
	return success;
}

static bool obs_websocket_vendor_emit_event(obs_websocket_vendor_t *vendor,
					    const char *event_name,
					    obs_data_t *event_data)
{
	if (!vendor)
		return false;
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph)
		return false;

	calldata_t cd = {0};
	calldata_set_ptr(&cd, "vendor", vendor);
	calldata_set_string(&cd, "event_name", event_name);
	calldata_set_ptr(&cd, "event_data", event_data);
	bool success =
		proc_handler_call(ph, "obs_websocket_vendor_emit_event", &cd);
	calldata_free(&cd);
	return success;
}

#ifdef __cplusplus
}
#endif
