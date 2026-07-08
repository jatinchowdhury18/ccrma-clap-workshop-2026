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

const PLUGIN_ID: &[u8] = b"org.ccrma.delay-plugin\0";

static S_FEATURES: StaticWrapper<[*const c_char; 3]> = StaticWrapper([
    b"audio-effect\0".as_ptr() as *const c_char,
    b"stereo\0".as_ptr() as *const c_char,
    ptr::null(),
]);

static S_MY_PLUG_DESC: clap_plugin_descriptor = clap_plugin_descriptor {
    clap_version: CLAP_VERSION,
    id:           PLUGIN_ID.as_ptr() as *const c_char,
    name:         b"Delay Plugin\0".as_ptr() as *const c_char,
    vendor:       b"CCRMA CLAP WORKSHOP\0".as_ptr() as *const c_char,
    url:          b"\0".as_ptr() as *const c_char,
    manual_url:   b"\0".as_ptr() as *const c_char,
    support_url:  b"\0".as_ptr() as *const c_char,
    version:      b"1.0.0\0".as_ptr() as *const c_char,
    description:  b"A delay plugin.\0".as_ptr() as *const c_char,
    features:     &raw const S_FEATURES as *const *const c_char,
};

struct Delay {
    max_delay_samples: usize,
    write_pointer:     usize,
    delay_buffer:      Vec<f32>,
}

impl Delay {
    fn new() -> Self {
        Delay { max_delay_samples: 0, write_pointer: 0, delay_buffer: Vec::new() }
    }

    fn initialize(&mut self, max_delay: usize) {
        self.max_delay_samples = max_delay;
        self.write_pointer = 0;
        self.delay_buffer = vec![0.0; max_delay];
    }

    fn write(&mut self, x: f32) {
        self.delay_buffer[self.write_pointer] = x;

        self.write_pointer += self.max_delay_samples - 1;
        if self.write_pointer >= self.max_delay_samples {
            self.write_pointer -= self.max_delay_samples;
        }
    }

    fn read(&self, delay_samples: usize) -> f32 {
        // @WORKSHOP: right now we're reading with no interpolation,
        // but here is where you would apply interpolation...
        let mut read_pointer = self.write_pointer + delay_samples;
        if read_pointer >= self.max_delay_samples {
            read_pointer -= self.max_delay_samples;
        }
        self.delay_buffer[read_pointer]
    }
}

#[allow(dead_code)]
struct MyPlugin {
    host:              *const clap_host,
    host_log:          *const clap_host_log,
    host_audio_ports:  *const clap_host_audio_ports,
    host_thread_check: *const clap_host_thread_check,

    fs: f32,

    // parameter handles
    delay_time_ms: f32,
    dry_wet:       f32,

    // DSP state
    // @WORKSHOP: fill in required processing state
    delay: [Delay; 2],
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

unsafe extern "C" fn my_plug_params_count(_plugin: *const clap_plugin) -> u32 { 2 }

unsafe extern "C" fn my_plug_params_info(_plugin: *const clap_plugin, param_index: u32, info: *mut clap_param_info) -> bool {
    // @WORKSHOP: add parameters here
    match param_index {
        0 => {
            (*info).id            = 0;
            (*info).flags         = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value     = 1.0;
            (*info).max_value     = 1000.0;
            (*info).default_value = 100.0;
            let name = b"Delay Time\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        1 => {
            (*info).id            = 1;
            (*info).flags         = CLAP_PARAM_IS_AUTOMATABLE;
            (*info).min_value     = 0.0;
            (*info).max_value     = 1.0;
            (*info).default_value = 0.5;
            let name = b"Dry/Wet\0";
            for (dst, &src) in (*info).name.iter_mut().zip(name.iter()) { *dst = src as c_char; }
        }
        _ => return false,
    }
    true
}

unsafe extern "C" fn my_plug_params_value(plugin: *const clap_plugin, param_id: clap_id, value: *mut f64) -> bool {
    let plug = &*((*plugin).plugin_data as *const MyPlugin);
    match param_id {
        0 => { *value = plug.delay_time_ms as f64; }
        1 => { *value = plug.dry_wet       as f64; }
        _ => return false,
    }
    true
}

unsafe extern "C" fn my_plug_params_value_to_text(_plugin: *const clap_plugin, param_id: clap_id, value: f64, display: *mut c_char, size: u32) -> bool {
    let s = match param_id {
        0 => format!("{:.1} ms", value),
        1 => format!("{:.2}%", value),
        _ => format!("{:.3}", value),
    };
    let bytes = s.as_bytes();
    if bytes.len() + 1 > size as usize { return false; }
    for (i, &b) in bytes.iter().enumerate() { *display.add(i) = b as c_char; }
    *display.add(bytes.len()) = 0;
    true
}

unsafe extern "C" fn my_plug_params_text_to_value(_plugin: *const clap_plugin, _param_id: clap_id, display: *const c_char, value: *mut f64) -> bool {
    let Ok(s) = CStr::from_ptr(display).to_str() else { return false };
    let Ok(v) = s.trim().parse::<f64>()           else { return false };
    *value = v;
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

unsafe extern "C" fn my_plug_activate(_plugin: *const clap_plugin, sample_rate: f64, _min_frames_count: u32, _max_frames_count: u32) -> bool {
    // @WORKSHOP: fill in activation code
    let plug = &mut *((*_plugin).plugin_data as *mut MyPlugin);
    plug.fs = sample_rate as f32;

    let max_delay_samples = sample_rate.ceil() as usize; // max delay time: 1 second
    for d in plug.delay.iter_mut() {
        d.initialize(max_delay_samples);
    }

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
                    0 => plug.delay_time_ms = ev.value as f32,
                    1 => plug.dry_wet       = ev.value as f32,
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

    // @WORKSHOP: fill in processing code here
    let delay_time_samples = (plug.delay_time_ms * 0.001 * plug.fs).round() as usize;
    let wet_gain = plug.dry_wet.sqrt();
    let dry_gain = (1.0 - plug.dry_wet).sqrt();

    let nframes   = (*process).frames_count;
    let nchannels = (*(*process).audio_inputs.add(0)).channel_count as usize;
    let in_data   = (*(*process).audio_inputs.add(0)).data32;
    let out_data  = (*(*process).audio_outputs.add(0)).data32;
    for ch in 0..nchannels {
        for n in 0..nframes as usize {
            let input = *(*in_data.add(ch)).add(n);

            // read from delay line
            let delay_out = plug.delay[ch].read(delay_time_samples);

            // write to delay line
            let delay_in = input;
            plug.delay[ch].write(delay_in);

            // dry/wet mix
            let output = wet_gain * delay_out + dry_gain * input;

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
        delay_time_ms:     100.0,
        dry_wet:           0.5,
        delay:             [Delay::new(), Delay::new()],
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
