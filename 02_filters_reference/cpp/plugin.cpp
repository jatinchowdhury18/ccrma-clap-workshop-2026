#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

// Frequency parameter: mapped logarithmically from [0, 1] to [100, 10000] Hz
static constexpr float CUTOFF_MIN = 100.0f;
static constexpr float CUTOFF_MAX = 10'000.0f;

static float map_frequency_param(float param01) {
   return CUTOFF_MIN * std::pow(CUTOFF_MAX / CUTOFF_MIN, param01);
}
static float map_frequency_param_inverse(float freq_hz) {
   return std::log(freq_hz / CUTOFF_MIN) / std::log(CUTOFF_MAX / CUTOFF_MIN);
}

struct My_Plugin : Plugin
{
   // @WORKSHOP: add data members here
   float fs {};

   // parameter handles
   float freq_01     { 0.5f };
   float freq_mod    { 0.0f };
   float damping     { 0.2f };
   float damping_mod { 0.0f };

   // DSP state (one per channel)
   struct FilterState {
      float ic1eq = 0.0f;
      float ic2eq = 0.0f;
   };
   FilterState filter_state[2]; // max 2 channels (stereo)

   inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
   inline static const clap_plugin_descriptor_t desc = {
      .clap_version = CLAP_VERSION_INIT,
      .id           = BUNDLE_ID,
      .name         = "Filter Plugin",
      .vendor       = "CCRMA CLAP WORKSHOP",
      .url          = "",
      .manual_url   = "",
      .support_url  = "",
      .version      = "1.4.2",
      .description  = "A filter plugin.",
      .features     = features,
   };

   explicit My_Plugin(const clap_host_t *host) : Plugin(&desc, host) {}

   ////////////////////////
   // clap_plugin_params //
   ////////////////////////
   bool implementsParams() const noexcept override {
      // @WORKSHOP: return true to enable the params extension
      return true;
   }
   uint32_t paramsCount() const noexcept override {
      // @WORKSHOP: add parameters here
      return 2;
   }
   bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
   {
       // @WORKSHOP: add parameters here
       if (paramIndex == 0)
       {
           info->id    = 0;
           info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
           std::strncpy(info->name, "Frequency", CLAP_NAME_SIZE - 1);
           info->min_value     = 0.0;
           info->max_value     = 1.0;
           info->default_value = 0.5;
       }
       else if (paramIndex == 1)
       {
           info->id    = 1;
           info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
           std::strncpy(info->name, "Damping", CLAP_NAME_SIZE - 1);
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
       // In this code, paramId and paramIndex are always the same.
       if (paramId == 0) // cutoff param
           *value = (double) freq_01;
       else if (paramId == 1) // damping param
           *value = (double) damping;
       else
           return false;
       return true;
   }

   bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
   {
       int written;
       if (paramId == 0)
           written = std::snprintf(display, static_cast<std::size_t>(size), "%.1f Hz",
                                   (double) map_frequency_param((float) value));
       else
           written = std::snprintf(display, static_cast<std::size_t>(size), "%.2f", value);

       if (written < 0)
           return false; // encoding/format error

       if (static_cast<uint32_t>(written) >= size)
           return false; // output was truncated

       return true;
   }

   bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
   {
       char *end = nullptr;
       errno = 0;
       double v = std::strtod(display, &end);

       if (end == display)
           return false; // No digits parsed at all

       if (errno != 0)
           return false; // decoding error

       if (paramId == 0)
           *value = (double) map_frequency_param_inverse((float) v);
       else
           *value = v;
       return true;
   }

   /////////////////////////////
   // clap_plugin_audio_ports //
   /////////////////////////////

   bool implementsAudioPorts() const noexcept override { return true; }

   uint32_t audioPortsCount(bool isInput) const noexcept override {
      // We just declare 1 audio input and 1 audio output
      return 1;
   }

   bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const noexcept override {
      if (index > 0)
         return false;
      info->id = 0;
      snprintf(info->name, sizeof(info->name), "%s", "My Port Name");
      info->channel_count = 2;
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
      info->port_type = CLAP_PORT_STEREO;
      info->in_place_pair = CLAP_INVALID_ID;
      return true;
   }

   /////////////////
   // clap_plugin //
   /////////////////

   bool init() noexcept override {
      // Fetch host's extensions here via the HostProxy (_host)
      // Make sure to check that the interface functions are not null pointers
      // e.g. _host.canUseLog(), _host.canUseAudioPorts(), etc.
      return Plugin::init();
   }

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
               // @WORKSHOP: handle parameter event
               if (ev->param_id == 0) // cutoff
                   freq_01 = (float) ev->value;
               else if (ev->param_id == 1) // damping
                   damping = (float) ev->value;
               break;
            }
            case CLAP_EVENT_PARAM_MOD: {
               const auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(hdr);
               // @WORKSHOP: handle param mod event
               if (ev->param_id == 0) // cutoff
                   freq_mod = (float) ev->amount;
               else if (ev->param_id == 1) // damping
                   damping_mod = (float) ev->amount;
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

      // Pre-compute filter coefficients once, before the audio loop
      const float freq_param = std::clamp(freq_01 + freq_mod * 0.5f, 0.0f, 1.0f);
      const float freq_hz    = map_frequency_param(freq_param);
      const float dam_param  = std::clamp(damping + damping_mod * 0.5f, 0.0f, 1.0f);

      const float w   = (float) M_PI * freq_hz / fs;
      const float g   = std::tan(w);
      const float k   = dam_param;
      const float gk  = g + k;
      const float gt0 = 1.0f / (1.0f + g * gk);
      const float gt1 = g   * gt0;
      const float gk1 = g   * gk * gt0;
      const float gt2 = g   * gt1;

      const uint32_t nchannels = proc->audio_inputs[0].channel_count;
      const uint32_t nframes   = proc->frames_count;
      for (uint32_t ch = 0; ch < nchannels; ++ch) {
         FilterState &state = filter_state[ch];
         for (uint32_t n = 0; n < nframes; ++n) {
            const float input = proc->audio_inputs[0].data32[ch][n];

            const float t0 = input - state.ic2eq;
            const float t1 = gt1 * t0 - gk1 * state.ic1eq;
            const float t2 = gt2 * t0 + gt1 * state.ic1eq;
            const float v1 = t1 + state.ic1eq;
            const float v2 = t2 + state.ic2eq;
            state.ic1eq += 2.0f * t1;
            state.ic2eq += 2.0f * t2;

            proc->audio_outputs[0].data32[ch][n] = v1; // bandpass output
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

static bool entry_init(const char *plugin_path) {
   // perform the plugin initialization
   return true;
}

static void entry_deinit(void) {
   // perform the plugin de-initialization
}

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
