// Raw Rust bindings: https://crates.io/crates/clap-sys

use std::ffi::{c_char, c_void, CStr};
use std::ptr;

use clap_sys::entry::clap_plugin_entry;
use clap_sys::events::*;
use clap_sys::ext::audio_ports::*;
use clap_sys::ext::log::*;
use clap_sys::ext::params::*;
use clap_sys::ext::thread_check::*;
use clap_sys::factory::plugin_factory::*;
use clap_sys::host::clap_host;
use clap_sys::id::{clap_id, CLAP_INVALID_ID};
use clap_sys::plugin::{clap_plugin, clap_plugin_descriptor};
use clap_sys::process::{clap_process, clap_process_status, CLAP_PROCESS_CONTINUE};
use clap_sys::version::{clap_version_is_compatible, CLAP_VERSION};

#[repr(transparent)]
struct StaticWrapper<T>(T);
unsafe impl<T> Sync for StaticWrapper<T> {}

const PLUGIN_ID: &[u8] = b"org.ccrma.dynamics-plugin\0";

static S_FEATURES: StaticWrapper<[*const c_char; 3]> = StaticWrapper([
    b"audio-effect\0".as_ptr() as *const c_char,
    b"stereo\0".as_ptr() as *const c_char,
    ptr::null(),
]);

static S_MY_PLUG_DESC: clap_plugin_descriptor = clap_plugin_descriptor {
    clap_version: CLAP_VERSION,
    id:           PLUGIN_ID.as_ptr() as *const c_char,
    name:         b"Dynamics Plugin\0".as_ptr() as *const c_char,
    vendor:       b"CCRMA CLAP WORKSHOP\0".as_ptr() as *const c_char,
    url:          b"\0".as_ptr() as *const c_char,
    manual_url:   b"\0".as_ptr() as *const c_char,
    support_url:  b"\0".as_ptr() as *const c_char,
    version:      b"1.0.0\0".as_ptr() as *const c_char,
    description:  b"A dynamics plugin.\0".as_ptr() as *const c_char,
    features:     &raw const S_FEATURES as *const *const c_char,
};

const CROSSOVER_MIN: f32 = 100.0;
const CROSSOVER_MAX: f32 = 10_000.0;

fn map_crossover_param(param01: f32) -> f32 {
    CROSSOVER_MIN * (CROSSOVER_MAX / CROSSOVER_MIN).powf(param01)
}

fn map_crossover_param_inverse(freq_hz: f32) -> f32 {
    (freq_hz / CROSSOVER_MIN).ln() / (CROSSOVER_MAX / CROSSOVER_MIN).ln()
}

fn db_to_gain(db: f32) -> f32 {
    10.0_f32.powf(db / 20.0)
}

#[allow(dead_code)]
struct MyPlugin {
    host:              *const clap_host,
    host_log:          *const clap_host_log,
    host_audio_ports:  *const clap_host_audio_ports,
    host_thread_check: *const clap_host_thread_check,

    fs: f32,

    // parameter handles (stored in native units)
    threshold_db:      f32, // dB
    ratio:             f32, // compression ratio
    knee_db:           f32, // dB
    attack_ms:         f32, // ms
    release_ms:        f32, // ms
    makeup_gain_db:    f32, // dB
    crossover_freq_01: f32,

    // @WORKSHOP: add DSP state here
}

unsafe extern "C" fn my_plug_audio_ports_count(_plugin: *const clap_plugin, _is_input: bool) -> u32 { 1 }

unsafe extern "C" fn my_plug_audio_ports_get(_plugin: *const clap_plugin, index: u32, _is_input: bool, info: *mut clap_audio_port_info) -> bool {
    if index > 0 { return false; }
    (*info).id = 0;
    let name = b"My Port Name";
    for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
    (*info).name[name.len()] = 0;
    (*info).channel_count = 2;
    (*info).flags         = CLAP_AUDIO_PORT_IS_MAIN;
    (*info).port_type     = CLAP_PORT_STEREO.as_ptr();
    (*info).in_place_pair = CLAP_INVALID_ID;
    true
}

static S_MY_PLUG_AUDIO_PORTS: clap_plugin_audio_ports = clap_plugin_audio_ports {
    count: Some(my_plug_audio_ports_count),
    get:   Some(my_plug_audio_ports_get),
};

unsafe extern "C" fn my_plug_params_count(_plugin: *const clap_plugin) -> u32 { 7 }

unsafe extern "C" fn my_plug_params_info(_plugin: *const clap_plugin, param_index: u32, info: *mut clap_param_info) -> bool {
    match param_index {
        0 => {
            (*info).id           = 0;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = -60.0;
            (*info).max_value    = 0.0;
            (*info).default_value = 0.0;
            let name = b"Threshold\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        1 => {
            (*info).id           = 1;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = 1.0;
            (*info).max_value    = 20.0;
            (*info).default_value = 4.0;
            let name = b"Ratio\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        2 => {
            (*info).id           = 2;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = 0.0;
            (*info).max_value    = 18.0;
            (*info).default_value = 0.0;
            let name = b"Knee\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        3 => {
            (*info).id           = 3;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = 1.0;
            (*info).max_value    = 500.0;
            (*info).default_value = 10.0;
            let name = b"Attack\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        4 => {
            (*info).id           = 4;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = 10.0;
            (*info).max_value    = 2000.0;
            (*info).default_value = 100.0;
            let name = b"Release\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        5 => {
            (*info).id           = 5;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = -30.0;
            (*info).max_value    = 30.0;
            (*info).default_value = 0.0;
            let name = b"Makeup Gain\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        6 => {
            (*info).id           = 6;
            (*info).flags        = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value    = 0.0;
            (*info).max_value    = 1.0;
            (*info).default_value = 0.5;
            let name = b"Crossover Frequency\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        _ => return false,
    }
    true
}

unsafe extern "C" fn my_plug_params_value(plugin: *const clap_plugin, param_id: clap_id, value: *mut f64) -> bool {
    let plug = &*((*plugin).plugin_data as *const MyPlugin);
    match param_id {
        0 => { *value = plug.threshold_db      as f64; }
        1 => { *value = plug.ratio             as f64; }
        2 => { *value = plug.knee_db           as f64; }
        3 => { *value = plug.attack_ms         as f64; }
        4 => { *value = plug.release_ms        as f64; }
        5 => { *value = plug.makeup_gain_db    as f64; }
        6 => { *value = plug.crossover_freq_01 as f64; }
        _ => return false,
    }
    true
}

unsafe extern "C" fn my_plug_params_value_to_text(_plugin: *const clap_plugin, param_id: clap_id, value: f64, display: *mut c_char, size: u32) -> bool {
    let s = match param_id {
        0 | 2 | 5 => format!("{:.1} dB", value),
        1          => format!("{:.1}:1", value),
        3 | 4      => format!("{:.1} ms", value),
        6          => format!("{:.1} Hz", map_crossover_param(value as f32)),
        _          => return false,
    };
    let bytes = s.as_bytes();
    if bytes.len() + 1 > size as usize { return false; }
    for (i, &b) in bytes.iter().enumerate() { *display.add(i) = b as c_char; }
    *display.add(bytes.len()) = 0;
    true
}

unsafe extern "C" fn my_plug_params_text_to_value(_plugin: *const clap_plugin, param_id: clap_id, display: *const c_char, value: *mut f64) -> bool {
    let Ok(s) = CStr::from_ptr(display).to_str() else { return false };
    let Ok(v) = s.trim().parse::<f64>()           else { return false };
    *value = if param_id == 6 { map_crossover_param_inverse(v as f32) as f64 } else { v };
    true
}

unsafe extern "C" fn my_plug_params_flush(_plugin: *const clap_plugin, _in: *const clap_input_events, _out: *const clap_output_events) {}

static S_MY_PLUG_PARAMS: clap_plugin_params = clap_plugin_params {
    count:         Some(my_plug_params_count),
    get_info:      Some(my_plug_params_info),
    get_value:     Some(my_plug_params_value),
    value_to_text: Some(my_plug_params_value_to_text),
    text_to_value: Some(my_plug_params_text_to_value),
    flush:         Some(my_plug_params_flush),
};

unsafe extern "C" fn my_plug_init(_plugin: *const clap_plugin) -> bool {
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);
    if let Some(get_ext) = (*plug.host).get_extension {
        plug.host_log          = get_ext(plug.host, CLAP_EXT_LOG.as_ptr())          as *const clap_host_log;
        plug.host_audio_ports  = get_ext(plug.host, CLAP_EXT_AUDIO_PORTS.as_ptr())  as *const clap_host_audio_ports;
        plug.host_thread_check = get_ext(plug.host, CLAP_EXT_THREAD_CHECK.as_ptr()) as *const clap_host_thread_check;
    }
    true
}

unsafe extern "C" fn my_plug_destroy(_plugin: *const clap_plugin) {
    drop(Box::from_raw((*_plugin).plugin_data as *mut MyPlugin));
    drop(Box::from_raw(_plugin as *mut clap_plugin));
}

unsafe extern "C" fn my_plug_activate(_plugin: *const clap_plugin, _sample_rate: f64, _min_frames_count: u32, _max_frames_count: u32) -> bool {
    // @WORKSHOP: initialization
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);
    plug.fs = _sample_rate as f32;
    true
}

unsafe extern "C" fn my_plug_deactivate(_plugin: *const clap_plugin) {}
unsafe extern "C" fn my_plug_start_processing(_plugin: *const clap_plugin) -> bool { true }
unsafe extern "C" fn my_plug_stop_processing(_plugin: *const clap_plugin) {}
unsafe extern "C" fn my_plug_reset(_plugin: *const clap_plugin) {}

unsafe fn my_plug_process_event(plug: &mut MyPlugin, hdr: *const clap_event_header) {
    if (*hdr).space_id == CLAP_CORE_EVENT_SPACE_ID {
        match (*hdr).type_ {
            CLAP_EVENT_PARAM_VALUE => {
                let ev = &*(hdr as *const clap_event_param_value);
                match ev.param_id {
                    0 => plug.threshold_db      = ev.value as f32,
                    1 => plug.ratio             = ev.value as f32,
                    2 => plug.knee_db           = ev.value as f32,
                    3 => plug.attack_ms         = ev.value as f32,
                    4 => plug.release_ms        = ev.value as f32,
                    5 => plug.makeup_gain_db    = ev.value as f32,
                    6 => plug.crossover_freq_01 = ev.value as f32,
                    _ => {}
                }
            }
            CLAP_EVENT_TRANSPORT => {
                let _ev = &*(hdr as *const clap_event_transport);
            }
            _ => {}
        }
    }
}

unsafe extern "C" fn my_plug_process(_plugin: *const clap_plugin, process: *const clap_process) -> clap_process_status {
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);

    let in_ev   = (*process).in_events;
    let size_fn = (*in_ev).size.unwrap_unchecked();
    let get_fn  = (*in_ev).get.unwrap_unchecked();
    let nev = size_fn(in_ev);
    for ev_index in 0..nev {
        let hdr = get_fn(in_ev, ev_index);
        my_plug_process_event(plug, hdr);
    }

    // @WORKSHOP: audio processing here!
    // Useful conversions:
    //   threshold_gain  = db_to_gain(plug.threshold_db)
    //   makeup_gain     = db_to_gain(plug.makeup_gain_db)
    //   attack_coeff    = (-1.0 / (plug.attack_ms  * 0.001 * plug.fs)).exp()
    //   release_coeff   = (-1.0 / (plug.release_ms * 0.001 * plug.fs)).exp()
    //   crossover_hz    = map_crossover_param(plug.crossover_freq_01)

    let nframes   = (*process).frames_count;
    let nchannels = (*(*process).audio_inputs.add(0)).channel_count as usize;
    let in_data   = (*(*process).audio_inputs.add(0)).data32;
    let out_data  = (*(*process).audio_outputs.add(0)).data32;
    for ch in 0..nchannels {
        for n in 0..nframes as usize {
            let input = *(*in_data.add(ch)).add(n);
            // @WORKSHOP: replace the passthrough below with multiband compression DSP
            let output = input;
            *(*out_data.add(ch)).add(n) = output;
        }
    }

    CLAP_PROCESS_CONTINUE
}

unsafe extern "C" fn my_plug_get_extension(_plugin: *const clap_plugin, id: *const c_char) -> *const c_void {
    let id = CStr::from_ptr(id);
    if id == CLAP_EXT_AUDIO_PORTS { return &raw const S_MY_PLUG_AUDIO_PORTS as *const c_void; }
    if id == CLAP_EXT_PARAMS      { return &raw const S_MY_PLUG_PARAMS      as *const c_void; }
    ptr::null()
}

unsafe extern "C" fn my_plug_on_main_thread(_plugin: *const clap_plugin) {}

fn my_plug_create(host: *const clap_host) -> *const clap_plugin {
    let data = Box::new(MyPlugin {
        host,
        host_log:          ptr::null(),
        host_audio_ports:  ptr::null(),
        host_thread_check: ptr::null(),
        fs:                0.0,
        threshold_db:      0.0,
        ratio:             4.0,
        knee_db:           0.0,
        attack_ms:         10.0,
        release_ms:        100.0,
        makeup_gain_db:    0.0,
        crossover_freq_01: 0.5,
    });

    let plugin = Box::new(clap_plugin {
        desc:             &raw const S_MY_PLUG_DESC,
        plugin_data:      Box::into_raw(data) as *mut c_void,
        init:             Some(my_plug_init),
        destroy:          Some(my_plug_destroy),
        activate:         Some(my_plug_activate),
        deactivate:       Some(my_plug_deactivate),
        start_processing: Some(my_plug_start_processing),
        stop_processing:  Some(my_plug_stop_processing),
        reset:            Some(my_plug_reset),
        process:          Some(my_plug_process),
        get_extension:    Some(my_plug_get_extension),
        on_main_thread:   Some(my_plug_on_main_thread),
    });

    Box::into_raw(plugin)
}

unsafe extern "C" fn plugin_factory_get_plugin_count(_factory: *const clap_plugin_factory) -> u32 { 1 }

unsafe extern "C" fn plugin_factory_get_plugin_descriptor(_factory: *const clap_plugin_factory, _index: u32) -> *const clap_plugin_descriptor {
    &raw const S_MY_PLUG_DESC
}

unsafe extern "C" fn plugin_factory_create_plugin(_factory: *const clap_plugin_factory, host: *const clap_host, plugin_id: *const c_char) -> *const clap_plugin {
    if !clap_version_is_compatible((*host).clap_version) { return ptr::null(); }
    let id     = CStr::from_ptr(plugin_id);
    let our_id = CStr::from_ptr(S_MY_PLUG_DESC.id);
    if id != our_id { return ptr::null(); }
    my_plug_create(host)
}

static S_PLUGIN_FACTORY: clap_plugin_factory = clap_plugin_factory {
    get_plugin_count:      Some(plugin_factory_get_plugin_count),
    get_plugin_descriptor: Some(plugin_factory_get_plugin_descriptor),
    create_plugin:         Some(plugin_factory_create_plugin),
};

unsafe extern "C" fn entry_init(_plugin_path: *const c_char) -> bool { true }
unsafe extern "C" fn entry_deinit() {}

unsafe extern "C" fn entry_get_factory(factory_id: *const c_char) -> *const c_void {
    let factory_id = CStr::from_ptr(factory_id);
    if factory_id == CLAP_PLUGIN_FACTORY_ID {
        return &raw const S_PLUGIN_FACTORY as *const c_void;
    }
    ptr::null()
}

#[no_mangle]
#[allow(non_upper_case_globals)]
pub static clap_entry: clap_plugin_entry = clap_plugin_entry {
    clap_version: CLAP_VERSION,
    init:         Some(entry_init),
    deinit:       Some(entry_deinit),
    get_factory:  Some(entry_get_factory),
};
