// This file demonstrates how to wire a CLAP plugin using the C++ glue layer:
// https://github.com/free-audio/clap-helpers/blob/main/include/clap/helpers/plugin.hh
//
// It is functionally equivalent to the C version in ../c/plugin.c

#include <cstring>
#include <cstdio>

#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

using Plugin = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                     clap::helpers::CheckingLevel::Maximal>;

struct My_Plugin : Plugin
{
   // @WORKSHOP:
   // We can add member variables here, like:
   // - data that our plugin will use for processing audio, etc.
   // Note: host extension handles are managed by the base class via _host
   float fs {};
   float phi {};
   float freq_hz { 1'000.0f };
   float gain { 0.5f };

   inline static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
   inline static const clap_plugin_descriptor_t desc = {
      .clap_version = CLAP_VERSION_INIT,
      .id           = BUNDLE_ID,
      .name         = "Generator Plugin",
      .vendor       = "CCRMA CLAP WORKSHOP",
      .url          = "",
      .manual_url   = "",
      .support_url  = "",
      .version      = "1.4.2",
      .description  = "A simple signal generator plugin.",
      .features     = features,
   };

   explicit My_Plugin(const clap_host_t *host) : Plugin(&desc, host) {}

   //--------------------//
   // clap_plugin_params //
   //--------------------//
   bool implementsParams() const noexcept override {
       // @WORKSHOP: return true to enable the params extension
       return true;
   }
   uint32_t paramsCount() const noexcept override {
       // @WORKSHOP: return the number of parameters (Gain + Frequency)
       return 2;
   }
   bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
   {
       if (paramIndex == 0)
       {
           // gain param
           info->id = 0;
           info->flags = CLAP_PARAM_IS_AUTOMATABLE;
           // info->cookie;
           std::strncpy(info->name, "Gain", CLAP_NAME_SIZE - 1);
           info->min_value = 0.0;
           info->max_value = 1.0;
           info->default_value = 0.5;
       }
       else if (paramIndex == 1)
       {
           // freq param
           info->id = 1;
           info->flags = CLAP_PARAM_IS_AUTOMATABLE;
           // info->cookie;
           std::strncpy(info->name, "Frequency", CLAP_NAME_SIZE - 1);
           info->min_value = 100.0;
           info->max_value = 10'000.0;
           info->default_value = 1'000.0;
       }
       else
       {
            return false;
       }

       return true;
   }
   bool paramsValue(clap_id paramId, double *value) noexcept override
   {
       // In this code, paramId and paramIndex are always the same.
       if (paramId == 0) // gain param
           *value = (double) gain;
       else if (paramId == 1) // freq param
           *value = (double) freq_hz;
       else
            return false;

       return true;
   }

   bool paramsValueToText(clap_id paramId, double value, char *display, uint32_t size) noexcept override
   {
       int written = std::snprintf(display, static_cast<std::size_t>(size), "%.2f", value);

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
      // @WORKSHOP
      // Anything we want to do at "initialization" time should go here
      fs = (float) sampleRate;
      return true;
   }

   void handleEvent(const clap_event_header_t *hdr) noexcept {
      if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
         switch (hdr->type) {
            case CLAP_EVENT_PARAM_VALUE: {
               const auto *ev = reinterpret_cast<const clap_event_param_value_t *>(hdr);
               // @WORKSHOP: handle parameter change
               if (ev->param_id == 0) // gain
                   gain = (float) ev->value;
               else if (ev->param_id == 1) // freq
                   freq_hz = (float) ev->value;
               break;
            }
            case CLAP_EVENT_PARAM_MOD: {
               const auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(hdr);
               // TODO: handle parameter modulation
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

   clap_process_status process(const clap_process_t *process) noexcept override {
      // Process incoming events
      const uint32_t nev = process->in_events->size(process->in_events);
      for (uint32_t ev_index = 0; ev_index < nev; ++ev_index) {
         const clap_event_header_t *hdr = process->in_events->get(process->in_events, ev_index);
         handleEvent(hdr);
      }

      // Process audio stream
      auto& input_buffer  = process->audio_inputs[0];
      auto& output_buffer = process->audio_outputs[0];
      const uint32_t nchannels = input_buffer.channel_count;
      const uint32_t nframes   = process->frames_count;
      const auto dp = 2 * (float) M_PI * freq_hz / fs;
      for (uint32_t n = 0; n < nframes; ++n) {
         // @WORKSHOP: actually do some audio processing here!
         phi += dp;
         if (phi > 2 * (float) M_PI)
             phi -= 2 * (float) M_PI;
         const auto y = gain * std::sin(phi);

         // store output samples (same value to all channels)
         for (uint32_t ch = 0; ch < nchannels; ++ch)
             output_buffer.data32[ch][n] = y;
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
