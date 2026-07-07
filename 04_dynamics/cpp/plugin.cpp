#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

static float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

struct My_Plugin : Plugin
{
    float fs {};

    // parameter handles
    float threshold_db      { 0.0f }; // dB
    float ratio             { 4.0f }; // compression ratio
    float knee_db           { 0.0f }; // dB
    float attack_ms         { 10.0f }; // ms
    float release_ms        { 100.0f }; // ms
    float makeup_gain_db    { 0.0f }; // dB
    float crossover_freq_01 { 0.5f };

    // DSP state
    // @WORKSHOP: fill in required processing state

    inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    inline static const clap_plugin_descriptor_t desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = BUNDLE_ID,
        .name         = "Dynamics Plugin",
        .vendor       = "CCRMA CLAP WORKSHOP",
        .url          = "",
        .manual_url   = "",
        .support_url  = "",
        .version      = "1.0.0",
        .description  = "A dynamics plugin.",
        .features     = features,
    };

    explicit My_Plugin(const clap_host_t *host) : Plugin(&desc, host) {}

    ////////////////////////
    // clap_plugin_params //
    ////////////////////////
    bool implementsParams() const noexcept override { return true; }

    uint32_t paramsCount() const noexcept override {
        return 7;
    }

    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        // @WORKSHOP: add parameters here
        if (paramIndex == 0)
        {
            info->id    = 0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Threshold", CLAP_NAME_SIZE - 1);
            info->min_value     = -60.0;
            info->max_value     =   0.0;
            info->default_value =   0.0;
        }
        else if (paramIndex == 1)
        {
            info->id    = 1;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Ratio", CLAP_NAME_SIZE - 1);
            info->min_value     =  1.0;
            info->max_value     = 20.0;
            info->default_value =  4.0;
        }
        else if (paramIndex == 2)
        {
            info->id    = 2;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Knee", CLAP_NAME_SIZE - 1);
            info->min_value     =  0.0;
            info->max_value     = 18.0;
            info->default_value =  0.0;
        }
        else if (paramIndex == 3)
        {
            info->id    = 3;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Attack", CLAP_NAME_SIZE - 1);
            info->min_value     =   1.0;
            info->max_value     = 500.0;
            info->default_value =  10.0;
        }
        else if (paramIndex == 4)
        {
            info->id    = 4;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Release", CLAP_NAME_SIZE - 1);
            info->min_value     =   10.0;
            info->max_value     = 2000.0;
            info->default_value =  100.0;
        }
        else if (paramIndex == 5)
        {
            info->id    = 5;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Makeup Gain", CLAP_NAME_SIZE - 1);
            info->min_value     = -30.0;
            info->max_value     =  30.0;
            info->default_value =   0.0;
        }
        else if (paramIndex == 6)
        {
            info->id    = 6;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Crossover Frequency", CLAP_NAME_SIZE - 1);
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
        // @WORKSHOP: return parameter values
        if (paramId == 0)
            *value = (double) threshold_db;
        else if (paramId == 1)
            *value = (double) ratio;
        else if (paramId == 2)
            *value = (double) knee_db;
        else if (paramId == 3)
            *value = (double) attack_ms;
        else if (paramId == 4)
            *value = (double) release_ms;
        else if (paramId == 5)
            *value = (double) makeup_gain_db;
        else if (paramId == 6)
            *value = (double) crossover_freq_01;
        else
            return false;
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        static constexpr float CROSSOVER_MIN = 100.0f;
        static constexpr float CROSSOVER_MAX = 10000.0f;
        int written;
        if (paramId == 0 || paramId == 2 || paramId == 5)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f dB", value);
        else if (paramId == 1)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f:1", value);
        else if (paramId == 3 || paramId == 4)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f ms", value);
        else if (paramId == 6) {
            float freq_hz = CROSSOVER_MIN * std::pow(CROSSOVER_MAX / CROSSOVER_MIN, (float)value);
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f Hz", (double)freq_hz);
        } else
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

    /////////////////
    // clap_plugin //
    /////////////////

    bool init() noexcept override { return Plugin::init(); }

    bool activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept override {
        fs = (float) sampleRate;
        // @WORKSHOP: fill in activation code
        return true;
    }

    void handleEvent(const clap_event_header_t *hdr) noexcept {
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_PARAM_VALUE: {
                    const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                    // @WORKSHOP: handle parameter change
                    if (ev->param_id == 0)
                        threshold_db = (float) ev->value;
                    else if (ev->param_id == 1)
                        ratio = (float) ev->value;
                    else if (ev->param_id == 2)
                        knee_db = (float) ev->value;
                    else if (ev->param_id == 3)
                        attack_ms = (float) ev->value;
                    else if (ev->param_id == 4)
                        release_ms = (float) ev->value;
                    else if (ev->param_id == 5)
                        makeup_gain_db = (float) ev->value;
                    else if (ev->param_id == 6)
                        crossover_freq_01 = (float) ev->value;
                    break;
                }
                case CLAP_EVENT_TRANSPORT: {
                    // TODO: handle transport event
                    break;
                }
            }
        }
    }

    clap_process_status process(const clap_process_t *proc) noexcept override {
        // Process incoming events
        const uint32_t nev = proc->in_events->size(proc->in_events);
        for (uint32_t ev_index = 0; ev_index < nev; ++ev_index) {
            const clap_event_header_t *hdr = proc->in_events->get(proc->in_events, ev_index);
            handleEvent(hdr);
        }

        // @WORKSHOP: fill in processing code here

        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;

        for (uint32_t ch = 0; ch < nchannels; ++ch)
        {
            for (uint32_t n = 0; n < nframes; ++n)
            {
                const float input = proc->audio_inputs[0].data32[ch][n];
                const auto output = input;
                proc->audio_outputs[0].data32[ch][n] = output;
            }
        }

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
