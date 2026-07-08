#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

struct Delay
{
    int max_delay_samples {};
    int write_pointer {};
    std::vector<float> delay_buffer {};

    void initialize (int max_delay)
    {
        max_delay_samples = max_delay;
        write_pointer = 0;
        delay_buffer.resize((size_t) max_delay_samples, 0.0f);
    }

    void write (float x)
    {
        // write value to delay buffer
        delay_buffer.data()[write_pointer] = x;

        // decrement write pointer
        write_pointer += max_delay_samples - 1;
        write_pointer = write_pointer >= max_delay_samples ? write_pointer - max_delay_samples
                                                           : write_pointer;
    }

    float read (float delay_samples)
    {
        // @WORKSHOP: 3rd-order Lagrange interpolation

        delay_samples = std::clamp (delay_samples, 0.0f, (float) max_delay_samples - 4.0f);

        auto rp1 = write_pointer + (int) delay_samples;
        auto rp2 = rp1 + 1;
        auto rp3 = rp1 + 2;
        auto rp4 = rp1 + 3;

        rp1 = rp1 >= max_delay_samples ? rp1 - max_delay_samples : rp1;
        rp2 = rp2 >= max_delay_samples ? rp2 - max_delay_samples : rp2;
        rp3 = rp3 >= max_delay_samples ? rp3 - max_delay_samples : rp3;
        rp4 = rp4 >= max_delay_samples ? rp4 - max_delay_samples : rp4;

        auto value1 = delay_buffer.data()[rp1];
        auto value2 = delay_buffer.data()[rp2];
        auto value3 = delay_buffer.data()[rp3];
        auto value4 = delay_buffer.data()[rp4];

        const auto delay_frac = delay_samples - static_cast<float> ((int) delay_samples);
        auto d1 = delay_frac - 1.0f;
        auto d2 = delay_frac - 2.0f;
        auto d3 = delay_frac - 3.0f;

        auto c1 = -d1 * d2 * d3 * (1.0f / 6.0f);
        auto c2 = d2 * d3 * 0.5f;
        auto c3 = -d1 * d3 * 0.5f;
        auto c4 = d1 * d2 * (1.0f / 6.0f);

        return value1 * c1 + delay_frac * (value2 * c2 + value3 * c3 + value4 * c4);
    }
};

struct My_Plugin : Plugin
{
    float fs {};

    // parameter handles
    float delay_time_ms { 100.0f };
    float dry_wet { 0.5f };
    float feedback_01 { 0.25f };
    float attack_ms { 10.0f }; // ms
    float release_ms { 100.0f }; // ms
    float mod_freq_hz { 10.0f };
    float duck_01 { 0.0f };

    // DSP state
    // @WORKSHOP: fill in required processing state
    Delay delay[2] {};
    float mod_phi[2] {};

    float mod_level_state[2]  = {};
    float duck_level_state[2]  = {};

    inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
    inline static const clap_plugin_descriptor_t desc = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = BUNDLE_ID,
        .name         = "Delay Plugin",
        .vendor       = "CCRMA CLAP WORKSHOP",
        .url          = "",
        .manual_url   = "",
        .support_url  = "",
        .version      = "1.0.0",
        .description  = "A delay plugin.",
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
            std::strncpy(info->name, "Delay Time", CLAP_NAME_SIZE - 1);
            info->min_value     = 1;
            info->max_value     = 1000;
            info->default_value = 100;
        }
        else if (paramIndex == 1)
        {
            info->id    = 1;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Dry/Wet", CLAP_NAME_SIZE - 1);
            info->min_value     = 0;
            info->max_value     = 1;
            info->default_value = 0.5;
        }
        else if (paramIndex == 2)
        {
            info->id    = 2;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Feedback", CLAP_NAME_SIZE - 1);
            info->min_value     = 0;
            info->max_value     = 1;
            info->default_value = 0.25;
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
            std::strncpy(info->name, "Mod. Freq", CLAP_NAME_SIZE - 1);
            info->min_value     =   0.1;
            info->max_value     =  20.0;
            info->default_value =   5.0;
        }
        else if (paramIndex == 6)
        {
            info->id    = 6;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            std::strncpy(info->name, "Duck", CLAP_NAME_SIZE - 1);
            info->min_value     = 0.0;
            info->max_value     = 1.0;
            info->default_value = 0.0;
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
            *value = (double) delay_time_ms;
        else if (paramId == 1)
            *value = (double) dry_wet;
        else if (paramId == 2)
            *value = (double) feedback_01;
        else if (paramId == 3)
            *value = (double) attack_ms;
        else if (paramId == 4)
            *value = (double) release_ms;
        else if (paramId == 5)
            *value = (double) mod_freq_hz;
        else if (paramId == 6)
            *value = (double) duck_01;
        else
            return false;
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        int written;
        if (paramId == 0) { // delay time
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f ms", value);
        } else if (paramId == 1 || paramId == 2 || paramId == 6) { // dry/wet, feedback, ducking
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.2f%%", value);
        } else if (paramId == 3 || paramId == 4) { // attack, release
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f ms", value);
        } else if (paramId == 5) { // mod freq
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.2f Hz", value);
        } else {
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.3f", value);
        }

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
        // @WORKSHOP: fill in activation code
        fs = (float) sampleRate;

        const auto max_delay_samples = (int) std::ceil (sampleRate); // max delay time: 1 second
        for (auto& d : delay)
            d.initialize (max_delay_samples);

        return true;
    }

    void handleEvent(const clap_event_header_t *hdr) noexcept {
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            switch (hdr->type) {
                case CLAP_EVENT_PARAM_VALUE: {
                    const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                    // @WORKSHOP: handle parameter change
                    if (ev->param_id == 0)
                        delay_time_ms = (float) ev->value;
                    else if (ev->param_id == 1)
                        dry_wet = (float) ev->value;
                    else if (ev->param_id == 2)
                        feedback_01 = (float) ev->value;
                    else if (ev->param_id == 3)
                        attack_ms = (float) ev->value;
                    else if (ev->param_id == 4)
                        release_ms = (float) ev->value;
                    else if (ev->param_id == 5)
                        mod_freq_hz = (float) ev->value;
                    else if (ev->param_id == 6)
                        duck_01 = (float) ev->value;
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
        const auto delay_time_samples = (int) std::round (delay_time_ms * 0.001f * fs);
        const auto wet_gain = std::sqrt (dry_wet);
        const auto dry_gain = std::sqrt (1.0f - dry_wet);
        const auto fb_gain = feedback_01 * 0.995f;

        const auto mod_attack_b0  = 1.0f - std::exp(-1.0f / (fs * attack_ms  * 0.001f));
        const auto mod_release_b0 = 1.0f - std::exp(-1.0f / (fs * release_ms * 0.001f));
        const auto mod_dp = 2 * (float) M_PI * mod_freq_hz / fs;
        const auto mod_depth_ms = 10.0f;
        const auto mod_depth_samples = mod_depth_ms * 0.001f * fs;

        const auto duck_attack_ms = 5.0f;
        const auto duck_release_ms = 50.0f;
        const auto duck_attack_b0  = 1.0f - std::exp(-1.0f / (fs * duck_attack_ms  * 0.001f));
        const auto duck_release_b0 = 1.0f - std::exp(-1.0f / (fs * duck_release_ms * 0.001f));
        const auto duck_threshold = 0.064f; // ~ -24 dB
        const auto duck_knee = 4.0f; // ~ 12 dB
        const auto duck_ratio = std::pow (10.0f, duck_01);
        const float half_knee  = std::sqrt(duck_knee);
        const float knee_lower = duck_threshold / half_knee;
        const float knee_upper = duck_threshold * half_knee;
        const float log_thresh = std::log(duck_threshold);
        const float log_knee   = std::log(duck_knee);
        const float rho        = (1.0f / duck_ratio) - 1.0f;

        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;

        const auto saturate = [] (float x)
        {
            return x / std::sqrt (x * x + 1);
        };

        for (uint32_t ch = 0; ch < nchannels; ++ch)
        {
            for (uint32_t n = 0; n < nframes; ++n)
            {
                const auto input = proc->audio_inputs[0].data32[ch][n];

                // update envelope follower
                const auto level = std::abs (input);
                const auto b0    = (level > mod_level_state[ch]) ? mod_attack_b0 : mod_release_b0;
                mod_level_state[ch] += b0 * (level - mod_level_state[ch]);

                // modulate delay time
                mod_phi[ch] += mod_dp;
                if (mod_phi[ch] > 2 * (float) M_PI)
                    mod_phi[ch] -= 2 * (float) M_PI;
                const auto mod_amount_samples = mod_level_state[ch] * mod_depth_samples * std::sin(mod_phi[ch]);

                // read from delay line
                auto delay_out = delay[ch].read (delay_time_samples + mod_amount_samples);

                // write to delay line
                const auto delay_in = input + saturate (fb_gain * delay_out);
                delay[ch].write (delay_in);

                // duck delay out
                {
                    // re-use input "level" from above
                    const auto b0    = (level > duck_level_state[ch]) ? duck_attack_b0 : duck_release_b0;
                    duck_level_state[ch] += b0 * (level - duck_level_state[ch]);

                    float gain;
                    if (duck_level_state[ch] <= knee_lower)
                    {
                        gain = 1.0f;
                    }
                    else if (duck_level_state[ch] >= knee_upper)
                    {
                        gain = std::pow(duck_level_state[ch] / duck_threshold, rho);
                    }
                    else
                    {
                        static constexpr float _8x_log10 = (float)(8.0 * 2.30258509299404568);
                        const auto log_x = std::log(duck_level_state[ch]);
                        const auto root  = log_knee + 2.0f * (log_x - log_thresh);
                        gain = std::pow(10.0f, ((1.0f - duck_ratio) * root * root) / (_8x_log10 * duck_ratio * log_knee));
                    }
                    delay_out *= gain;
                }

                // dry/wet mix
                const auto output = wet_gain * delay_out + dry_gain * input;

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
