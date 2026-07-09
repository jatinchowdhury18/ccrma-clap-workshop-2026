#include <cstring>
#include <cstdio>
#include <cmath>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

#include "editor.h"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

static float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

struct My_Plugin : Plugin
{
    float fs {};

    float gain_db  { 0.0f }; // dB
    float dummy_01 { 0.5f }; // this is just to show how you WOULD extend it

    // UI communication (message structs and queue types live in editor.h)
    moodycamel::ReaderWriterQueue<To_UI>   to_ui_q   { 256 }; //  256 = capacity
    moodycamel::ReaderWriterQueue<From_UI> from_ui_q { 256 };
    
    // pointer to the editor. Is null when there is no editor, like before the
    // user has opened the plugin GUI in their device chain, so you should be
    // prepared to process audio without the GUI active, and always null-check...
    // a plugin ideally should work headlessly
    My_Editor    *editor    { nullptr }; 

    inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    inline static const clap_plugin_descriptor_t desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = BUNDLE_ID,
        .name         = "GUI Plugin",
        .vendor       = "CCRMA CLAP WORKSHOP",
        .url          = "",
        .manual_url   = "",
        .support_url  = "",
        .version      = "1.0.0",
        .description  = "A gain plugin with a hand-drawn Visage GUI.",
        .features     = features,
    };

    explicit My_Plugin(const clap_host_t *host) : Plugin(&desc, host) {}

    ////////////////////////
    // clap_plugin_params //
    ////////////////////////
    bool implementsParams() const noexcept override { return true; }

    uint32_t paramsCount() const noexcept override {
        return 2;
    }

    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (paramIndex == 0)
        {
            info->id    = 0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Gain", CLAP_NAME_SIZE - 1);
            info->min_value     = -60.0;
            info->max_value     =   6.0;
            info->default_value =   0.0;
        }
        else if (paramIndex == 1)
        {
            info->id    = 1;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Dummy", CLAP_NAME_SIZE - 1);
            info->min_value     = 0.0;
            info->max_value     = 1.0;
            info->default_value = 0.5;
        }
        else
        {
            return false;
        }
        return true;
    }

    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        if (paramId == 0)
            *value = (double) gain_db;
        else if (paramId == 1)
            *value = (double) dummy_01;
        else
            return false;
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        int written;
        if (paramId == 0)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f dB", value);
        else
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.3f", value);

        if (written < 0)
            return false;
        if (static_cast<uint32_t>(written) >= size)
            return false;
        return true;
    }

    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        char *end = nullptr;
        errno = 0;
        double v = std::strtod(display, &end);
        if (end == display) return false;
        if (errno != 0)     return false;
        *value = v;
        return true;
    }

    // @WORKSHOP: when audio is NOT running the host delivers/collects param events
    // here (on the main thread) instead of in process(). The host never
    // calls this while process() is running.
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        const uint32_t nev = in->size(in);
        for (uint32_t ev_index = 0; ev_index < nev; ++ev_index)
            handleEvent(in->get(in, ev_index));

        handle_ui_events(out);
    }

    /////////////////////////////
    // clap_plugin_audio_ports //
    /////////////////////////////

    bool implementsAudioPorts() const noexcept override { return true; }

    uint32_t audioPortsCount(bool isInput) const noexcept override { return 1; }

    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const noexcept override {
        if (index > 0) return false;
        info->id = 0;
        snprintf(info->name, sizeof(info->name), "%s", "My Port Name");
        info->channel_count = 2;
        info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type     = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    /////////////////////
    // clap_plugin_gui //
    /////////////////////

    bool implementsGui() const noexcept override { return true; }

    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override {
        if (isFloating) return false; // embedded in the host window only
#if defined(IS_MAC)
        return !std::strcmp(api, CLAP_WINDOW_API_COCOA);
#elif defined(IS_WIN)
        return !std::strcmp(api, CLAP_WINDOW_API_WIN32);
#else
        return !std::strcmp(api, CLAP_WINDOW_API_X11);
#endif
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override {
        // main thread...
        editor = new My_Editor(to_ui_q, from_ui_q,
                               [this] { if (_host.canUseParams()) _host.paramsRequestFlush(); });
        // on init, read the audio thread's values directly. Technically a data
        // race, practically probably ok :-)
        editor->gain_slider.set_value(gain_db);
        editor->dummy_slider.set_value(dummy_01);
        editor->setWindowDimensions(GUI_WIDTH, GUI_HEIGHT); // REQUIRED before show() — visage asserts nonzero size
        return true;
    }

    void guiDestroy() noexcept override {
        My_Editor *e = editor;
        editor = nullptr; // the audio thread checks this before enqueueing to to_ui_q,
        delete e;        
    }

    bool guiSetParent(const clap_window *window) noexcept override {
        editor->show(window->ptr); // ptr = the host's native window handle 
        return true;
    }

    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override {
#if defined(IS_MAC)
        *width  = GUI_WIDTH; // cocoa sizes are logical points
        *height = GUI_HEIGHT;
#else
        // win32/x11 sizes are physical pixels; visage knows the scale once created
        *width  = editor ? (uint32_t) editor->nativeWidth()  : GUI_WIDTH;
        *height = editor ? (uint32_t) editor->nativeHeight() : GUI_HEIGHT;
#endif
        return true;
    }

    bool guiCanResize() const noexcept override { return false; } // fixed size = minimal surface
    bool guiSetScale(double scale) noexcept override { return false; } // visage handles DPI itself
    bool guiShow() noexcept override { return true; }
    bool guiHide() noexcept override { return true; }

    /////////////////
    // clap_plugin //
    /////////////////

    bool init() noexcept override { return Plugin::init(); }

    bool activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept override {
        fs = (float) sampleRate;
        return true;
    }

    void handleEvent(const clap_event_header_t *hdr) noexcept {
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_PARAM_VALUE: {
                    const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                    if (ev->param_id == 0)
                        gain_db = (float) ev->value;
                    else if (ev->param_id == 1)
                        dummy_01 = (float) ev->value;
                    // @WORKSHOP: report host-driven changes (automation, generic UI)
                    // to our GUI 
                    if (editor)
                        to_ui_q.try_enqueue({ev->param_id, ev->value});
                    break;
                }
                case CLAP_EVENT_TRANSPORT: {
                    // TODO: handle transport event
                    break;
                }
            }
        }
    }

    // This function drains the UI queue 
    void handle_ui_events(const clap_output_events_t *out) noexcept
    {
        From_UI msg;
        while (from_ui_q.try_dequeue(msg)) {
            switch (msg.type) {
                case From_UI::Begin_Edit:
                case From_UI::End_Edit: {
                    clap_event_param_gesture_t ev = {};
                    ev.header.size     = sizeof(ev);
                    ev.header.type     = msg.type == From_UI::Begin_Edit
                                             ? (uint16_t) CLAP_EVENT_PARAM_GESTURE_BEGIN
                                             : (uint16_t) CLAP_EVENT_PARAM_GESTURE_END;
                    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    ev.param_id        = msg.param_id;
                    out->try_push(out, &ev.header);
                    break;
                }
                case From_UI::Adjust_Value: {
                    if (msg.param_id == 0)
                        gain_db = (float) msg.value;
                    else if (msg.param_id == 1)
                        dummy_01 = (float) msg.value;

                    clap_event_param_value_t ev = {};
                    ev.header.size     = sizeof(ev);
                    ev.header.type     = (uint16_t) CLAP_EVENT_PARAM_VALUE;
                    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    ev.param_id        = msg.param_id;
                    ev.value           = msg.value;
                    ev.note_id    = -1; // -1 = wildcard, "not targeting one voice"
                    ev.port_index = -1;
                    ev.channel    = -1;
                    ev.key        = -1;
                    out->try_push(out, &ev.header);
                    break;
                }
            }
        }
    }

    clap_process_status process(const clap_process_t *proc) noexcept override {
        // Apply any pending changes from the GUI before this block
        handle_ui_events(proc->out_events);

        // Process incoming events
        const uint32_t nev = proc->in_events->size(proc->in_events);
        for (uint32_t ev_index = 0; ev_index < nev; ++ev_index) {
            const clap_event_header_t *hdr = proc->in_events->get(proc->in_events, ev_index);
            handleEvent(hdr);
        }

        // @WORKSHOP: the gain jumps to its new value between blocks — you may hear
        // zipper noise when dragging. Smoothing it is a great exercise.
        const float g = db_to_gain(gain_db);

        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;

        for (uint32_t ch = 0; ch < nchannels; ++ch)
            for (uint32_t n = 0; n < nframes; ++n)
                proc->audio_outputs[0].data32[ch][n] = proc->audio_inputs[0].data32[ch][n] * g;

        return CLAP_PROCESS_CONTINUE;
    }
};

/////////////////////////
// clap_plugin_factory //
/////////////////////////

static uint32_t plugin_factory_get_plugin_count(const clap_plugin_factory *f) { return 1; }

static const clap_plugin_descriptor_t *
plugin_factory_get_plugin_descriptor(const clap_plugin_factory *f, uint32_t index)
{
    return &My_Plugin::desc;
}

static const clap_plugin_t *plugin_factory_create_plugin(const clap_plugin_factory *f,
                                                         const clap_host_t         *host,
                                                         const char                *plugin_id)
{
    if (!clap_version_is_compatible(host->clap_version))
        return nullptr;
    if (strcmp(plugin_id, My_Plugin::desc.id) != 0)
        return nullptr;
    auto p = new My_Plugin{host};
    return p->clapPlugin();
}

static const clap_plugin_factory_t s_plugin_factory = {
    .get_plugin_count      = plugin_factory_get_plugin_count,
    .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
    .create_plugin         = plugin_factory_create_plugin,
};

////////////////
// clap_entry //
////////////////

static bool entry_init(const char *plugin_path) { return true; }
static void entry_deinit(void) {}

static const void *entry_get_factory(const char *factory_id) {
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID))
        return &s_plugin_factory;
    return nullptr;
}

// This symbol will be resolved by the host
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
