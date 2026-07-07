// This file demonstrates how to wire a CLAP plugin using the C++ glue layer:
// https://github.com/free-audio/clap-helpers/blob/main/include/clap/helpers/plugin.hh
//
// It is functionally equivalent to the C version in ../c/plugin.c

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

// Crossover frequency: mapped logarithmically from [0, 1] to [100, 10000] Hz.
// These helpers are provided — the @WORKSHOP focus is on the distortion DSP below.
static constexpr float CROSSOVER_MIN = 100.0f;
static constexpr float CROSSOVER_MAX = 10'000.0f;

static float map_crossover_param(float param01) {
    return CROSSOVER_MIN * std::pow(CROSSOVER_MAX / CROSSOVER_MIN, param01);
}
static float map_crossover_param_inverse(float freq_hz) {
    return std::log(freq_hz / CROSSOVER_MIN) / std::log(CROSSOVER_MAX / CROSSOVER_MIN);
}
struct My_Plugin : Plugin
{
    // @WORKSHOP:
    // We can add member variables here, like:
    // - data that our plugin will use for processing audio, etc.
    // Note: host extension handles are managed by the base class via _host
    float fs {};

    // parameter handles (stored in native units)
    float low_drive_db      { 0.0f }; // dB
    float high_drive_db     { 0.0f }; // dB
    float crossover_freq_01 { 0.5f };
    float output_gain_db    { 0.0f }; // dB

    // @WORKSHOP: add DSP state here

    inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    inline static const clap_plugin_descriptor_t desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = BUNDLE_ID,
        .name         = "Multiband Distortion Plugin",
        .vendor       = "CCRMA CLAP WORKSHOP",
        .url          = "",
        .manual_url   = "",
        .support_url  = "",
        .version      = "1.0.0",
        .description  = "A multiband distortion plugin.",
        .features     = features,
    };

    explicit My_Plugin(const clap_host_t *host) : Plugin(&desc, host) {}

    ////////////////////////
    // clap_plugin_params //
    ////////////////////////
    bool implementsParams() const noexcept override {
        // @WORKSHOP: return true to enable the params extension
        return false;
    }
    uint32_t paramsCount() const noexcept override {
        // @WORKSHOP: return the number of parameters
        return 0;
    }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        // @WORKSHOP: implement parameter info
        // Parameters:
        //   0  Low Drive       dB   [-40, 40]  default 0
        //   1  High Drive      dB   [-40, 40]  default 0
        //   2  Crossover Freq  0-1  [0,   1]   default 0.5
        //   3  Output Gain     dB   [-40, 40]  default 0
        // Hint: use CLAP_PARAM_IS_AUTOMATABLE for all params
        return false;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        // @WORKSHOP: implement parameter value
        return false;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        int written;
        if (paramId == 0 || paramId == 1 || paramId == 3)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f dB", value);
        else // paramId == 2: crossover frequency
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f Hz",
                                    (double) map_crossover_param((float) value));

        if (written < 0)                          return false;
        if (static_cast<uint32_t>(written) >= size) return false;
        return true;
    }

    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        char *end = nullptr;
        errno = 0;
        double v = std::strtod(display, &end);
        if (end == display) return false;
        if (errno != 0)     return false;

        if (paramId == 2)
            *value = (double) map_crossover_param_inverse((float) v);
        else
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
        // @WORKSHOP: initialization
        fs = (float) sampleRate;
        return true;
    }

    void handleEvent(const clap_event_header_t *hdr) noexcept {
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_PARAM_VALUE: {
                    const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                    // @WORKSHOP: handle parameter change
                    break;
                }
                case CLAP_EVENT_TRANSPORT: {
                    const auto *ev = reinterpret_cast<const clap_event_transport_t *>(hdr);
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

        // Process audio stream
        // @WORKSHOP: audio processing here!
        // Hint: pre-compute your crossover filter coefficients here (once, before the
        // audio loop), then run the per-sample DSP loop below.
        // Convert parameters to usable values:
        //   const float crossover_hz = map_crossover_param(crossover_freq_01);
        //   const float low_drive    = db_to_gain(low_drive_db);
        //   const float high_drive   = db_to_gain(high_drive_db);
        //   const float output_gain  = db_to_gain(output_gain_db);
        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;
        for (uint32_t ch = 0; ch < nchannels; ++ch) {
            for (uint32_t n = 0; n < nframes; ++n) {
                const float input = proc->audio_inputs[0].data32[ch][n];

                // @WORKSHOP: replace the passthrough below with multiband distortion DSP
                const float output = input;

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
    if (!clap_version_is_compatible(host->clap_version)) return nullptr;
    if (strcmp(plugin_id, My_Plugin::desc.id) != 0)      return nullptr;
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
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &s_plugin_factory;
    return nullptr;
}

// This symbol will be resolved by the host
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
