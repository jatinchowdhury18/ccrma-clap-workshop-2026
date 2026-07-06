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

// Helper that lets a raw-pointer-containing type live in a `static`.
// Safety: the caller guarantees all pointers inside point to 'static data.
#[repr(transparent)]
struct StaticWrapper<T>(T);
unsafe impl<T> Sync for StaticWrapper<T> {}

///////////////////////
// Plugin Descriptor //
///////////////////////

const PLUGIN_ID: &[u8] = b"org.ccrma.filter-plugin\0";

// Null-terminated feature list used by the descriptor below.
// Needs StaticWrapper because *const c_char is !Sync.
static S_FEATURES: StaticWrapper<[*const c_char; 3]> = StaticWrapper([
    b"audio-effect\0".as_ptr() as *const c_char,
    b"stereo\0".as_ptr() as *const c_char,
    ptr::null(),
]);

static S_MY_PLUG_DESC: clap_plugin_descriptor = clap_plugin_descriptor {
    clap_version: CLAP_VERSION,
    id:           PLUGIN_ID.as_ptr() as *const c_char,
    name:         b"Filter Plugin\0".as_ptr() as *const c_char,
    vendor:       b"CCRMA CLAP WORKSHOP\0".as_ptr() as *const c_char,
    url:          b"\0".as_ptr() as *const c_char,
    manual_url:   b"\0".as_ptr() as *const c_char,
    support_url:  b"\0".as_ptr() as *const c_char,
    version:      b"1.4.2\0".as_ptr() as *const c_char,
    description:  b"A filter plugin.\0".as_ptr() as *const c_char,
    features:     &raw const S_FEATURES as *const *const c_char,
};

// Frequency parameter: mapped logarithmically from [0, 1] to [100, 10000] Hz.
// These helpers are provided — the @WORKSHOP focus is on the filter DSP below.
const CUTOFF_MIN: f32 = 100.0;
const CUTOFF_MAX: f32 = 10_000.0;

fn map_frequency_param(param01: f32) -> f32 {
    CUTOFF_MIN * (CUTOFF_MAX / CUTOFF_MIN).powf(param01)
}

fn map_frequency_param_inverse(freq_hz: f32) -> f32 {
    (freq_hz / CUTOFF_MIN).ln() / (CUTOFF_MAX / CUTOFF_MIN).ln()
}

///////////////////////////////////////////////////////////
// Plugin instance data (equivalent to my_plug_t in C)  //
///////////////////////////////////////////////////////////

// @WORKSHOP: DSP state for one channel of the state variable filter (SVF)
struct FilterState {
    ic1eq: f32,
    ic2eq: f32,
}

impl FilterState {
    const fn new() -> Self {
        Self { ic1eq: 0.0, ic2eq: 0.0 }
    }
}

// @WORKSHOP:
// We can add fields here, like:
// - data that our plugin will use for processing audio, etc.
// Note: host extension handles are stored here after init() fetches them.
#[allow(dead_code)] // fields are written in init() and read in @WORKSHOP code
struct MyPlugin {
    host:              *const clap_host,
    host_log:          *const clap_host_log,
    host_audio_ports:  *const clap_host_audio_ports,
    host_thread_check: *const clap_host_thread_check,

    fs: f32,

    // parameter handles
    freq_01:     f32,
    freq_mod:    f32,
    damping:     f32,
    damping_mod: f32,

    // @WORKSHOP: DSP state (one FilterState per channel)
    filter_state: [FilterState; 2], // max 2 channels (stereo)
}

/////////////////////////////
// clap_plugin_audio_ports //
/////////////////////////////

unsafe extern "C" fn my_plug_audio_ports_count(
    _plugin: *const clap_plugin,
    _is_input: bool,
) -> u32 {
    // We just declare 1 audio input and 1 audio output
    1
}

unsafe extern "C" fn my_plug_audio_ports_get(
    _plugin: *const clap_plugin,
    index: u32,
    _is_input: bool,
    info: *mut clap_audio_port_info,
) -> bool {
    if index > 0 {
        return false;
    }
    (*info).id = 0;
    let name = b"My Port Name";
    for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) {
        *dst = src as c_char;
    }
    (*info).name[name.len()] = 0;
    (*info).channel_count = 2;
    (*info).flags = CLAP_AUDIO_PORT_IS_MAIN;
    (*info).port_type = CLAP_PORT_STEREO.as_ptr();
    (*info).in_place_pair = CLAP_INVALID_ID;
    true
}

static S_MY_PLUG_AUDIO_PORTS: clap_plugin_audio_ports = clap_plugin_audio_ports {
    count: Some(my_plug_audio_ports_count),
    get:   Some(my_plug_audio_ports_get),
};

////////////////////////
// clap_plugin_params //
////////////////////////

unsafe extern "C" fn my_plug_params_count(_plugin: *const clap_plugin) -> u32 {
    // @WORKSHOP: implement parameter count
    0
}

unsafe extern "C" fn my_plug_params_info(
    _plugin: *const clap_plugin,
    _param_index: u32,
    _info: *mut clap_param_info,
) -> bool {
    // @WORKSHOP: implement parameter info
    // Hint: use CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE for both params
    false
}

unsafe extern "C" fn my_plug_params_value(
    _plugin: *const clap_plugin,
    _param_id: clap_id,
    _value: *mut f64,
) -> bool {
    // @WORKSHOP: implement parameter value
    false
}

unsafe extern "C" fn my_plug_params_value_to_text(
    _plugin: *const clap_plugin,
    param_id: clap_id,
    value: f64,
    display: *mut c_char,
    size: u32,
) -> bool {
    let s = if param_id == 0 {
        format!("{:.1} Hz", map_frequency_param(value as f32))
    } else {
        format!("{:.2}", value)
    };
    let bytes = s.as_bytes();
    if bytes.len() + 1 > size as usize {
        return false; // output would be truncated
    }
    for (i, &b) in bytes.iter().enumerate() {
        *display.add(i) = b as c_char;
    }
    *display.add(bytes.len()) = 0;
    true
}

unsafe extern "C" fn my_plug_params_text_to_value(
    _plugin: *const clap_plugin,
    param_id: clap_id,
    display: *const c_char,
    value: *mut f64,
) -> bool {
    let Ok(s) = CStr::from_ptr(display).to_str() else { return false };
    let Ok(v) = s.trim().parse::<f64>()           else { return false };
    *value = if param_id == 0 {
        map_frequency_param_inverse(v as f32) as f64
    } else {
        v
    };
    true
}

unsafe extern "C" fn my_plug_params_flush(
    _plugin: *const clap_plugin,
    _in: *const clap_input_events,
    _out: *const clap_output_events,
) {
    // @WORKSHOP: handle parameter events when not processing (can be no-op for now)
}

static S_MY_PLUG_PARAMS: clap_plugin_params = clap_plugin_params {
    count:         Some(my_plug_params_count),
    get_info:      Some(my_plug_params_info),
    get_value:     Some(my_plug_params_value),
    value_to_text: Some(my_plug_params_value_to_text),
    text_to_value: Some(my_plug_params_text_to_value),
    flush:         Some(my_plug_params_flush),
};

/////////////////
// clap_plugin //
/////////////////

unsafe extern "C" fn my_plug_init(_plugin: *const clap_plugin) -> bool {
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);

    // Fetch host's extensions here
    // Make sure to check that the interface functions are not null pointers
    if let Some(get_ext) = (*plug.host).get_extension {
        plug.host_log = get_ext(plug.host, CLAP_EXT_LOG.as_ptr())
            as *const clap_host_log;
        plug.host_audio_ports = get_ext(plug.host, CLAP_EXT_AUDIO_PORTS.as_ptr())
            as *const clap_host_audio_ports;
        plug.host_thread_check = get_ext(plug.host, CLAP_EXT_THREAD_CHECK.as_ptr())
            as *const clap_host_thread_check;
    }
    true
}

unsafe extern "C" fn my_plug_destroy(_plugin: *const clap_plugin) {
    drop(Box::from_raw((*_plugin).plugin_data as *mut MyPlugin));
    drop(Box::from_raw(_plugin as *mut clap_plugin));
}

unsafe extern "C" fn my_plug_activate(
    _plugin: *const clap_plugin,
    _sample_rate: f64,
    _min_frames_count: u32,
    _max_frames_count: u32,
) -> bool {
    // @WORKSHOP: initialization
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);
    plug.fs = _sample_rate as f32;
    true
}

unsafe extern "C" fn my_plug_deactivate(_plugin: *const clap_plugin) {}

unsafe extern "C" fn my_plug_start_processing(_plugin: *const clap_plugin) -> bool { true }

unsafe extern "C" fn my_plug_stop_processing(_plugin: *const clap_plugin) {}

unsafe extern "C" fn my_plug_reset(_plugin: *const clap_plugin) {}

unsafe fn my_plug_process_event(_plug: &mut MyPlugin, hdr: *const clap_event_header) {
    if (*hdr).space_id == CLAP_CORE_EVENT_SPACE_ID {
        match (*hdr).type_ {
            CLAP_EVENT_PARAM_VALUE => {
                let _ev = &*(hdr as *const clap_event_param_value);
                // @WORKSHOP: handle parameter change
            }
            CLAP_EVENT_PARAM_MOD => {
                let _ev = &*(hdr as *const clap_event_param_mod);
                // @WORKSHOP: handle parameter modulation
                // Hint: store _ev.amount into freq_mod or damping_mod based on _ev.param_id
            }
            CLAP_EVENT_TRANSPORT => {
                let _ev = &*(hdr as *const clap_event_transport);
                // TODO: handle transport event
            }
            _ => {}
        }
    }
}

unsafe extern "C" fn my_plug_process(
    _plugin: *const clap_plugin,
    process: *const clap_process,
) -> clap_process_status {
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);

    // Process incoming events
    let in_ev   = (*process).in_events;
    // SAFETY: the host must provide a valid clap_input_events with non-null
    // size/get function pointers. If it doesn't, we have a host bug; abort
    // is safer than UB from unwinding through C frames.
    let size_fn = (*in_ev).size.unwrap_unchecked();
    let get_fn  = (*in_ev).get.unwrap_unchecked();
    let nev = size_fn(in_ev);
    for ev_index in 0..nev {
        let hdr = get_fn(in_ev, ev_index);
        my_plug_process_event(plug, hdr);
    }

    // Process audio stream
    // @WORKSHOP: audio processing here!
    // Hint: pre-compute your filter coefficients here (once, before the audio loop),
    // then run the per-sample filter loop below.
    let nframes   = (*process).frames_count;
    let nchannels = (*(*process).audio_inputs.add(0)).channel_count as usize;
    let in_data   = (*(*process).audio_inputs.add(0)).data32;
    let out_data  = (*(*process).audio_outputs.add(0)).data32;
    for ch in 0..nchannels {
        let state = &mut plug.filter_state[ch];
        for n in 0..nframes as usize {
            // fetch input sample
            let input = *(*in_data.add(ch)).add(n);

            // @WORKSHOP: replace the passthrough below with SVF filter DSP
            let output = input;

            // store output sample
            *(*out_data.add(ch)).add(n) = output;
        }
    }

    CLAP_PROCESS_CONTINUE
}

unsafe extern "C" fn my_plug_get_extension(
    _plugin: *const clap_plugin,
    id: *const c_char,
) -> *const c_void {
    let id = CStr::from_ptr(id);
    if id == CLAP_EXT_AUDIO_PORTS {
        return &raw const S_MY_PLUG_AUDIO_PORTS as *const c_void;
    }
    if id == CLAP_EXT_PARAMS {
        return &raw const S_MY_PLUG_PARAMS as *const c_void;
    }
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
        freq_01:           0.5,
        freq_mod:          0.0,
        damping:           0.2,
        damping_mod:       0.0,
        filter_state:      [FilterState::new(), FilterState::new()],
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

    // Don't call into the host here

    Box::into_raw(plugin)
}

/////////////////////////
// clap_plugin_factory //
/////////////////////////

unsafe extern "C" fn plugin_factory_get_plugin_count(
    _factory: *const clap_plugin_factory,
) -> u32 {
    1
}

unsafe extern "C" fn plugin_factory_get_plugin_descriptor(
    _factory: *const clap_plugin_factory,
    _index: u32,
) -> *const clap_plugin_descriptor {
    &raw const S_MY_PLUG_DESC
}

unsafe extern "C" fn plugin_factory_create_plugin(
    _factory: *const clap_plugin_factory,
    host: *const clap_host,
    plugin_id: *const c_char,
) -> *const clap_plugin {
    if !clap_version_is_compatible((*host).clap_version) {
        return ptr::null();
    }

    let id = CStr::from_ptr(plugin_id);
    let our_id = CStr::from_ptr(S_MY_PLUG_DESC.id);
    if id != our_id {
        return ptr::null();
    }

    my_plug_create(host)
}

static S_PLUGIN_FACTORY: clap_plugin_factory = clap_plugin_factory {
    get_plugin_count:      Some(plugin_factory_get_plugin_count),
    get_plugin_descriptor: Some(plugin_factory_get_plugin_descriptor),
    create_plugin:         Some(plugin_factory_create_plugin),
};

////////////////
// clap_entry //
////////////////

unsafe extern "C" fn entry_init(_plugin_path: *const c_char) -> bool {
    // perform the plugin initialization
    true
}

unsafe extern "C" fn entry_deinit() {
    // perform the plugin de-initialization
}

unsafe extern "C" fn entry_get_factory(factory_id: *const c_char) -> *const c_void {
    let factory_id = CStr::from_ptr(factory_id);
    if factory_id == CLAP_PLUGIN_FACTORY_ID {
        return &raw const S_PLUGIN_FACTORY as *const c_void;
    }
    ptr::null()
}

// This symbol will be resolved by the host
#[no_mangle]
#[allow(non_upper_case_globals)]
pub static clap_entry: clap_plugin_entry = clap_plugin_entry {
    clap_version: CLAP_VERSION,
    init:         Some(entry_init),
    deinit:       Some(entry_deinit),
    get_factory:  Some(entry_get_factory),
};
