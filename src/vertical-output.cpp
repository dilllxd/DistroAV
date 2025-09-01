/******************************************************************************
	Copyright (C) 2016-2024 DistroAV <contact@distroav.org>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, see <https://www.gnu.org/licenses/>.
******************************************************************************/

#include "vertical-output.h"

#include "plugin-main.h"

#include <obs-frontend-api.h>
#include <obs.h>

struct vertical_output_context {
    bool is_running;
    QString ndi_name;
    QString ndi_groups;
    QString last_error;
    obs_output_t *output;
};

static struct vertical_output_context vctx = {0};

static const char *AITUM_VERTICAL_CANVAS_NAME = "Aitum Vertical";

QString vertical_output_last_error()
{
	return vctx.last_error;
}

// Some OBS builds expose a canvas API used by Aitum. When available we will
// grab the "Aitum Vertical" canvas directly. Otherwise we fall back.
#if defined(HAVE_OBS_CANVAS_API)
static obs_canvas_t *find_aitum_vertical_canvas()
{
    obs_frontend_canvas_list cl = {};
    obs_frontend_get_canvases(&cl);
    obs_canvas_t *found = nullptr;
    for (size_t i = 0; i < cl.canvases.num; i++) {
        obs_canvas_t *c = cl.canvases.array[i];
        const char *name = obs_canvas_get_name(c);
        if (name && strcmp(name, AITUM_VERTICAL_CANVAS_NAME) == 0) {
            found = obs_canvas_get_ref(c);
            break;
        }
    }
    obs_frontend_canvas_list_free(&cl);
    return found;
}
#endif

// Fetch Aitum Vertical canvas video via vendor proc exposed by the Aitum
// plugin. Returns nullptr if not available or not loaded.
static video_t *get_aitum_vertical_video()
{
    proc_handler_t *ph = obs_get_proc_handler();
    if (!ph)
        return nullptr;
    calldata cd;
    calldata_init(&cd);
    // width/height 0 matches any vertical canvas
    calldata_set_int(&cd, "width", 0);
    calldata_set_int(&cd, "height", 0);
    video_t *video = nullptr;
    bool called = proc_handler_call(ph, "aitum_vertical_get_video", &cd);
    if (called) {
        video = (video_t *)calldata_ptr(&cd, "video");
    }
    calldata_free(&cd);
    return video;
}

void on_vertical_output_started(void *, calldata_t *)
{
	obs_log(LOG_DEBUG, "+on_vertical_output_started()");
	Config::Current()->VerticalOutputEnabled = true;
	obs_log(LOG_DEBUG, "-on_vertical_output_started()");
	obs_log(LOG_INFO, "NDI Vertical Output started");
}

void on_vertical_output_stopped(void *, calldata_t *)
{
	obs_log(LOG_DEBUG, "+on_vertical_output_stopped()");
	Config::Current()->VerticalOutputEnabled = false;
	obs_log(LOG_DEBUG, "-on_vertical_output_stopped()");
	obs_log(LOG_INFO, "NDI Vertical Output stopped");
}

void vertical_output_stop()
{
	obs_log(LOG_DEBUG, "+vertical_output_stop()");
    if (vctx.is_running) {
        obs_log(LOG_DEBUG, "vertical_output_stop: stopping NDI Vertical Output '%s'", QT_TO_UTF8(vctx.ndi_name));
        // Detach any foreign video/audio queues to avoid dangling references
        obs_output_set_media(vctx.output, nullptr, nullptr);
        obs_output_stop(vctx.output);
        vctx.is_running = false;
        obs_log(LOG_DEBUG, "vertical_output_stop: stopped NDI Vertical Output '%s'", QT_TO_UTF8(vctx.ndi_name));
    } else {
        obs_log(LOG_DEBUG, "vertical_output_stop: NDI Vertical Output '%s' not running", QT_TO_UTF8(vctx.ndi_name));
    }
    obs_log(LOG_DEBUG, "-vertical_output_stop()");
}

void vertical_output_start()
{
	obs_log(LOG_DEBUG, "+vertical_output_start()");
	if (!vctx.output) {
		obs_log(LOG_ERROR, "ERR-451 - NDI Vertical Output not initialized");
		obs_log(LOG_DEBUG, "vertical_output_start: not initialized");
		obs_log(LOG_DEBUG, "-vertical_output_start()");
		return;
	}

	if (vctx.is_running) {
		vertical_output_stop();
	}

    // Acquire vertical canvas video via Aitum vendor proc or canvas API; else
    // fall back to main program video.
    video_t *video = nullptr;
    // 1) Vendor proc exposed by Aitum Vertical plugin
    if (!video) {
        video = get_aitum_vertical_video();
        if (video)
            obs_log(LOG_INFO, "Vertical NDI: bound to Aitum video via vendor proc");
    }
#if defined(HAVE_OBS_CANVAS_API)
    if (!video) {
        obs_canvas_t *canvas = find_aitum_vertical_canvas();
        if (canvas) {
            video = obs_canvas_get_video(canvas);
            obs_canvas_release(canvas);
            if (video)
                obs_log(LOG_INFO, "Vertical NDI: bound to Aitum canvas via canvas API");
        }
    }
#endif
    if (!video)
    {
        video = obs_get_video();
        obs_log(LOG_INFO, "Vertical NDI: bound to main program video");
    }
    obs_output_set_media(vctx.output, video, obs_get_audio());

	vctx.is_running = obs_output_start(vctx.output);
	if (vctx.is_running) {
		vctx.last_error = QString("");
		obs_log(LOG_DEBUG, "vertical_output_start: started '%s'", QT_TO_UTF8(vctx.ndi_name));
	} else {
		vctx.last_error = obs_output_get_last_error(vctx.output);
		obs_log(LOG_ERROR, "ERR-450 - Failed to start NDI Vertical Output '%s'; error='%s'",
			QT_TO_UTF8(vctx.ndi_name), QT_TO_UTF8(vctx.last_error));
		obs_output_stop(vctx.output);
	}

	obs_log(LOG_DEBUG, "-vertical_output_start()");
}

bool vertical_output_is_supported()
{
	// Same mechanism as main output: attempt to create and start an output with a random name
	obs_log(LOG_DEBUG, "+vertical_output_is_supported()");
	bool supported = true;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "ndi_name", "NDI Vertical Support Test");
	obs_data_set_string(settings, "ndi_groups", "DistroAV Config");

	obs_output_t *out = obs_output_create("ndi_output", "NDI Vertical Output", settings, nullptr);
	obs_data_release(settings);
	if (out) {
		if (!obs_output_start(out)) {
			supported = false;
		}
		obs_output_stop(out);
		obs_output_release(out);
	} else {
		supported = false;
	}
	obs_log(LOG_DEBUG, "-vertical_output_is_supported()");
	return supported;
}

void vertical_output_deinit()
{
    obs_log(LOG_DEBUG, "+vertical_output_deinit()");
    if (vctx.output) {
        vertical_output_stop();
        // Ensure media is cleared even if stop was a no-op
        obs_output_set_media(vctx.output, nullptr, nullptr);
        auto sh = obs_output_get_signal_handler(vctx.output);
        signal_handler_disconnect(sh, "start", on_vertical_output_started, nullptr);
        signal_handler_disconnect(sh, "stop", on_vertical_output_stopped, nullptr);
        obs_output_release(vctx.output);
        vctx.output = nullptr;
        vctx.ndi_name.clear();
        vctx.ndi_groups.clear();
    }
    obs_log(LOG_DEBUG, "-vertical_output_deinit()");
}

void vertical_output_init()
{
	obs_log(LOG_DEBUG, "+vertical_output_init()");
	auto config = Config::Current();
	if (!config->VerticalOutputEnabled || config->VerticalOutputName.isEmpty()) {
		vertical_output_deinit();
		obs_log(LOG_DEBUG, "-vertical_output_init(): disabled or name empty");
		return;
	}

	vertical_output_deinit();

	obs_log(LOG_DEBUG, "vertical_output_init: creating NDI Vertical Output '%s'",
		QT_TO_UTF8(config->VerticalOutputName));
	obs_data_t *output_settings = obs_data_create();
	obs_data_set_string(output_settings, "ndi_name", QT_TO_UTF8(config->VerticalOutputName));
	obs_data_set_string(output_settings, "ndi_groups", QT_TO_UTF8(config->VerticalOutputGroups));

	// Vertical output uses audio from main by default
	obs_data_set_bool(output_settings, "uses_audio", true);

	vctx.output = obs_output_create("ndi_output", "NDI Vertical Output", output_settings, nullptr);
	obs_data_release(output_settings);
    if (vctx.output) {
        auto sh = obs_output_get_signal_handler(vctx.output);
        signal_handler_connect(sh, "start", on_vertical_output_started, nullptr);
        signal_handler_connect(sh, "stop", on_vertical_output_stopped, nullptr);
        vctx.ndi_name = config->VerticalOutputName;
        vctx.ndi_groups = config->VerticalOutputGroups;
        vertical_output_start();
    } else {
        obs_log(LOG_ERROR, "ERR-452 - Failed to create NDI Vertical Output '%s'",
                QT_TO_UTF8(config->VerticalOutputName));
    }
	obs_log(LOG_DEBUG, "-vertical_output_init()");
}
