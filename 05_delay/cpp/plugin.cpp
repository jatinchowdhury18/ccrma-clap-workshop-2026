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

    float read (int delay_samples)
    {
        // @WORKSHOP: right now we're reading with no interpolation,
        // but here is where you would apply interpolation...

        // define read pointer relative to write pointer
        auto read_pointer = write_pointer + delay_samples;
        read_pointer = read_pointer >= max_delay_samples ? read_pointer - max_delay_samples
                                                         : read_pointer;
        // read at read pointer
        return delay_buffer.data()[read_pointer];
    }
};

struct My_Plugin : Plugin
{
    float fs {};

    // parameter handles
    float delay_time_ms { 100.0f };
    float dry_wet { 0.5f };

    // DSP state
    // @WORKSHOP: fill in required processing state
    Delay delay[2] {};

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
        return 2;
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
        else
            return false;
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
    {
        int written;
        if (paramId == 0) {
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f ms", value);
        } else if (paramId == 1) {
            written = std::snprintf(display, static_cast<std::size_t>(size), "%.2f%%", value);
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

        const uint32_t nchannels = proc->audio_inputs[0].channel_count;
        const uint32_t nframes   = proc->frames_count;

        for (uint32_t ch = 0; ch < nchannels; ++ch)
        {
            for (uint32_t n = 0; n < nframes; ++n)
            {
                const auto input = proc->audio_inputs[0].data32[ch][n];

                // read from delay line
                const auto delay_out = delay[ch].read (delay_time_samples);

                // write to delay line
                const auto delay_in = input;
                delay[ch].write (delay_in);

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
