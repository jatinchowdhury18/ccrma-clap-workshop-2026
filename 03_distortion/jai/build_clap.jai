#import "Basic";
#import "Compiler";
File :: #import "File";

#if OS == .MACOS {
    #import "File_Utilities";
}

plugin_source  :: "plugin.jai";
plugin_name    :: "multiband_distortion_plugin";

// Plugin metadata for the macOS bundle (must match values in plugin.jai)
BUNDLE_ID      :: "com.ccrma.multiband-distortion-plugin";
BUNDLE_VERSION :: "1.0.0";
BUNDLE_DESC    :: "A multiband distortion plugin.";

#run build();

build :: () {
    w := compiler_create_workspace();

    options := get_build_options(w);
    options.output_type = .DYNAMIC_LIBRARY;
    options.output_executable_name = plugin_name;

    // Forward any -import_dir paths passed on the command line to the new workspace,
    // and handle optional build flags like -release.
    import_paths: [..] string;
    array_add(*import_paths, ..options.import_path);
    cli := options.compile_time_command_line;
    for arg, idx: cli {
        if arg == "-import_dir" {
            array_add(*import_paths, cli[idx + 1]);
        } else if arg == "-release" {
            set_optimization(*options, .OPTIMIZED);
            options.stack_trace = false;
        }
    }
    options.import_path = import_paths;

    set_build_options(options, w);

    compiler_begin_intercept(w);
    add_build_file(tprint("%/%", #filepath, plugin_source), w);
    while true {
        msg := compiler_wait_for_message();
        if msg.kind == .COMPLETE  break;
    }
    compiler_end_intercept(w);

    set_build_options_dc(.{do_output = false});

    #if OS == .WINDOWS {
        dll  := tprint("%.dll",  plugin_name);
        clap := tprint("%.clap", plugin_name);
        File.file_delete(clap);
        File.file_move(dll, clap);
        print("Built: %\n", clap);
    } else #if OS == .MACOS {
        create_macos_bundle();
        print("Built: %.clap\n", plugin_name);
    } else #if OS == .LINUX {
        so   := tprint("%.so",   plugin_name);
        clap := tprint("%.clap", plugin_name);
        File.file_delete(clap);
        File.file_move(so, clap);
        print("Built: %\n", clap);
    }
}

#if OS == .MACOS {

create_macos_bundle :: () {
    bundle := tprint("%.clap", plugin_name);
    dylib  := tprint("%.dylib", plugin_name);

    // (Re-)create the bundle directory structure
    File.delete_directory(bundle);
    File.make_directory_if_it_does_not_exist(bundle);
    File.make_directory_if_it_does_not_exist(tprint("%/Contents",           bundle));
    File.make_directory_if_it_does_not_exist(tprint("%/Contents/MacOS",     bundle));
    File.make_directory_if_it_does_not_exist(tprint("%/Contents/Resources", bundle));

    File.file_move(dylib, tprint("%/Contents/MacOS/%", bundle, plugin_name));

    File.write_entire_file(tprint("%/Contents/PkgInfo",    bundle), "BNDL????");
    File.write_entire_file(tprint("%/Contents/Info.plist", bundle), make_info_plist());
}

make_info_plist :: () -> string {
    // Six % substitutions in order: executable, description, identifier,
    // bundle name, short version, and full version.
    return tprint(PLIST_TEMPLATE, plugin_name, BUNDLE_DESC, BUNDLE_ID,
                                 plugin_name, BUNDLE_VERSION, BUNDLE_VERSION);
}

PLIST_TEMPLATE :: #string XML
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>English</string>
    <key>CFBundleExecutable</key><string>%</string>
    <key>CFBundleGetInfoString</key><string>%</string>
    <key>CFBundleIdentifier</key><string>%</string>
    <key>CFBundleName</key><string>%</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleSignature</key><string>????</string>
    <key>CFBundleShortVersionString</key><string>%</string>
    <key>CFBundleVersion</key><string>%</string>
</dict>
</plist>
XML

} // #if OS == .MACOS
