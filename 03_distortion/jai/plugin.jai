// This file demonstrates how to wire a CLAP plugin using the Jai programming language.
// It is functionally equivalent to the C version in ../c/plugin.c

#import "Basic";
#import "clap-jai";
Math :: #import "Math";

///////////////////////
// Plugin Descriptor //
///////////////////////

PLUGIN_ID      :: "com.ccrma.multiband-distortion-plugin";
PLUGIN_NAME    :: "Multiband Distortion Plugin";
PLUGIN_VENDOR  :: "CCRMA CLAP WORKSHOP";
PLUGIN_URL     :: "";
PLUGIN_VERSION :: "1.0.0";
PLUGIN_DESC    :: "A multiband distortion plugin.";

plugin_features :: *u8.[
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT.data,
    CLAP_PLUGIN_FEATURE_STEREO.data,
    null,
];

plugin_descriptor :: clap_plugin_descriptor_t.{
    clap_version = CLAP_VERSION,
    id           = PLUGIN_ID.data,
    name         = PLUGIN_NAME.data,
    vendor       = PLUGIN_VENDOR.data,
    url          = PLUGIN_URL.data,
    version      = PLUGIN_VERSION.data,
    description  = PLUGIN_DESC.data,
    features     = plugin_features.data,
};

/////////////////
// Plugin Data //
/////////////////

My_Plugin :: struct {
    clap_plugin:      clap_plugin_t;
    host:             *clap_host_t;
    host_log:         *clap_host_log_t;
    host_audio_ports: *clap_host_audio_ports_t;

    // @WORKSHOP:
    // We can add things to this struct, like:
    // - more extension handles (latency reporting, etc.)
    // - data that our plugin will use for processing audio, etc.
    fs : float;

    // parameter handles (stored in native units)
    low_drive_db      : float = 0.0;  // dB
    high_drive_db     : float = 0.0;  // dB
    crossover_freq_01 : float = 0.5;
    output_gain_db    : float = 0.0;  // dB

    // @WORKSHOP: add DSP state here
}

// Crossover frequency: mapped logarithmically from [0, 1] to [100, 10000] Hz.
// These helpers are provided — the @WORKSHOP focus is on the distortion DSP below.
CROSSOVER_MAX :: 10000.0;
CROSSOVER_MIN :: 100.0;
map_crossover_param :: (param01 : float) -> float {
    return CROSSOVER_MIN * Math.pow(CROSSOVER_MAX / CROSSOVER_MIN, param01);
}
map_crossover_param_inverse :: (freq_hz : float) -> float {
    return Math.log(freq_hz / CROSSOVER_MIN) / Math.log(CROSSOVER_MAX / CROSSOVER_MIN);
}

/////////////////////////////
// clap_plugin_audio_ports //
/////////////////////////////

audio_ports_count :: (plugin: *clap_plugin_t, is_input: bool) -> u32 #c_call {
    return 1;
}

audio_ports_get :: (plugin: *clap_plugin_t, index: u32, is_input: bool, info: *clap_audio_port_info_t) -> bool #c_call {
    if index > 0  return false;
    info.id = 0;
    port_name :: "My Port Name\0";
    memcpy(info.name.data, port_name.data, port_name.count);
    info.channel_count = 2;
    info.flags = CLAP_AUDIO_PORT_IS_MAIN;
    info.port_type = CLAP_PORT_STEREO.data;
    info.in_place_pair = CLAP_INVALID_ID;
    return true;
}

plugin_audio_ports := clap_plugin_audio_ports_t.{
    count = audio_ports_count,
    get   = audio_ports_get,
};

////////////////////////
// clap_plugin_params //
////////////////////////

params_count :: (plugin: *clap_plugin_t) -> u32 #c_call {
    // @WORKSHOP: implement parameter count
    return 0;
}

params_info :: (plugin: *clap_plugin_t, param_index: u32, info: *clap_param_info_t) -> bool #c_call {
    push_context {
        // @WORKSHOP: implement parameter info
        // Parameters:
        //   0  Low Drive       dB   [-40, 40]  default 0
        //   1  High Drive      dB   [-40, 40]  default 0
        //   2  Crossover Freq  0-1  [0,   1]   default 0.5
        //   3  Output Gain     dB   [-40, 40]  default 0
        // Hint: use .CLAP_PARAM_IS_AUTOMATABLE for all params
        return false;
    }
}

params_value :: (plugin: *clap_plugin_t, param_id: clap_id, value: *float64) -> bool #c_call {
    // @WORKSHOP: implement parameter value
    return false;
}

params_value_to_text :: (plugin: *clap_plugin_t, param_id: clap_id, value: float64, display: *u8, size: u32) -> bool #c_call {
    push_context {
        str := tprint("%", value);
        if param_id == 0 || param_id == 1 || param_id == 3 {
            str = tprint("% dB", value);
        } else if param_id == 2 {
            str = tprint("% Hz", map_crossover_param(xx value));
        }
        if cast(u32)(str.count + 1) > size  return false;
        memcpy(display, str.data, str.count + 1);
        return true;
    }
}

params_text_to_value :: (plugin: *clap_plugin_t, param_id: clap_id, display: *u8, value: *float64) -> bool #c_call {
    push_context {
        str := to_string(display);
        v, ok := string_to_float64(str);
        if !ok  return false;
        value.* = v;
        if param_id == 2 {
            value.* = xx map_crossover_param_inverse(xx v);
        }
        return true;
    }
}

params_flush :: (plugin: *clap_plugin_t, in_events: *clap_input_events_t, out_events: *clap_output_events_t) -> void #c_call {
    // @WORKSHOP: handle parameter events when not processing (can be no-op for now)
}

plugin_params := clap_plugin_params_t.{
    count          = params_count,
    get_info       = params_info,
    get_value      = params_value,
    value_to_text  = params_value_to_text,
    text_to_value  = params_text_to_value,
    flush          = params_flush,
};

/////////////////
// clap_plugin //
/////////////////

plug_init :: (plugin: *clap_plugin_t) -> bool #c_call {
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;
        // Fetch host extensions here
        // Make sure to check that the interface functions are not null pointers
        plug.host_log         = cast(*clap_host_log_t)         plug.host.get_extension(plug.host, CLAP_EXT_LOG.data);
        plug.host_audio_ports = cast(*clap_host_audio_ports_t) plug.host.get_extension(plug.host, CLAP_EXT_AUDIO_PORTS.data);
    }
    return true;
}

plug_destroy :: (plugin: *clap_plugin_t) -> void #c_call {
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;
        free(plug);
    }
}

plug_activate :: (plugin: *clap_plugin_t, sample_rate: float64, min_frames_count: u32, max_frames_count: u32) -> bool #c_call {
    // @WORKSHOP: initialization
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;
        plug.fs = xx sample_rate;
    }
    return true;
}

plug_deactivate       :: (plugin: *clap_plugin_t) -> void #c_call {}
plug_start_processing :: (plugin: *clap_plugin_t) -> bool #c_call { return true; }
plug_stop_processing  :: (plugin: *clap_plugin_t) -> void #c_call {}
plug_reset            :: (plugin: *clap_plugin_t) -> void #c_call {}

process_event :: (plug: *My_Plugin, hdr: *clap_event_header_t) {
    if hdr.space_id == CLAP_CORE_EVENT_SPACE_ID {
        if hdr.type == {
            case CLAP_EVENT_PARAM_VALUE;
                ev := cast(*clap_event_param_value_t) hdr;
                // @WORKSHOP: handle parameter change

            case CLAP_EVENT_TRANSPORT;
                ev := cast(*clap_event_transport_t) hdr;
                // TODO: handle transport event
        }
    }
}

plug_process :: (plugin: *clap_plugin_t, proc: *clap_process_t) -> clap_process_status #c_call {
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;

        // Process incoming events
        nev: u32 = proc.in_events.size(proc.in_events);
        for ev_index: 0..cast(s64)nev - 1 {
            hdr := proc.in_events.get(proc.in_events, cast(u32)ev_index);
            process_event(plug, hdr);
        }

        // Process audio stream
        // @WORKSHOP: audio processing here!
        // Hint: pre-compute your crossover filter coefficients here (once, before the
        // audio loop), then run the per-sample DSP loop below.
        // Convert parameters to usable values:
        //   crossover_hz := map_crossover_param(plug.crossover_freq_01);
        //   low_drive    := Math.pow(10.0, plug.low_drive_db  / 20.0);
        //   high_drive   := Math.pow(10.0, plug.high_drive_db / 20.0);
        //   output_gain  := Math.pow(10.0, plug.output_gain_db / 20.0);
        nchannels := proc.audio_inputs[0].channel_count;
        nframes   := proc.frames_count;
        for ch : 0..cast(s64)nchannels - 1 {
            for n: 0..cast(s64)nframes - 1 {
                // fetch input sample
                input := proc.audio_inputs[0].data32[ch][n];

                // @WORKSHOP: replace the passthrough below with multiband distortion DSP
                output := input;

                // store output sample
                proc.audio_outputs[0].data32[ch][n] = output;
            }
        }
    }
    return .CLAP_PROCESS_CONTINUE;
}

plug_get_extension :: (plugin: *clap_plugin_t, id: *char) -> *void #c_call {
    push_context {
        if to_string(id) == "clap.audio-ports"  return *plugin_audio_ports;
        if to_string(id) == "clap.params"        return *plugin_params;
    }
    return null;
}

plug_on_main_thread :: (plugin: *clap_plugin_t) -> void #c_call {}

plugin_create :: (factory: *clap_plugin_factory_t, host: *clap_host_t) -> *clap_plugin_t {
    p := New(My_Plugin);
    p.host = host;
    p.clap_plugin.desc             = *plugin_descriptor;
    p.clap_plugin.plugin_data      = p;
    p.clap_plugin.init             = plug_init;
    p.clap_plugin.destroy          = plug_destroy;
    p.clap_plugin.activate         = plug_activate;
    p.clap_plugin.deactivate       = plug_deactivate;
    p.clap_plugin.start_processing = plug_start_processing;
    p.clap_plugin.stop_processing  = plug_stop_processing;
    p.clap_plugin.reset            = plug_reset;
    p.clap_plugin.process          = plug_process;
    p.clap_plugin.get_extension    = plug_get_extension;
    p.clap_plugin.on_main_thread   = plug_on_main_thread;
    // Don't call into the host here
    return *p.clap_plugin;
}

/////////////////////////
// clap_plugin_factory //
/////////////////////////

factory_get_plugin_count :: (factory: *clap_plugin_factory_t) -> u32 #c_call {
    return 1;
}

factory_get_plugin_descriptor :: (factory: *clap_plugin_factory_t, idx: u32) -> *clap_plugin_descriptor_t #c_call {
    return *plugin_descriptor;
}

factory_create_plugin :: (factory: *clap_plugin_factory_t, host: *clap_host_t, plugin_id: *char) -> *clap_plugin_t #c_call {
    push_context {
        if !clap_version_is_compatible(host.clap_version)  return null;
        if to_string(plugin_id) != PLUGIN_ID               return null;
        return plugin_create(factory, host);
    }
}

plugin_factory := clap_plugin_factory_t.{
    get_plugin_count      = factory_get_plugin_count,
    get_plugin_descriptor = factory_get_plugin_descriptor,
    create_plugin         = factory_create_plugin,
};

////////////////
// clap_entry //
////////////////

entry_init :: (plugin_path: *char) -> bool #c_call {
    // perform the plugin initialization
    return true;
}

entry_deinit :: () #c_call {
    // perform the plugin de-initialization
}

entry_get_factory :: (factory_id: *char) -> *void #c_call {
    push_context {
        if to_string(factory_id) == "clap.plugin-factory"  return *plugin_factory;
    }
    return null;
}

// This symbol will be resolved by the host
#program_export
clap_entry := clap_plugin_entry_t.{
    clap_version = CLAP_VERSION,
    init         = entry_init,
    deinit       = entry_deinit,
    get_factory  = entry_get_factory,
};
