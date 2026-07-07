// This file demonstrates how to wire a CLAP plugin using the Jai programming language.
// It is functionally equivalent to the C++ version in ../cpp/plugin.cpp

#import "Basic";
#import "clap-jai";
Math :: #import "Math";

///////////////////////
// Plugin Descriptor //
///////////////////////

PLUGIN_ID      :: "com.ccrma.dynamics-plugin";
PLUGIN_NAME    :: "Dynamics Plugin";
PLUGIN_VENDOR  :: "CCRMA CLAP WORKSHOP";
PLUGIN_URL     :: "";
PLUGIN_VERSION :: "1.0.0";
PLUGIN_DESC    :: "A dynamics plugin.";

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
    host_thread_pool: *clap_host_thread_pool_t;

    fs : float;

    // parameter handles
    threshold_db      : float = 0.0;   // dB
    ratio             : float = 4.0;   // compression ratio
    knee_db           : float = 0.0;   // dB
    attack_ms         : float = 10.0;  // ms
    release_ms        : float = 100.0; // ms
    makeup_gain_db    : float = 0.0;   // dB
    crossover_freq_01 : float = 0.5;

    // @WORKSHOP: add DSP state here
    Crossover_Filter_State :: struct {
        stage1_ic1eq : float = 0;
        stage1_ic2eq : float = 0;
        stage2_lpf_ic1eq : float = 0;
        stage2_lpf_ic2eq : float = 0;
        stage2_hpf_ic1eq : float = 0;
        stage2_hpf_ic2eq : float = 0;
    };
    crossover_state : [2] Crossover_Filter_State; // max 2 channels (stereo)

    low_band_level_state : [2]float;
    high_band_level_state : [2]float;

    // save for thread-pool
    low_band : []float;
    high_band : []float;
    current_channel : s64;
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

CROSSOVER_MAX :: 10000.0;
CROSSOVER_MIN :: 100.0;
map_crossover_param :: (param01 : float) -> float {
    return CROSSOVER_MIN * Math.pow(CROSSOVER_MAX / CROSSOVER_MIN, param01);
}

map_crossover_param_inverse :: (freq_hz : float) -> float {
    return Math.log(freq_hz / CROSSOVER_MIN) / Math.log(CROSSOVER_MAX / CROSSOVER_MIN);
}

params_count :: (plugin: *clap_plugin_t) -> u32 #c_call {
    // @WORKSHOP: add parameters here
    return 7;
}

params_info :: (plugin: *clap_plugin_t, param_index: u32, info: *clap_param_info_t) -> bool #c_call {
    push_context {
        // @WORKSHOP: add parameters here
        if param_index == 0 {
            info.id = 0;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Threshold\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = -60.0;
            info.max_value = 0.0;
            info.default_value = 0.0;
        } else if param_index == 1 {
            info.id = 1;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Ratio\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = 1.0;
            info.max_value = 20.0;
            info.default_value = 4.0;
        } else if param_index == 2 {
            info.id = 2;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Knee\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = 0.0;
            info.max_value = 18.0;
            info.default_value = 0.0;
        } else if param_index == 3 {
            info.id = 3;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Attack\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = 1.0;
            info.max_value = 500.0;
            info.default_value = 10.0;
        } else if param_index == 4 {
            info.id = 4;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Release\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = 10.0;
            info.max_value = 2000.0;
            info.default_value = 100.0;
        } else if param_index == 5 {
            info.id = 5;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Makeup Gain\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = -30.0;
            info.max_value = 30.0;
            info.default_value = 0.0;
        } else if param_index == 6 {
            info.id = 6;
            info.flags = .CLAP_PARAM_IS_AUTOMATABLE;
            name := "Crossover Frequency\0";
            memcpy(info.name.data, name.data, name.count);
            info.min_value = 0.0;
            info.max_value = 1.0;
            info.default_value = 0.5;
        } else {
            return false;
        }

        return true;
    }
}

params_value :: (plugin: *clap_plugin_t, param_id: clap_id, value: *float64) -> bool #c_call {
    // @WORKSHOP: return parameter values
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;
        if param_id == 0 {
            value.* = plug.threshold_db;
        } else if param_id == 1 {
            value.* = plug.ratio;
        } else if param_id == 2 {
            value.* = plug.knee_db;
        } else if param_id == 3 {
            value.* = plug.attack_ms;
        } else if param_id == 4 {
            value.* = plug.release_ms;
        } else if param_id == 5 {
            value.* = plug.makeup_gain_db;
        } else if param_id == 6 {
            value.* = plug.crossover_freq_01;
        } else {
            return false;
        }
        return true;
    }
}

params_value_to_text :: (plugin: *clap_plugin_t, param_id: clap_id, value: float64, display: *u8, size: u32) -> bool #c_call {
    push_context {
        str := tprint("%", value);
        if param_id == 0 || param_id == 2 || param_id == 5 {
            str = tprint("% dB", value);
        } else if param_id == 1 {
            str = tprint("%:1", value);
        } else if param_id == 3 || param_id == 4 {
            str = tprint("% ms", value);
        } else if param_id == 6 {
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
        if param_id == 6 {
            value.* = xx map_crossover_param_inverse(xx v);
        }
        return true;
    }
}

params_flush :: (plugin: *clap_plugin_t, in_events: *clap_input_events_t, out_events: *clap_output_events_t) -> void #c_call {
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
        plug.host_log         = cast(*clap_host_log_t)         plug.host.get_extension(plug.host, CLAP_EXT_LOG.data);
        plug.host_audio_ports = cast(*clap_host_audio_ports_t) plug.host.get_extension(plug.host, CLAP_EXT_AUDIO_PORTS.data);
        plug.host_thread_pool = cast(*clap_host_thread_pool_t) plug.host.get_extension(plug.host, CLAP_EXT_THREAD_POOL.data);
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
                if (ev.param_id == 0)
                    plug.threshold_db = xx ev.value;
                else if (ev.param_id == 1)
                    plug.ratio = xx ev.value;
                else if (ev.param_id == 2)
                    plug.knee_db = xx ev.value;
                else if (ev.param_id == 3)
                    plug.attack_ms = xx ev.value;
                else if (ev.param_id == 4)
                    plug.release_ms = xx ev.value;
                else if (ev.param_id == 5)
                    plug.makeup_gain_db = xx ev.value;
                else if (ev.param_id == 6)
                    plug.crossover_freq_01 = xx ev.value;

            case CLAP_EVENT_TRANSPORT;
                ev := cast(*clap_event_transport_t) hdr;
        }
    }
}

apply_compression :: (using plug: *My_Plugin, data: [] float, ch: int, is_low_band: bool) {
    auto_release_temp();

    threshold_lin := Math.pow(10.0, threshold_db / 20.0);
    makeup_gain_lin := Math.pow(10.0, makeup_gain_db / 20.0);
    knee_lin := Math.pow(10.0, knee_db / 20.0);

    half_knee := Math.sqrt(knee_lin); // half in Decibels -> sqrt in linear gain
    knee_lower := threshold_lin / half_knee;
    knee_upper := threshold_lin * half_knee;
    log_thresh := Math.log(threshold_lin);
    log_knee := Math.log(knee_lin);
    rho := (1.0 / ratio) - 1.0;

    nframes := data.count;
    gain := [] float.{ nframes, talloc(nframes * size_of(float)) };

    // full-wave rectify
    for n: 0..cast(s64)nframes - 1 {
        gain[n] = Math.abs(data[n]);
    }

    // apply gain computer
    for n: 0..cast(s64)nframes - 1 {
        level := gain[n];

        if (level <= knee_lower) { // below threshold
            gain[n] = 1;
        } else if (level >= knee_upper) { // compression range
            gain[n] = Math.pow(level / threshold_lin, rho);
        } else { // knee range
            _8x_log10 : float : xx (8.0 * 2.30258509299404568);

            log_x := Math.log(level);
            root := log_knee + 2.0 * (log_x - log_thresh);
            gain[n] = Math.pow(10.0, ((1.0 - ratio) * root * root) / (_8x_log10 * ratio * log_knee));
        }

        gain[n] *= makeup_gain_lin;
    }

    // apply level detector
    z := ifx is_low_band then *low_band_level_state[ch] else *high_band_level_state[ch];
    attack_b0 := 1.0 - Math.exp (-1.0 / (fs * attack_ms * 0.001));
    release_b0 := 1.0 - Math.exp (-1.0 / (fs * release_ms * 0.001));
    for n: 0..cast(s64)nframes - 1 {
        level := gain[n];
        b0 := ifx level > z.* then release_b0 else attack_b0;
        z.* += b0 * (level - z.*);
        gain[n] = z.*;
    }

    // apply gain reduction
    for n: 0..cast(s64)nframes - 1 {
        data[n] *= gain[n];
    }
}

plug_process :: (plugin: *clap_plugin_t, proc: *clap_process_t) -> clap_process_status #c_call {
    push_context {
        using plug := cast(*My_Plugin) plugin.plugin_data;

        // Process incoming events
        nev: u32 = proc.in_events.size(proc.in_events);
        for ev_index: 0..cast(s64)nev - 1 {
            hdr := proc.in_events.get(proc.in_events, cast(u32)ev_index);
            process_event(plug, hdr);
        }

        // Process audio stream
        // @WORKSHOP: audio processing here!
        crossover_hz  := map_crossover_param(crossover_freq_01);

        // crossover filter coefficients
        w := Math.PI * crossover_hz / fs;
        g := Math.tan(w);
        k := Math.sqrt(2.0);
        gk := g + k;
        gt0 := 1/(1 + g*gk);
        gk0 := gk*gt0;
        gt1 := g*gt0;
        gk1 := g*gk0;
        gt2 := g*gt1;

        auto_release_temp();
        nchannels := proc.audio_inputs[0].channel_count;
        nframes   := proc.frames_count;
        plug.low_band = [] float.{ nframes, talloc(nframes * size_of(float)) };
        plug.high_band = [] float.{ nframes, talloc(nframes * size_of(float)) };
        for ch : 0..cast(s64)nchannels - 1 {
            current_channel = ch;

            // split low and high band
            s := *crossover_state[ch];
            for n: 0..cast(s64)nframes - 1 {
                input := proc.audio_inputs[0].data32[ch][n];
                t0 := input - s.stage1_ic2eq;
                v0 := gt0 * t0 - gk0 * s.stage1_ic1eq;
                t1 := gt1 * t0 - gk1 * s.stage1_ic1eq;
                t2 := gt2 * t0 + gt1 * s.stage1_ic1eq;
                v1 := t1 + s.stage1_ic1eq;
                v2 := t2 + s.stage1_ic2eq;
                s.stage1_ic1eq = s.stage1_ic1eq + 2 * t1;
                s.stage1_ic2eq = s.stage1_ic2eq + 2 * t2;
                low_band[n] = v2;
                high_band[n] = v0;
            }

            if (host_thread_pool.request_exec(host, 2)) {
                // code is executed in thread_pool_exec...
            } else {
                apply_compression(plug, low_band, ch, false);
                apply_compression(plug, high_band, ch, true);
            }


            // sum outputs
            for n: 0..cast(s64)nframes - 1 {
                proc.audio_outputs[0].data32[ch][n] = low_band[n] + high_band[n];
            }
        }
    }

    return .CLAP_PROCESS_CONTINUE;
}

////////////////////////////
// clap_plugin_thread_pool //
////////////////////////////

plug_thread_pool_exec :: (plugin: *clap_plugin_t, task_index: u32) -> void #c_call {
    push_context {
        plug := cast(*My_Plugin) plugin.plugin_data;
        is_high_band := task_index == 1;
        buffer := ifx is_high_band then plug.high_band else plug.low_band;
        apply_compression(plug, buffer, plug.current_channel, is_high_band);
    }
}

plugin_thread_pool := clap_plugin_thread_pool_t.{
    exec = plug_thread_pool_exec,
};

plug_get_extension :: (plugin: *clap_plugin_t, id: *char) -> *void #c_call {
    push_context {
        if to_string(id) == "clap.audio-ports"  return *plugin_audio_ports;
        if to_string(id) == "clap.params"        return *plugin_params;
        if to_string(id) == "clap.thread-pool"   return *plugin_thread_pool;
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
    return true;
}

entry_deinit :: () #c_call {
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
