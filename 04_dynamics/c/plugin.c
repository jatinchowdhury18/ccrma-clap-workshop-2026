// This file is here to demonstrate how to wire a CLAP plugin
// You can use it as a starting point, however if you are implementing a C++
// plugin, I'd encourage you to use the C++ glue layer instead:
// https://github.com/free-audio/clap-helpers/blob/main/include/clap/helpers/plugin.hh

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <clap/clap.h>

static const clap_plugin_descriptor_t s_my_plug_desc = {
   .clap_version = CLAP_VERSION_INIT,
   .id           = BUNDLE_ID,
   .name         = "Dynamics Plugin",
   .vendor       = "CCRMA CLAP WORKSHOP",
   .version      = "1.0.0",
   .description  = "A dynamics plugin.",
   .features     = (const char *[]){CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, NULL},
};

// Crossover frequency: mapped logarithmically from [0, 1] to [100, 10000] Hz.
// These helpers are provided — the @WORKSHOP focus is on the compression DSP below.
#define CROSSOVER_MIN 100.0f
#define CROSSOVER_MAX 10000.0f
static float map_crossover_param(float param01) {
   return CROSSOVER_MIN * powf(CROSSOVER_MAX / CROSSOVER_MIN, param01);
}
static float map_crossover_param_inverse(float freq_hz) {
   return logf(freq_hz / CROSSOVER_MIN) / logf(CROSSOVER_MAX / CROSSOVER_MIN);
}

static float db_to_gain(float db) {
   return powf(10.0f, db / 20.0f);
}

typedef struct {
   clap_plugin_t                   plugin;
   const clap_host_t              *host;
   const clap_host_log_t          *host_log;
   const clap_host_audio_ports_t  *host_audio_ports;
   const clap_host_thread_check_t *host_thread_check;

   float fs;

   // parameter handles (stored in native units)
   float threshold_db;       // dB
   float ratio;              // compression ratio
   float knee_db;            // dB
   float attack_ms;          // ms
   float release_ms;         // ms
   float makeup_gain_db;     // dB
   float crossover_freq_01;

   // @WORKSHOP: add DSP state here
} my_plug_t;

/////////////////////////////
// clap_plugin_audio_ports //
/////////////////////////////

static uint32_t my_plug_audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
   return 1;
}

static bool my_plug_audio_ports_get(const clap_plugin_t    *plugin,
                                    uint32_t                index,
                                    bool                    is_input,
                                    clap_audio_port_info_t *info) {
   if (index > 0) return false;
   info->id = 0;
   snprintf(info->name, sizeof(info->name), "%s", "My Port Name");
   info->channel_count = 2;
   info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
   info->port_type     = CLAP_PORT_STEREO;
   info->in_place_pair = CLAP_INVALID_ID;
   return true;
}

static const clap_plugin_audio_ports_t s_my_plug_audio_ports = {
   .count = my_plug_audio_ports_count,
   .get   = my_plug_audio_ports_get,
};

////////////////////////
// clap_plugin_params //
////////////////////////

static uint32_t my_plug_params_count(const clap_plugin_t *plugin) {
   return 7;
}

static bool my_plug_params_info(const clap_plugin_t *plugin, uint32_t param_index, clap_param_info_t *info) {
   // Parameters:
   //   0  Threshold         dB   [-60, 0]    default 0
   //   1  Ratio             x:1  [1,   20]   default 4
   //   2  Knee              dB   [0,   18]   default 0
   //   3  Attack            ms   [1,   500]  default 10
   //   4  Release           ms   [10,  2000] default 100
   //   5  Makeup Gain       dB   [-30, 30]   default 0
   //   6  Crossover Freq    0-1  [0,   1]    default 0.5
   switch (param_index) {
      case 0:
         info->id           = 0;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = -60.0;
         info->max_value    = 0.0;
         info->default_value= 0.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Threshold");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 1:
         info->id           = 1;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = 1.0;
         info->max_value    = 20.0;
         info->default_value= 4.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Ratio");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 2:
         info->id           = 2;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = 0.0;
         info->max_value    = 18.0;
         info->default_value= 0.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Knee");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 3:
         info->id           = 3;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = 1.0;
         info->max_value    = 500.0;
         info->default_value= 10.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Attack");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 4:
         info->id           = 4;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = 10.0;
         info->max_value    = 2000.0;
         info->default_value= 100.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Release");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 5:
         info->id           = 5;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = -30.0;
         info->max_value    = 30.0;
         info->default_value= 0.0;
         snprintf(info->name, CLAP_NAME_SIZE, "Makeup Gain");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      case 6:
         info->id           = 6;
         info->flags        = CLAP_PARAM_IS_AUTOMATABLE;
         info->min_value    = 0.0;
         info->max_value    = 1.0;
         info->default_value= 0.5;
         snprintf(info->name, CLAP_NAME_SIZE, "Crossover Frequency");
         snprintf(info->module, CLAP_PATH_SIZE, "");
         return true;
      default:
         return false;
   }
}

static bool my_plug_params_value(const clap_plugin_t *plugin, clap_id param_id, double *value) {
   const my_plug_t *plug = plugin->plugin_data;
   switch (param_id) {
      case 0: *value = (double)plug->threshold_db;      return true;
      case 1: *value = (double)plug->ratio;             return true;
      case 2: *value = (double)plug->knee_db;           return true;
      case 3: *value = (double)plug->attack_ms;         return true;
      case 4: *value = (double)plug->release_ms;        return true;
      case 5: *value = (double)plug->makeup_gain_db;    return true;
      case 6: *value = (double)plug->crossover_freq_01; return true;
      default: return false;
   }
}

static bool my_plug_params_value_to_text(const clap_plugin_t *plugin, clap_id param_id,
                                         double value, char *display, uint32_t size) {
   int written;
   if (param_id == 0 || param_id == 2 || param_id == 5)
      written = snprintf(display, (size_t)size, "%.1f dB", value);
   else if (param_id == 1)
      written = snprintf(display, (size_t)size, "%.1f:1", value);
   else if (param_id == 3 || param_id == 4)
      written = snprintf(display, (size_t)size, "%.1f ms", value);
   else // param_id == 6: crossover frequency
      written = snprintf(display, (size_t)size, "%.1f Hz",
                         (double)map_crossover_param((float)value));

   if (written < 0)               return false;
   if ((uint32_t)written >= size) return false;
   return true;
}

static bool my_plug_params_text_to_value(const clap_plugin_t *plugin, clap_id param_id,
                                         const char *display, double *value) {
   char *end = NULL;
   errno = 0;
   double v = strtod(display, &end);
   if (end == display) return false;
   if (errno != 0)     return false;

   if (param_id == 6)
      *value = (double)map_crossover_param_inverse((float)v);
   else
      *value = v;
   return true;
}

static void my_plug_params_flush(const clap_plugin_t *plugin,
                                  const clap_input_events_t *in,
                                  const clap_output_events_t *out) {}

static const clap_plugin_params_t s_my_plug_params = {
   .count          = my_plug_params_count,
   .get_info       = my_plug_params_info,
   .get_value      = my_plug_params_value,
   .value_to_text  = my_plug_params_value_to_text,
   .text_to_value  = my_plug_params_text_to_value,
   .flush          = my_plug_params_flush,
};

/////////////////
// clap_plugin //
/////////////////

static bool my_plug_init(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;

   plug->host_log         = (const clap_host_log_t *)         plug->host->get_extension(plug->host, CLAP_EXT_LOG);
   plug->host_audio_ports = (const clap_host_audio_ports_t *) plug->host->get_extension(plug->host, CLAP_EXT_AUDIO_PORTS);
   plug->host_thread_check= (const clap_host_thread_check_t *)plug->host->get_extension(plug->host, CLAP_EXT_THREAD_CHECK);

   // Initialize parameter defaults
   plug->threshold_db      = 0.0f;
   plug->ratio             = 4.0f;
   plug->knee_db           = 0.0f;
   plug->attack_ms         = 10.0f;
   plug->release_ms        = 100.0f;
   plug->makeup_gain_db    = 0.0f;
   plug->crossover_freq_01 = 0.5f;

   return true;
}

static void my_plug_destroy(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   free(plug);
}

static bool my_plug_activate(const struct clap_plugin *plugin,
                              double sample_rate,
                              uint32_t min_frames_count,
                              uint32_t max_frames_count) {
   // @WORKSHOP: initialization
   my_plug_t *plug = plugin->plugin_data;
   plug->fs = (float)sample_rate;
   return true;
}

static void my_plug_deactivate(const struct clap_plugin *plugin) {}
static bool my_plug_start_processing(const struct clap_plugin *plugin) { return true; }
static void my_plug_stop_processing(const struct clap_plugin *plugin) {}
static void my_plug_reset(const struct clap_plugin *plugin) {}

static void my_plug_process_event(my_plug_t *plug, const clap_event_header_t *hdr) {
   if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
      switch (hdr->type) {
         case CLAP_EVENT_PARAM_VALUE: {
            const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
            switch (ev->param_id) {
               case 0: plug->threshold_db      = (float)ev->value; break;
               case 1: plug->ratio             = (float)ev->value; break;
               case 2: plug->knee_db           = (float)ev->value; break;
               case 3: plug->attack_ms         = (float)ev->value; break;
               case 4: plug->release_ms        = (float)ev->value; break;
               case 5: plug->makeup_gain_db    = (float)ev->value; break;
               case 6: plug->crossover_freq_01 = (float)ev->value; break;
            }
            break;
         }
         case CLAP_EVENT_TRANSPORT: {
            const clap_event_transport_t *ev = (const clap_event_transport_t *)hdr;
            (void)ev; // TODO: handle transport event
            break;
         }
      }
   }
}

static clap_process_status my_plug_process(const struct clap_plugin *plugin,
                                           const clap_process_t     *process) {
   my_plug_t *plug = plugin->plugin_data;

   // Process incoming events
   const uint32_t nev = process->in_events->size(process->in_events);
   for (uint32_t ev_index = 0; ev_index < nev; ++ev_index) {
      const clap_event_header_t *hdr = process->in_events->get(process->in_events, ev_index);
      my_plug_process_event(plug, hdr);
   }

   // Process audio stream
   // @WORKSHOP: audio processing here!
   // Hint: pre-compute your crossover filter coefficients here (once, before the
   // audio loop), then run the per-sample DSP loop below.
   // Convert parameters to usable values:
   //   const float crossover_hz   = map_crossover_param(plug->crossover_freq_01);
   //   const float threshold_lin  = db_to_gain(plug->threshold_db);
   //   const float makeup_gain    = db_to_gain(plug->makeup_gain_db);
   //   const float attack_coeff   = expf(-1.0f / (plug->attack_ms  * 0.001f * plug->fs));
   //   const float release_coeff  = expf(-1.0f / (plug->release_ms * 0.001f * plug->fs));
   const uint32_t nchannels = process->audio_inputs[0].channel_count;
   const uint32_t nframes   = process->frames_count;
   for (uint32_t ch = 0; ch < nchannels; ++ch) {
      for (uint32_t n = 0; n < nframes; ++n) {
         const float input = process->audio_inputs[0].data32[ch][n];

         // @WORKSHOP: replace the passthrough below with multiband compression DSP
         const float output = input;

         process->audio_outputs[0].data32[ch][n] = output;
      }
   }

   return CLAP_PROCESS_CONTINUE;
}

static const void *my_plug_get_extension(const struct clap_plugin *plugin, const char *id) {
   if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &s_my_plug_audio_ports;
   if (!strcmp(id, CLAP_EXT_PARAMS))      return &s_my_plug_params;
   return NULL;
}

static void my_plug_on_main_thread(const struct clap_plugin *plugin) {}

clap_plugin_t *my_plug_create(const clap_host_t *host) {
   my_plug_t *p = calloc(1, sizeof(*p));
   p->host = host;
   p->plugin.desc             = &s_my_plug_desc;
   p->plugin.plugin_data      = p;
   p->plugin.init             = my_plug_init;
   p->plugin.destroy          = my_plug_destroy;
   p->plugin.activate         = my_plug_activate;
   p->plugin.deactivate       = my_plug_deactivate;
   p->plugin.start_processing = my_plug_start_processing;
   p->plugin.stop_processing  = my_plug_stop_processing;
   p->plugin.reset            = my_plug_reset;
   p->plugin.process          = my_plug_process;
   p->plugin.get_extension    = my_plug_get_extension;
   p->plugin.on_main_thread   = my_plug_on_main_thread;
   // Don't call into the host here
   return &p->plugin;
}

/////////////////////////
// clap_plugin_factory //
/////////////////////////

static struct {
   const clap_plugin_descriptor_t *desc;
   clap_plugin_t *(CLAP_ABI *create)(const clap_host_t *host);
} s_plugins[] = {
   { .desc = &s_my_plug_desc, .create = my_plug_create },
};

static uint32_t plugin_factory_get_plugin_count(const struct clap_plugin_factory *factory) {
   return sizeof(s_plugins) / sizeof(s_plugins[0]);
}

static const clap_plugin_descriptor_t *
plugin_factory_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
   return s_plugins[index].desc;
}

static const clap_plugin_t *plugin_factory_create_plugin(const struct clap_plugin_factory *factory,
                                                         const clap_host_t                *host,
                                                         const char *plugin_id) {
   if (!clap_version_is_compatible(host->clap_version)) return NULL;
   const int N = sizeof(s_plugins) / sizeof(s_plugins[0]);
   for (int i = 0; i < N; ++i)
      if (!strcmp(plugin_id, s_plugins[i].desc->id))
         return s_plugins[i].create(host);
   return NULL;
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
   return NULL;
}

// This symbol will be resolved by the host
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
   .clap_version = CLAP_VERSION_INIT,
   .init         = entry_init,
   .deinit       = entry_deinit,
   .get_factory  = entry_get_factory,
};
