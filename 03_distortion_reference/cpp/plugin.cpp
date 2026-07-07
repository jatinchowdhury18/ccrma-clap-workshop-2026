// This file demonstrates how to wire a CLAP plugin using the C++ glue layer:
// https://github.com/free-audio/clap-helpers/blob/main/include/clap/helpers/plugin.hh
//
// It is functionally equivalent to the Jai version in ../jai/plugin.jai

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

// Crossover frequency: mapped logarithmically from [0, 1] to [100, 10000] Hz
static constexpr float CROSSOVER_MIN = 100.0f;
static constexpr float CROSSOVER_MAX = 10'000.0f;

static float map_crossover_param(float param01) {
    return CROSSOVER_MIN * std::pow(CROSSOVER_MAX / CROSSOVER_MIN, param01);
}
static float map_crossover_param_inverse(float freq_hz) {
    return std::log(freq_hz / CROSSOVER_MIN) / std::log(CROSSOVER_MAX / CROSSOVER_MIN);
}
static float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

struct My_Plugin : Plugin
{
    float fs {};

    // parameter handles
    float low_drive_db      { 0.0f }; // dB
    float high_drive_db     { 0.0f }; // dB
    float crossover_freq_01 { 0.5f };
    float output_gain_db    { 0.0f }; // dB

    // @WORKSHOP: add DSP state here
    struct CrossoverFilterState {
        float stage1_ic1eq     = 0.0f;
        float stage1_ic2eq     = 0.0f;
        float stage2_lpf_ic1eq = 0.0f;
        float stage2_lpf_ic2eq = 0.0f;
        float stage2_hpf_ic1eq = 0.0f;
        float stage2_hpf_ic2eq = 0.0f;
    };
    CrossoverFilterState crossover_state[2]; // max 2 channels (stereo)

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
    bool implementsParams() const noexcept override { return true; }

    uint32_t paramsCount() const noexcept override {
        // @WORKSHOP: add parameters here
        return 4;
    }

    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        // @WORKSHOP: add parameters here
        if (paramIndex == 0)
        {
            info->id    = 0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Low Drive", CLAP_NAME_SIZE - 1);
            info->min_value     = -40.0;
            info->max_value     =  40.0;
            info->default_value =   0.0;
        }
        else if (paramIndex == 1)
        {
            info->id    = 1;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "High Drive", CLAP_NAME_SIZE - 1);
            info->min_value     = -40.0;
            info->max_value     =  40.0;
            info->default_value =   0.0;
        }
        else if (paramIndex == 2)
        {
            info->id    = 2;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Crossover Frequency", CLAP_NAME_SIZE - 1);
            info->min_value     = 0.0;
            info->max_value     = 1.0;
            info->default_value = 0.5;
        }
        else if (paramIndex == 3)
        {
            info->id    = 3;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Output Gain", CLAP_NAME_SIZE - 1);
            info->min_value     = -40.0;
            info->max_value     =  40.0;
            info->default_value =   0.0;
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
            *value = (double) low_drive_db;
        else if (paramId == 1)
            *value = (double) high_drive_db;
        else if (paramId == 2)
            *value = (double) crossover_freq_01;
        else if (paramId == 3)
            *value = (double) output_gain_db;
        else
            return false;
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        int written;
        if (paramId == 0 || paramId == 1 || paramId == 3)
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f dB", value);
        else // paramId == 2: crossover frequency
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f Hz",
                                    (double) map_crossover_param((float) value));

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
                    if (ev->param_id == 0)
                        low_drive_db = (float) ev->value;
                    else if (ev->param_id == 1)
                        high_drive_db = (float) ev->value;
                    else if (ev->param_id == 2)
                        crossover_freq_01 = (float) ev->value;
                    else if (ev->param_id == 3)
                        output_gain_db = (float) ev->value;
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

        // Process audio stream
        // @WORKSHOP: audio processing here!
        const float crossover_hz = map_crossover_param(crossover_freq_01);
        const float low_drive    = db_to_gain(low_drive_db);
        const float high_drive   = db_to_gain(high_drive_db);
        const float output_gain  = db_to_gain(output_gain_db);

        const float w   = (float) M_PI * crossover_hz / fs;
        const float g   = std::tan(w);
        const float k   = std::sqrt(2.0f);
        const float gk  = g + k;
        const float gt0 = 1.0f / (1.0f + g * gk);
        const float gk0 = gk * gt0;
        const float gt1 = g * gt0;
        const float gk1 = g * gk0;
        const float gt2 = g * gt1;

        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;
        for (uint32_t ch = 0; ch < nchannels; ++ch) {
            auto &state = crossover_state[ch];
            for (uint32_t n = 0; n < nframes; ++n) {
                const float input = proc->audio_inputs[0].data32[ch][n];

                // Stage 1: split into low and high bands
                const float t0_1 = input - state.stage1_ic2eq;
                const float v0_1 = gt0 * t0_1 - gk0 * state.stage1_ic1eq;
                const float t1_1 = gt1 * t0_1 - gk1 * state.stage1_ic1eq;
                const float t2_1 = gt2 * t0_1 + gt1 * state.stage1_ic1eq;
                const float v2_1 = t2_1 + state.stage1_ic2eq;
                state.stage1_ic1eq += 2.0f * t1_1;
                state.stage1_ic2eq += 2.0f * t2_1;
                float low_sample  = v2_1;
                float high_sample = v0_1;

                // Stage 2 LP: second-order LP on the low band
                const float t0_lp = low_sample - state.stage2_lpf_ic2eq;
                const float t1_lp = gt1 * t0_lp - gk1 * state.stage2_lpf_ic1eq;
                const float t2_lp = gt2 * t0_lp + gt1 * state.stage2_lpf_ic1eq;
                const float v2_lp = t2_lp + state.stage2_lpf_ic2eq;
                state.stage2_lpf_ic1eq += 2.0f * t1_lp;
                state.stage2_lpf_ic2eq += 2.0f * t2_lp;
                low_sample = v2_lp;

                // Stage 2 HP: second-order HP on the high band
                const float t0_hp = high_sample - state.stage2_hpf_ic2eq;
                const float v0_hp = gt0 * t0_hp - gk0 * state.stage2_hpf_ic1eq;
                const float t1_hp = gt1 * t0_hp - gk1 * state.stage2_hpf_ic1eq;
                const float t2_hp = gt2 * t0_hp + gt1 * state.stage2_hpf_ic1eq;
                state.stage2_hpf_ic1eq += 2.0f * t1_hp;
                state.stage2_hpf_ic2eq += 2.0f * t2_hp;
                high_sample = v0_hp;

                // Distort each band independently
                auto distort = [](float x) { return x / std::sqrt(1.0f + x * x); };
                low_sample  = distort(low_sample  * low_drive);
                high_sample = distort(high_sample * high_drive);

                proc->audio_outputs[0].data32[ch][n] = output_gain * (low_sample + high_sample);
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
