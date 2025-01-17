PATrace
=======

PATrace is software for capturing GLES calls of an application and replaying them on a different device, keeping the GPU workload the same. It's similar to the open source APITrace project, but optimised for performance measurements. This means it tries hard to make the GPU the bottleneck and has low CPU usage. It achieves this by using a compact format for trace files and by allowing preloading, which means parsing and loading the trace data into RAM beforehand, then capturing the performance data while replaying the GLES calls from RAM

Installation
------------

You can build the latest trunk from source. First check it out, then make sure you have fetched all submdules:

    git submodule update --recursive --init

**Note**: master branch should not be used in production systems. Trace files produced with the trunk build might be incompatible in the future. Trace files produced by release builds will always be supported in future releases.

### Building for desktop Linux

Building for desktop Linux is useful if you want to replay trace files on your desktop using the GLES emulator or if you want to inspect the trace files using TraceView, which is a trace browsing GUI. Install these build dependencies:

    apt-get install build-essential cmake libx11-dev libwxgtk3.0-dev swig python-dev python-setuptools libtiff-dev default-jdk g++-multilib gcc-multilib libhdf5-serial-dev libpq-dev libpq5 subversion python-virtualenv qt5-default

Then build with the build.py script:

    ./scripts/build.py patrace x11_x64 release

If you need to run patrace under a `no_mali=1` Mali DDK (this is only available to ARM and ARM Mali partners), then you need to build for fbdev instead:

    scripts/build.py patrace fbdev_x64 release

If you need patrace python tools, you can install them like this:

    cd patrace/python
    pip install ./pypatrace/dist/pypatrace-*.whl
    pip install ./patracetools/dist/patracetools-*.whl

We recommend using and installing these packages inside a python **virtualenv**.

### Building for ARM Linux fbdev

For running on most ARMv7 embedded devices, such as Arndale or Firefly:

    ./scripts/build.py patrace fbdev_arm_hardfloat release

For running on ARMv8 embedded devices, such as most Juno board configurations:

    ./scripts/build.py patrace fbdev_aarch64 release

For embedded devices that do not use hardfloat (rare these days):

    ./scripts/build.py patrace fbdev_arm release

### Building for Android

Build dependencies: You must install the Android SDK and NDK with support for the **android** command as well as the **ndk-build** command (deprecated in later SDK versions). You can get the SDK tools at https://dl.google.com/android/adt/adt-bundle-linux-x86_64-20140702.zip and the NDK at https://dl.google.com/android/repository/android-ndk-r13b-linux-x86_64.zip. Add the NDK installation dir to your path environment variable, or to the NDK environment variable. Add the tools and platform-tools folders located in the android SDK installation folder to your path. Make sure you have java and ant installed:

    apt-get install openjdk-7-jdk ant

Building:

    ./scripts/build.py patrace android release

If you have a different android target installed than what the build script expects, then you will get build errors. To fix this, grep for the `ANDROID_TARGET` variable in the ./scripts directory and change it to your android target.

### Build Known Issues

The Python tree can get stale and fail to rebuild. You can clean the Python build parts with:

    git clean -fxd patrace/python

If you have strange errors and wish to reset the build system, try this:

    rm -rf builds/*
    git clean -fxd patrace/python  
    git submodule update --init  

If you do not wish to build the Python code (not doing so speeds up Linux builds considerably), you can do this by setting the environment variable

    NO_PYTHON_BUILD=y

Tracing
-------

### Installing fakedriver on Android

Make sure you have a rooted device where developer mode is enabled and connect it to your desktop. You then need to remount the system directory to install files there.

    adb shell  
    su  
    mount -o rw,remount /system  

When it comes to installing the fakedriver, the method differs depending on which device you have. Android will load the first driver it finds that goes by the name `libGLES_*.so` that resides in

    /system/lib/egl/

for 32-bit and

    /system/lib64/egl/

for 64-bit applications, with a fallback to `/system/vendor/lib[64]/egl`. If a vendor driver resides in this location, move it to the corresponding `/system/vendor/lib[64]/egl` directory to make sure Android loads the fakedriver rather than the system driver.

We have some hardcoded driver names that we check, if these do not work, try renaming the vendor drivers to `wrapped_libGLES.so` and place it in the `/system/vendor/lib[64]/egl` directory. (Note that if you do this, and then remove the fakedriver without renaming the system driver back, the phone will suddenly become unusable!)

By convention, we call our fakedriver `libGLES_wrapper.so`.

Example instructions for **Galaxy S7** (32-bit apps):

    adb push fakedriver/libGLES_wrapper_arm.so /sdcard/  
    adb shell su -c mount -o rw,remount /system  
    adb shell su -c cp /sdcard/libGLES_wrapper_arm.so /system/lib/egl/libGLES.so  
    adb shell su -c rm /sdcard/libGLES_wrapper_arm.so  
    adb shell su -c chmod 755 /system/lib/egl/libGLES.so  
    adb shell su -c mount -o ro,remount /system  

### Installing interceptor on Android

Install the interceptor and configure it:

    adb push install/patrace/android/release/egltrace/libinterceptor_patrace_arm.so /sdcard/  
    adb push install/patrace/android/release/egltrace/libinterceptor_patrace_arm64.so /sdcard/  
    adb shell  
    su  
    mount -o rw,remount /system  

    cp /sdcard/libinterceptor_patrace_arm.so /system/lib/egl/libinterceptor_patrace_arm.so  
    cp /sdcard/libinterceptor_patrace_arm64.so /system/lib64/egl/libinterceptor_patrace_arm64.so  

    chmod 755 /system/lib/egl/libinterceptor_patrace_arm.so  
    chmod 755 /system/lib64/egl/libinterceptor_patrace_arm64.so  

    echo "/system/lib/egl/libinterceptor_patrace_arm.so" > /system/lib/egl/interceptor.cfg  
    echo "/system/lib64/egl/libinterceptor_patrace_arm64.so" > /system/lib64/egl/interceptor.cfg  

    chmod 644 /system/lib/egl/interceptor.cfg  
    chmod 644 /system/lib64/egl/interceptor.cfg  

    mkdir /data/apitrace  
    chmod 777 /data/apitrace

Sometimes you will need to store the traces on the SD card instead:

    adb shell su -c mkdir /sdcard/patrace  
    adb shell su -c chmod 777 /sdcard/patrace  
    adb shell su -c ln -s /sdcard/Android/data /data/apitrace  

For Android before version 4.4, you need to update `egl.cfg`. Either update you `egl.cfg` manually, or use the provided one:

    adb push patrace/project/android/fakedriver/egl.cfg /system/lib/egl/

### Installing fakedriver on Android 8

Tracing on an Android 8 device is similar with the above. But some Project Treble enabled Android 8 devices forbid us to load libraries from /system/lib(64)/egl, so we need to install fakedrivers to /vendor/lib(64)/egl instead.

You can check if your Android 8 device supports Treble in adb shell:

    getprop ro.treble.enabled

Return value is "true" means your device supports Treble. Then you need to use the following steps to install fakedriver (32-bit apps):

    adb push fakedriver/libGLES_wrapper_arm.so /sdcard/  

    adb shell  
    su  
    mount -o rw,remount /vendor  

    cp /sdcard/libGLES_wrapper_arm.so /vendor/lib/egl/  
    rm /sdcard/libGLES_wrapper_arm.so  
    chmod 755 /vendor/lib/egl/libGLES_wrapper_arm.so  
    mv /vendor/lib/egl/libGLES_mali.so /vendor/lib/egl/lib_mali.so  
    mount -o ro,remount /vendor  

### Installing interceptor on Android 8

Similarly, install the interceptor to /vendor/lib(64)/egl and configure it:

    adb push install/patrace/android/release/egltrace/libinterceptor_patrace_arm.so /sdcard/  
    adb push install/patrace/android/release/egltrace/libinterceptor_patrace_arm64.so /sdcard/  
    adb shell  
    su  
    mount -o rw,remount /vendor  

    cp /sdcard/libinterceptor_patrace_arm.so /vendor/lib/egl/libinterceptor_patrace_arm.so  
    cp /sdcard/libinterceptor_patrace_arm64.so /vendor/lib64/egl/libinterceptor_patrace_arm64.so  

    chmod 755 /vendor/lib/egl/libinterceptor_patrace_arm.so  
    chmod 755 /vendor/lib64/egl/libinterceptor_patrace_arm64.so  

    echo "/vendor/lib/egl/libinterceptor_patrace_arm.so" > /vendor/lib/egl/interceptor.cfg  
    echo "/vendor/lib64/egl/libinterceptor_patrace_arm64.so" > /vendor/lib64/egl/interceptor.cfg  

    chmod 644 /vendor/lib/egl/interceptor.cfg  
    chmod 644 /vendor/lib64/egl/interceptor.cfg  

    mkdir /data/apitrace  
    chmod 777 /data/apitrace  

### Installing fakedriver on Android 9

Tracing on an Android 9 device is similar with tracing on Android 8. But some Project Treble enabled Android 9 devices forbid our fakedriver to load libstdc++.so from /system/lib(64)/egl, so we need to copy them to /vendor/lib(64)/ first.

You can check if your Android 8 device supports Treble in adb shell:

    getprop ro.treble.enabled

Return value is "true" means your device supports Treble. So you need to do the following copying first:

    cp /system/lib64/libstdc++.so /vendor/lib64/  
    cp /system/lib/libstdc++.so /vendor/lib/  

Then you can follow the steps in "Installing fakedriver on Android 8".

### Installing interceptor on Android 9

The same with "Installing interceptor on Android 8".

### Running Vulkan apps after installing fakedriver on Android 8 and 9

The Android Vulkan loader uses 32-bit and 64-bit Vulkan drivers here:

    /vendor/lib/hw/vulkan.<ro.product.platform>.so  
    /vendor/lib64/hw/vulkan.<ro.product.platform>.so  

If these 2 files are symbolic links to real DDK(`libGLES_mali.so`) and you renamed the real DDK for installing fakedriver, you need to recreate these symbolic links for Vulkan apps running.

### Tracing on Android

Find out the package name of the app you want to trace (i.e. `com.arm.mali.Timbuktu`, not just `Timbuktu`). The easiest way it to run the app, then run `top -m 5` or `ps`.

Add the name to `/system/lib/egl/appList.cfg` (32 bit) or `/system/lib64/egl/appList.cfg` (64 bit). This is a newline-separated list of the names of the apps that you want to trace (the full package name, not the icon name).

If `/system/lib[64]/egl/` does not exist, you can also use `/system/vendor/lib/egl/` is also OK.

Create the output trace directory in advance, which will be named /data/apitrace/&lt;package name&gt;. Give it 777 permissions.

Example:

    echo com.arm.mali.Timbuktu >> /system/lib/egl/appList.cfg
    chmod 644 /system/lib/egl/appList.cfg  
    mkdir -p /data/apitrace/com.arm.mali.Timbuktu
    chmod 777 /data/apitrace/com.arm.mali.Timbuktu

Make sure `/system/lib[64]/egl/appList.cfg` is world readable.

When you have run the application and want to stop tracing, go to home screen on the Android UI, then kill the application either using "adb shell kill &lt;pid&gt;" (do **not** use -9 here!) or use the Android UI to kill it. You need to make sure the application is properly closed before copying out the file, otherwise the trace file will not work. Do not use "Close all" from Android. After killing from adb shell, grep for "Close trace file " in logcat. If you find it, means the trace file was created properly.

If you do the above, but still have problems with the trace file being incomplete, which is usually seen by a lack of properly set headers ("paretrace -info" on the file gives you just zero values as output), you can configure patrace to save to disk every frame. This is not recommended for the general case, as it will slow down tracing considerably. For how to do this, see the "FlushTraceFileEveryFrame" configuration option below.

#### Tracing on Android L and M

You may need to do 'setenforce 0' in adb shell before tracing, otherwise you will get file open errors. This may need to be repeated after each reboot of the device. Note that on Android N and later, this may break the device.

#### Tracing non-native resolutions

It is possible to trace non-native resolutions on Android by switching window resolution through adb. Use "adb shell wm size WIDTHxHEIGHT", but you may need to add a few pixels to get the exact right size. Look in the "adb logcat" for the resolution traced, to verify. For example, on Nexus10, you need 2004x1080 resolution to trace 1080p content.

#### Tracing on Samsung S8

More restrictions have been added on Android. You will need to pre-create the target trace directory, adnd use chcon to set the Selinux file permissions on it, for example `chcon u:object_r:app_data_file:s0:c512,c768 /data/apitrace/com.futuremark.dmandroid`, in addition to setting the permissions to 777. You can also no longer 'adb pull' the file directly from this directory, but have to go by the way of /sdcard/.

#### Tracing on Android with Treble enabled

If the fakedriver doesn't seem to be picked up (nothing relevant printed in logcat), it might be because Android has Treble enabled. If so, put libs under `vendor/` and rename `libGLES_mali.so` to `lib_mali.so`.

### Tracing on Linux

Put the libegltrace.so file you built somewhere where you can access it, then run:

    LD_PRELOAD=/my/path/libegltrace.so OUT_TRACE_FILE=myGLB2 ./glbenchmark2

Use the `OUT_TRACE_FILE` variable to set the path and filename of the captured trace. The filename will be appended with an index followed by `.pat`. If `%u` appears in `OUT_TRACE_FILE`, it will be used as a placeholder for the index. E.g. `foo-%u-bar.pat`. If the `OUT_TRACE_FILE` variable is omitted, then the hard coded pattern, `trace.%u.pat`, will be used.

You may also specify the environment variables `TRACE_LIBEGL`, `TRACE_LIBGLES1` and `TRACE_LIBGLES2` to tell the tracer exactly where the various GLES DDK files are found. If you use these, you may be able to use `LD_LIBRARY_PATH` to the fakedriver instead of `LD_PRELOAD` of the tracer directly. In this latter case, set `INTERCEPTOR_LIB` to point to your tracer library.

The fakedriver may be hard to use on desktop Linux. We have poor experience using the Mali emulator as well, and recommend using a proper GLES capable driver.

### Tracer configuration file

The tracer can be configured through a special configuration file `$PWD/tracerparams.cfg` that contains configuration lines containing one keyword and one value which is usually "true" or "false".The following parameters can be specified in it:

-   EnableErrorCheck - Turn on or off saving errors to the trace file
-   UniformBufferOffsetAlignment - Change UBO alignment. By default this is set to the lowest common denominator for all relevant platforms.
-   ShaderStorageBufferOffsetAlignment - Change SSBO alignment. As above.
-   EnableActiveAttribCheck
-   InteractiveIntercept - Debugging tool
-   FilterSupportedExtension - Report only a specified list of extensions to the application.
-   FlushTraceFileEveryFrame - Make sure we save each frame to disk. Use if you have problems with trace being incomplete when retrieved from device.
-   StateDumpAfterSnapshot - Debugging tool
-   StateDumpAfterDrawCall - Debugging tool
-   SupportedExtension - Use this to specify which extensions to report to the application. One extension per keyword.

The most useful keyword is 'FilterSupportedExtension', which, if set to 'true', will fake the list of supported extensions reported to the application only a limited list of extensions. In this case, put each extension you want to support in the configuration file on a separate line with the 'SupportedExtension' keyword.

### Special features of PaTrace Tracer

You may enable or disable extensions, this is useful to create traces that don't use certain extensions. Limiting the extensions an app 'sees' is useful when you want to create a tracefile that is compatible with devices that don't support a given extension. How it works; when an app calls `glGetString(GL_EXTENSIONS)`, only the ones enabled in `/system/lib/egl/tracerparams.cfg` will be returned to the app. Unfortunately, some applications ignore or don't use this information, and may use certain extensions, anyways.The tracer will ignore some extensions like the binary shader extensions by default. You can override this by explicitly listing the extensions to use in `tracerparams.cfg`, as described 
above.The full list of extensions that the device supports will be saved in the trace header.

We never want binary shaders in the resulting trace file, since it can then only be retraced on the same platform. Since binary shaders are supported in GLES3, merely disabling the binary shader extensions may not be enough. You may have to go into Android app settings, and flush all app caches before you run the app to make sure the shader caches are cleared, 
before tracing it.

### Troubleshooting trace issues

1.  If the traced content uses multiple threads and/or multiple contexts, retracing may fail. Try running the pat-remap-egl python script that comes with patrace. See python tool installation instructions above.
2.  Did you remember to close the application correctly? See tracing instructions above.
3.  The tracer does not write directly to disk; instead it has a in-memory cache that is flushed to disk when full. The cache is currently hardcoded to 70MB, but can be decreased or increased. It was increased from 20MB to 70MB when it was discovered that a game allocating large textures (almost 4k by 4k) used more space that the maxium size of the cache. The downside of increasing the cache size is that we have a limited amount of memory on the devices we create traces on. The cache size is defined in `patrace/src/tracer/egltrace.hpp::TraceOut::WRITE_BUF::LEN`

Retracing
---------

### Performance measurements

One of PATrace's main targets has all along been about measuring performance, and due to it's fast binary format it's suitable for such tasks. There are a few different things to consider when doing performance measurements:

-   Set measurement range using the framerange option to include only the gameplay / main content, and avoiding any loading frames. The easiest way to find the relevant framerange is to use the -step option of paretrace.
-   Use the preload option to keep the selected framerange in memory to avoid disk IO.
-   If restricted by vsync or your screen is too small, use offscreen mode. Offscreen mode adds the overhead of an additional blit every 100 frames, but when running on silicon devices, it is usually the right option to use.

You can get detailed information saved to disk about each frame in 'results.json' with the 'collectors' options. For the options possible to set with this option, see the 'libcollector documentation' below.

Detailed call statistics about the time spent in each API call can be gathered with the 'callstats' option. The results will end up in a 'callstats.csv' file.

### Retracing on FPGA

On the FPGA with dummy winsys, you need to specify some extra environment variables. `MALI_EGL_DUMMY_DISPLAY_WIDTH` and `MALI_EGL_DUMMY_DISPLAY_HEIGHT` should be set to "4096". Set `LD_LIBRARY_PATH` to point to the location of your compiled DDK.

### Retracing on Android

-   Install `eglretrace-release.apk`
-   If you need to use the GUI, copy original/modified traces to `/data/apitrace/` or `/sdcard/apitrace/trace_repo`. (they will already be in `/data/apitrace` if they are traces from the device).
-   Run the `PARetrace` app.
-   Select a trace to start retracing it.
-   For extra parameters, such as setting a custom screen resolution, thread ID etc, start the retracer using am as shown below.

To launch the retracer from command line, you can use the following command:

    adb shell am start -n com.arm.pa.paretrace/.Activities.RetraceActivity --es fileName /absolute/path/to/tracefile.pat

For a full set of available parameters, see Command Line Parameters in ADB Shell.

To retrace some traces which contain external YUV textures on Android 7 or later, you need to add libui.so to /etc/public.libraries.txt out

    adb shell  
    su  
    mount -o rw,remount /  
    echo libui.so >> /etc/public.libraries.txt  

### Retracing On Desktop Linux

For GLES support on Linux desktop environments there are three options:

-   Use Nvidia's GLES capable driver
-   Use the Mali GLES Emulator
-   Use the MESA GLES libraries provided with Debian/Ubuntu (`libgles1-mesa libgles2-mesa`)

The preferred option is to use the latest Nvidia driver (see Tracing on desktop Linux). Using the MESA's GLES implementation is the least preferred option. Copy the trace file from the device and replay it with:

    paretrace TRACE_FILE.pat

The first time you run the file, it is recommended to add the "-debug" command line switch to see any error messages from the driver, which can reveal many issues.

### Retracing on embedded Linux

-   Just run paretrace &lt;tracefile.pat&gt;
-   If you're interested in the final FPS number then you will need to set a suitable frame range and use preload, i.e. "eglretrace -preload x y tracefile.pat"
-   See paretrace -h for options such as setting window size, retracing only a part of the file, debugging etc.
-   If your use case is more advanced (automation, instrumented data collection) you'll need to pass the parameters to the retracer as a JSON file - see PatracePerframeData for more information.

### Retracing on Midgard Model

You need the retracer for x86 built for **fbdev** window system (available as other binaries, see above) and with the same bit-ness as your model build. A 32-bit model requires a 32-bit retracer, and a 64-bit model requires a 64-bit retracer. This is because libMidgardModel.so will use the retracers libEGL.so and libGLESv2.so

Check out Midgard DDK and build it with e.g. `scons profile=x86-32-debug-dump`

Run the trace using e.g.

    LD_LIBRARY_PATH=path_to_driver_libraries MALI_SAVE_FRAMES_TO_FILE=1 paretrace input.pat

### Parameter options

There are three different ways to tell the retracer which parameters that should be used: (1) by command line, (2) in adb shell, and (3) by passing a JSON file.

#### Command Line Parameters 

| Parameter                                    | Description                                                                                                                                                                                                                            |
|----------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `-tid THREADID`                              | only the function calls invoked by the given thread ID will be retraced                                                                                                                                                                |
| `-s CALL_SET`                                | take snapshot for the calls in the specific call set. Example `*/frame` for one snapshot for each frame, or `250/frame` to take a snapshot just of frame 250.                                                                         |
| `-step`                                      | use F1-F4 to step forward frame by frame, F5-F8 to step forward draw call by draw call (only supported on desktop linux)                                                                                                               |
| `-ores W H`                                  | override resolution of onscreen rendering (FBO's are not affected)                                                                                                                                                                     |
| `-msaa SAMPLES`                              | enable multi sample anti alias                                                                                                                                                                                                         |
| `-preload START STOP`                        | preload the trace file frames from START to STOP. START must be greater than zero. Implies -framerange.                                                                                                                                |
| `-framerange FRAME_START FRAME_END`          | start fps timer at frame start, stop timer and playback at frame end. Frame start can be 0, but you usually want to measure the middle-to-end part of a trace, so you're not measuring time spent for EGL init and loading screens.    |
| `-debug`                                     | Output debug messages                                                                                                                                                                                                                  |
| `-skipwork WARMUP_FRAMES`                    | Discard GPU work outside frame range with given number of warmup frames. Requires GLES3. Works by calling glDiscardFramebuffer() before GLES sync point, and skipping compute calls.                                                   |
| `-singlewindow`                              | Force everything to render in a single window                                                                                                                                                                                          |
| `-singleframe`                               | Draw only one frame for each buffer swap (offscreen only)                                                                                                                                                                              |
| `-jsonParameters FILE RESULT_FILE TRACE_DIR` | path to a JSON file containing the parameters, the output result file and base trace path                                                                                                                                              |
| `-info`                                      | Show default EGL Config for playback (stored in trace file header). Do not play trace.                                                                                                                                                 |
| `-infojson`                                  | Show JSON header. Do not play trace.                                                                                                                                                                                                   |
| `-instr`                                     | Output the supported instrumentation modes as a JSON file. Do not play trace.                                                                                                                                                          |
| `-offscreen`                                 | Run in offscreen mode                                                                                                                                                                                                                  |
| `-overrideEGL`                               | Red Green Blue Alpha Depth Stencil, example: overrideEGL 5 6 5 0 16 8, for 16 bit color and 16 bit depth and 8 bit stencil                                                                                                             |
| `-strict`                                    | Use strict EGL mode (fail unless the specified EGL configuration is valid)                                                                                                                                                             |
| `-skip CALL_SET`                             | skip calls in the specific call set                                                                                                                                                                                                    |
| `-libEGL_path=`                              |                                                                                                                                                                                                                                        |
| `-libGLESv1_path=`                           |                                                                                                                                                                                                                                        |
| `-libGLESv2_path=`                           |                                                                                                                                                                                                                                        |
| `-version`                                   | Output the version of this program                                                                                                                                                                                                     |
| `-callstats`                                 | (since r2p4) Output GLES API call statistics to disk, time spent in API calls measured in nanoseconds.                                                                                                                                 |
| `-collect`                                   | (since r2p4) Collect performance information and save it to disk. It enables some default libcollector collectors. For fine-grained control over libcollector behaviour, use the JSON interface instead.                               |
| `-perf FRAME_START FRAME_END`                | (since r2p5) Create perf callstacks of the selected frame range and save it to disk. It calls "perf record -g" in a separate thread once your selected frame range begins.                                                             |
| `-perfpath filepath`                         | (since r2p5) Path to your perf binary. Mostly useful on embedded systems.                                                                                                                                                              |
| `-perffreq freq`                             | (since r2p5) Your perf polling frequency. The default is 1000. Can usually go up to 25000.                                                                                                                                             |
| `-perfout filepath`                          | (since r2p5) Destination file for your -perf data                                                                                                                                                                                      |
| `-noscreen`                                  | (since r2p4) Render without visual output using a pbuffer render target. This can be significantly slower, but will work on some setups where trying to render to a visual output target will not work.                                |
| `-flush`                                     | (since r2p5) Will try hard to flush all pending CPU and GPU work before starting the selected framerange. This should usually not be necessary.                                                                                        |
| `-multithread`                               | Enable to run the calls in all the threads recorded in the pat file. These calls will be dispatched to corresponding work threads and run simultaneously. The execution sequence of calls between different threads is not guaranteed. |
| `-insequence`                                | This option should be used after -multithread. It guarantees the calls in different work threads run in the sequence as recorded in the pat file.                                                                                      |

    CALL_SET = interval ( '/' frequency )
    interval = '*' | number | start_number '-' end_number
    frequency = divisor | "frame" | "draw"

#### Command Line Parameters in ADB Shell

| Command                    | Description                                                                                                                                                                                                                                                                                                                                                                  |
|----------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `--es fileName`            | /path/to/tracefile                                                                                                                                                                                                                                                                                                                                                           |
| `--ei tid`                 | thread ID to retrace                                                                                                                                                                                                                                                                                                                                                         |
| `--ei oresW`               | width                                                                                                                                                                                                                                                                                                                                                                        |
| `--ei oresH`               | height                                                                                                                                                                                                                                                                                                                                                                       |
|                            | Overriding resolution (oresW, oresH). This affects performance, scales the viewport to smaller or larger resolution, which results in a smaller or larger number of fragments shaded. Does not affect the trace-ed apps internal FBOs. Scale is relative to winW and winH, scaleRatioW = oresW / winW and same for height. If oresW=1 and winW=1, you won't see any scaling. |
| `--ei winW`                | width                                                                                                                                                                                                                                                                                                                                                                        |
| `--ei winH`                | height                                                                                                                                                                                                                                                                                                                                                                       |
|                            | Window size, affects snapshot dimensions. When overriding resolution, you usually want keep window size (winW,winH) the same as when tracing the application, as scaling is relative to winW,winH.                                                                                                                                                                           |
| `--ez forceOffscreen`      | true/false. When enabled, 100 frames will be rendered per visible frame, and rendering is no longer vsync limited. Frames are visible as 10x10 tiles.                                                                                                                                                                                                                        |
| `--es snapshotPrefix`      | /path/to/snapshots/prefix- Must contain full path and a prefix, resulting screenshots will be named prefix-callnumber.png                                                                                                                                                                                                                                                    |
| `--es snapshotCallset`     | call begin - call end / frequency, example: '10-100/draw' or '10-100/frame' or '10-100' (snapshot after every call in range!)                                                                                                                                                                                                                                                |
| `--ei frame_start`         | Start measure fps from this frame. First allowed frame number is 1 (not 0)                                                                                                                                                                                                                                                                                                   |
| `--ei frame_end`           | Stop fps measure, and stop playback                                                                                                                                                                                                                                                                                                                                          |
| `--ez preload`             | Loads calls for frames between `frame_start` and `frame_end` into memory. Useful when playback is IO-bound.                                                                                                                                                                                                                                                                  |
|                            | The following options may be used to override onscreen EGL config stored in trace header.                                                                                                                                                                                                                                                                                    |
| `--ei colorBitsRed`        | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ei colorBitsGreen`      | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ei colorBitsBlue`       | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ei colorBitsAlpha`      | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ei depthBits`           | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ei stencilBits`         | bits                                                                                                                                                                                                                                                                                                                                                                         |
| `--ez antialiasing`        | true/false Enable 4x MSAA.                                                                                                                                                                                                                                                                                                                                                   |
| `--es jsonData`            | path to a JSON file containing parameters, e.g. /data/apitrace/input.json. Only works together with traceFilePath and resultFile, any other options don't work anymore                                                                                                                                                                                                       |
| `--es traceFilePath`       | base path to trace file storage, e.g. /data/apitrace                                                                                                                                                                                                                                                                                                                         |
| `--es resultFile`          | path to output result file, e.g. /data/apitrace/result.json                                                                                                                                                                                                                                                                                                                  |
| `--ez multithread`         | Enable/Disable(default) Multithread execution mode.                                                                                                                                                                                                                                                                                                                          |
| `--ez insequence`          | Add after --ez multithread, true/false(default) to garantee all calls run in the sequence as recorded in the pat file.                                                                                                                                                                                                                                                       |
| `--ez enOverlay`           | If true(default), enable overlay all the surfaces when there is more then one surface created. If false, all the surfaces will be splited horizontally in a slider container.                                                                                                                                                                                                |
| `--ei transparent`         | The alpha value of each surface, when using Overlay layout. The defualt is 100(opaque).                                                                                                                                                                                                                                                                                      |
| `--ez force_single_window` | Ture/False(default) to force render all the calls onto a single surface. This can't be true with multithread mode enabled.                                                                                                                                                                                                                                                   |
| `--ez enFullScreen`        | Ture/False(defualt) to hide the system navigator and control bars.                                                                                                                                                                                                                                                                                                           |

#### Parameters from JSON file

A JSON file can be passed to the retracer via the -jsonParameters option. In this mode, the retracer will output a result file when the retracing has finished. The content should be a JSON object that contains the following keys:

| Key                          | Type       | Optional | Description                                                                                                                                                                                                                            |
|------------------------------|------------|----------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| colorBitsAlpha               | int        | yes      |                                                                                                                                                                                                                                        |
| colorBitsBlue                | int        | yes      |                                                                                                                                                                                                                                        |
| colorBitsGreen               | int        | yes      |                                                                                                                                                                                                                                        |
| colorBitsRed                 | int        | yes      |                                                                                                                                                                                                                                        |
| depthBits                    | int        | yes      |                                                                                                                                                                                                                                        |
| file                         | string     | no       | The file name of the .pat file.                                                                                                                                                                                                        |
| frames                       | string     | no       | The frame range delimited with '-'. The first frame must be 1 or higher                                                                                                                                                                |
| instrumentation              | list       | yes      | **(deprecated since r2p4)** See PATrace performance measurements setup for more information                                                                                                                                            |
| collectors                   | dictionary | yes      | (since r2p4) Dictionary of libcollector collectors to enable, and their configuration options. <br> Example:                              <br>                                                                            {                                                                                                                                                                                                                                                                                              "cpufreq": { "required": true },<br>                                                                                                                                                                                                 "rusage": {}<br>                                                                                                                                                                                                                                                                               } <br>                                                                                                                                                                                                                                 For description of the various collectors, see the libcollector documentation below.                                                                                                               |
| landscape                    | boolean    | yes      | Override the orientation                                                                                                                                                                                                               |
| offscreen                    | boolean    | yes      | Render the trace offscreen                                                                                                                                                                                                             |
| overrideHeight               | int        | yes      | Override height in pixels                                                                                                                                                                                                              |
| overrideResolution           | boolean    | yes      | If true then the resolution is overridden                                                                                                                                                                                              |
| overrideWidth                | int        | yes      | Override width in pixels                                                                                                                                                                                                               |
| preload                      | boolean    | yes      | Preloads the trace                                                                                                                                                                                                                     |
| removeUnusedVertexAttributes | boolean    | yes      | Modify the shader in runtime by removing attributes that were not enabled during tracing. When this is enabled, 'storeProgramInformation' is automatically turned on.                                                                  |
| stencilBits                  | int        | yes      |                                                                                                                                                                                                                                        |
| storeProgramInformation      | boolean    | yes      | In the result file, store information about a program after each glLinkProgram. Such as, active attributes and compile errors.                                                                                                         |
| threadId                     | int        | yes      | Retrace this specified thread id. **DO NOT USE** except for debugging!                                                                                                                                                                 |
| skipWork                     | int        | yes      | See command line options for Linux above.                                                                                                                                                                                              |
| offscreenSingleTile          | boolean    | yes      | Draw only one frame for each buffer swap in offscreen mode.                                                                                                                                                                            |
| multithread                  | boolean    | yes      | Enable to run the calls in all the threads recorded in the pat file. These calls will be dispatched to corresponding work threads and run simultaneously. The execution sequence of calls between different threads is not guaranteed. |
| insequence                   | boolean    | yes      | This option should be used after enabling "multithread". It guarantees the calls in different work threads run in the sequence as recorded in the pat file.                                                                            |

This is an example of a JSON parameter file:

    {
     "colorBitsAlpha": 0,
     "colorBitsBlue": 8,
     "colorBitsGreen": 8,
     "colorBitsRed": 8,
     "depthBits": 24,
     "file": "./egypt_hd_10fps.orig.std.etc1.gles2.pat",
     "frames": "4-200",
     "landscape": true,
     "offscreen": false,
     "overrideHeight": 720,
     "overrideResolution": true,
     "overrideWidth": 1280,
     "preload": true,
     "removeUnusedVertexAttributes": true,
     "stencilBits": 0,
     "storeProgramInformation": true
    }

For using it with the Linux retracer, use the following command line:

    paretrace -jsonParameters yourParameterFile.json result.json .

Other
-----

### PaTrace File Format

The latest .pat file format has the following structure:

1. Fixed size header containing that contains among other things;
    -   length of json string
    -   file offset where json string ends (from where we can begin reading sigbook and calls)
2. Variable length json string "header" described below.
3. A function signature book (or list) (sigbook), which maps EGL and GLES function names to id's (a number) used per intercepted call. This list is generated from khronos headers when compiling the tracer. When playing back a tracefile, the retracer reads the sigbook. The sigbook is compressed using the 'snappy' compression algorithm.
4. Finally the real content: intercepted EGL and GLES calls, which are also compressed with "snappy".
 
The variable length json "header" always contains:
-   default thread id
-   glesVersion (1,2,3)
-   frame count
-   call count
-   a list of per thread EGL configurations
-   per thread client side buffer use in bytes (non-VBO type data)
-   window width and height (winW, winH) captured from eglCreateWindowSurface

Debugging the interceptor on Android
------------------------------------

This section describes how to debug the tracer while it is tracing a third party Android app. This is solved by using gdb's remote debugging possibilities. Remote debugging with gdb is achieved by setting up a gdb server (`gdbserver`) on the Android device and connect to it with the gdb client (`gdb`) on your local machine. The method described in this section focuses on egltrace. However, this method can be used to debug any native Android library or app.

### Compile the interceptor

The egltrace project is normally built with optimizations and without debugging symbols. In order to be able to debug, optimizations should be disabled and debugging symbols must be generated.In the egltrace project (`content_capturing/patrace/project/android/egltrace`), edit the `jni/Application.mk` file:

-   Change `APP_OPTIM :=` `release` to `APP_OPTIM :=` `debug`
-   Remove the optimization flag, `-O3`, from `APP_CFLAGS` and replace it with `-g`

Compile the project with the `NDK_DEBUG=1` parameter:

    ndk-build NDK_DEBUG=1

### Installing built files

After a successful build, files in the `libs` and `obj` directories are created. These four files are relevant for remote debugging:

|                                                     |                                                                       |
|-----------------------------------------------------|-----------------------------------------------------------------------|
| `./libs/armeabi-v7a/gdb.setup`                      | configuration file for gdb client                                     |
| `./libs/armeabi-v7a/gdbserver`                      | gdbserver to be used on your Android device                           |
| `./libs/armeabi-v7a/libinterceptor_patrace.so`      | library with debug symbols stripped, to be used on the Android device |
| `./obj/local/armeabi-v7a/libinterceptor_patrace.so` | library with debug symbols, to be used by the gdb client              |

Upload `./libs/armeabi-v7a/libinterceptor_patrace.so` to your Android device to `/system/lib/egl`

    adb push ./libs/armeabi-v7a/libinterceptor_patrace.so /system/lib/egl

Upload `./libs/armeabi-v7a/gdbserver` to your Android device to `/data/local`

    adb push ./libs/armeabi-v7a/gdbserver /data/local

`./libs/armeabi-v7a/gdb.setup` is a configuration file for the gdb client. It instructs gdb where to search for libraries and source code. In this case, where to find the unstripped version of `libinterceptor_patrace.so`.

### Setup remote debugging

Forward port 5039 on your PC to port 5039 on your Android device:

    adb forward tcp:5039 tcp:5039

Open an adb shell:

    adb shell

Start the app that you want to trace on Android. Make sure that it is traced with `libinterceptor_patrace.so`. Find the app's process id by using `ps` in the adb shell.Start the gdb server on the Android device:

    /data/local/gdbserver :5039 --attach <YOUR-APP'S-PROCESS-ID>

The gdbserver is now listening for incoming connections.Open a new terminal on your desktop PC, and change directory to where you built the tracer (`content_capturing/patrace/project/android/egltrace`).You cannot use your normal `gdb` in order to remote debug the Android device. Instead, use a compatible gdb that is included in the Android NDK. Tell gdb to use the generated `gdb.setup file`. E.g.

    android-ndk-r8e/toolchains/arm-linux-androideabi-4.7/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gdb -x libs/armeabi-v7a/gdb.setup

Once gdb is started, tell it to connect to port 5039:

    target remote :5039

List all loaded libraries:

    info sharedlibrary

Make sure that you find `libinterceptor_patrace.so` in the list returned. Also make sure that is prepended with the path of where it is located on your PC. Example output:

    ...
     No librs_jni.so
     No libandroid.so
     No libwebcore.so
     No libsoundpool.so
    0x5bd48350 0x5be51dc8 No /work/dev/arm/content_capturing/patrace/project/android/egltrace/obj/local/armeabi-v7a/libinterceptor_patrace.so
     No libUMP.so
     No libMali.so
     No libGLESv2_mali.so
    ...

"No" means that the symbols are not loaded. To load the symbols run the commmand:

    sharedlibrary

Perform the `info sharedlibraries` command again, and make sure the symbols are now loaded for `libinterceptor_patrace.so` To set a breakpoint in `eglSwapBuffers`:

    break eglSwapBuffers

Now continue the execution of the app:

    continue

gdb should now break inside the the eglSwapBuffers.Now you can perform gdb debugging as normal.**Optional:** In order to see function names for system libraries, you can copy them to 
your local machine:

    adb pull /system/bin/ ./obj/local/armeabi-v7a/
    adb pull /system/lib/ ./obj/local/armeabi-v7a/

### CGDB

If you want to debug with `cgdb` use the `-d` parameter to specify the arm compatible gdb:

    cgdb -d <PATH-TO-GDB>/arm-linux-androideabi-gdb -x libs/armeabi-v7a/gdb.setup

### Break on app start

Sometimes it is desirable to break as early as possible in a started app. If an app crashes immediately when you start it, you do not have the time to find its PID, attach the gdbserver to the PID, and connect the gbd client, set break points, and so on.The solution is to add an infinite loop. This makes the app hang and gives you time to find its PID. Add the following code somewhere in the tracer library. For example in the constructor of `DllLoadUloadHandler` in `src/tracer/egltrace.cpp`

    int i = 1;
    while (i) {}

Perform the following steps that are described in more detail above:

1.  Compile the library
2.  Upload it to the device
3.  Start the app on the device
4.  Find the PID of the app
5.  Start the gdbserver on the device
6.  Connect the gdb client the gdb server
7.  Load sharedlibaries on the gdb client

The app is stuck in the infinite loop. In order to continue the execution of app, perform these steps in the gdb client

1.  `continue`
2.  Ctrl+C
3.  `set var i = 0`
4.  `next`

The `next` command will make you end up on the statement **after** the infinite loop. Now it is possible to debug as normal.

libcollector documentation
==========================

libcollector is a library for doing performance measurements.

All collectors can be configured with the following JSON options:

| Option     | What it does                                                                    | Values        | Default                   |
|------------|---------------------------------------------------------------------------------|---------------|---------------------------|
| "required" | Aborts the program if initializing the collector fails.                         | true or false | false                     |
| "threaded" | Run the collector in a separate background thread.                              | true or false | false (true for ferret)   |
| "rate"     | When run in a background thread, how often to collect samples, in milliseconds. | integer       | 100 (200 for ferret)      |

Existing collectors:

| Name                 | What it does                                                                                                                                                                            | Unit             | Options                             
|----------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------|-------------------------------------|
| `perf`               | Gets CPU cycles from perf                                                                                                                                                               | Cycles           |                                     
| `perf_record`        | Generates perf callstack data on disk for the relevant frame range.                                                                                                                     <br>**EXPERIMENTAL**                                                                                                                                                                         | -                |                                     |
| `battery_temperature`| Gets the battery temperature                                                                                                                                                            | Celsius          |                                     |                                                                                          |
| `cpufreq`            | CPU frequencies. Each sample is the average frequency used since the last sampling point for each core. Then you also get the max of every core in `highest_avg` for your convenience. | Hz               |                                     
| `memfreq`            | Memory frequency                                                                                                                                                                        |  Hz              |                                     |                                                                                          |
| `memfreqdisplay`     | Memory frequency                                                                                                                                                                        |  Hz |                                     |                                                                                          |
| `memfreqint`         | Memory frequency                                                                                                                                                           |  Hz |                                     |                                                                                          |
| `gpu_active_time`    | Time GPU has been active                                                                                                                                                                |                  |                                     |                                                                                          |
| `gpu_suspended_time` | Time GPU has been suspended                                                                                                                                                             |                  |                                     |                                                                                          |
| `cpufreqtrans`       | Number of frequency transitions the CPU has made                                                                                                                                        | Transitions      |                                     |
| `debug`              | **EXPERIMENTAL**                                                                                                                                                                        |                  |                                     |                                                                                          |
| `streamline`         | Adds streamline markers to the execution                                                                                                                                                |  None            |                                     |
| `memory`             | Track amount of memory used                                                                                                                                                             | Kilobytes        |                                     | 
| `cputemp`            | CPU temperature                                                                                                                                                                         |                  |                                     | 
| `gpufreq`            | GPU frequency                                                                                                                                                                           |                  | 'path' : path to GPU frequency file |
| `procfs`             | Information from /procfs filesystem                                                                                                                                                     | Various          |                                     | 
| `rusage`             | Information from getrusage() system call                                                                                                                                                | Various          |                                     | 
| `power`              | TBD                                                                                                                                                                                     |                  | TBD                                 | 
| `ferret`             | Monitors CPU usage by polling system files. Gives coarse per thread CPU load statistics (cycles consumed, frequencies during the rune etc.) | Various | 'cpus': List of cpus to monitor. Example: cpus: [0, 2, 3, 5, 7], will monitor core 0, 2, 3, 5 and 7. All work done on the other cores will be ignored.<br>This defaults to all cores on the system if not set.<br><br>'enable_postprocessing': Boolean value. If this is set, the sampled results will be postprocessed at shutdown. Giving per. thread derived statistics like estimated CPU fps etc. Defaults to false.<br><br>'banned_threads': Only used when 'enable_postprocessing' is set to true. This is a list of thread names to exclude when generating derived statistics. Defaults to: 'banned_threads': ["ferret"], this will exclude the CPU overhead added by the ferret instrumentation.<br><br>'output_dir': Path to an existing directory where sample data will be stored. |

Example
-------

The above names should be added as keys under the "collectors" dictionary in the input.json file:

    {
        "collectors": {
            "cpufreq": {},
                "gpufreq": {
                    "path": "/sys/kernel/gpu/gpu_clock"
                },
                "perf": {},
                "procfs": {},
                "rusage": {}
        },
        "file": "driver2.orig.gles3.pat",
        "frames": "1-191",
        "offscreen": true,
        "preload": true
    }


Generating CPU load statistics
------------------------------

You can get statistics for the CPU load (driver overhead) when running a trace by enabling the ferret libcollector module with postprocessing.

To do this, create a json parameter file similar to the one below (we will refer to it as parameters.json):

    {
        "collectors": {
            "ferret": {
                "enable_postprocessing": true,
                "output_dir": "<my_out_dir>"
            }
        },
        "file": <opath_to_my_trace_file>,
        "frames": "<start_frame>-<end_frame>",
        "offscreen": true,
        "preload": true
    }


Then run paretrace as follows:

    # Linux  
    paretrace -jsonParameters parameters.json results.json .  


    # Android (using adb)  
    adb shell am start -n com.arm.pa.paretrace/.Activities.RetraceActivity --es jsonData parameters.json --es resultFile results.json  


    # Android, using the (on device) android shell  
    am start -n com.arm.pa.paretrace/.Activities.RetraceActivity --es jsonData parameters.json --es resultFile results.json  


Once the run finishes the most relevant CPU statistics will be printed to stdout.

Detailed derived statistics will be available in the results.json file.

In depth (per. sample) data can be found in the `<my_out_dir>` folder specified in the parameters.json file.



The most interesting metrics are the following:

|Metric name stdout  | Metric name json | Calculated as	| Description |
|--------------------|------------------|---------------|-------------|
|CPU full system FPS@3GHz |	cpu_fps_full_system@3GHz | num_frames / (total_cpu_cycles / 3,000,000,000) | This metric represents the total amount of work done by the driver across all cores on the system.<br><br>Specifically, it is the maximum FPS the driver could deliver if it were to run on a single 3GHz CPU core comparable to the ones the CPU data was generated on.<br><br>This includes work done in all threads, and therefore it is not a good measure for the maximum throughput of the driver (as it is expected to be worse for multithreaded DDK's). Use it only as a indicator of how much work the driver does (and not necessarily how fast it does it). |
| CPU main thread FPS@3GHz | cpu_fps_main_thread@3GHz | num_frames / (heaviest_thread_cpu_cycles / 3,000,000,000) | This metric represents the work done by the heaviest thread in the driver.<br><br>This is the number that most accurately represents the maximum throughput CPU side. It will give you the maximum FPS the driver could deliver if the heaviest thread (assumed to also be the bottleneck) was to run on a 3GHz CPU core comparable to the ones the CPU data was generated on.<br><br>This number is the one to use if you are looking for a representation of the maximum FPS the target system could deliver if it was CPU bound. |



Getting robust results
----------------------

In order to get good results (with low error bars) it is essential that the content you are retracing runs for an extended period of time.

If the content in question has a high CPU load, running for 5+ seconds should give you good results. Still, running for longer will improve the quality of the data and reduce error bars.

If the content in question has a low CPU load, running for several minutes might be required.

In general, if the "active" times across all threads (as seen in the results.json file) is less than 0.2 for more than 4 cores (on a system with a clock tick time of 10ms) you should increase the runtime of your application until this is not the case.

A good rule of thumb is to ensure that all active times on all cores for all relevant threads is greater than the one divided by the clock ticks per seconf (`1 / _SC_CLK_TCK` on linux) times 20.

Note that this is rarely an issue for modern content. Ensuring a runtime of 30+ seconds will result in good data most of the time.

Also, you should configure your framerange such that all loading frame work is skipped (as loading assets is not relevant to the driver performance).



Calculating a CPU index number
------------------------------
A CPU index number is a single number that summarizes the CPU performance across multiple content entries. A good way to calculate this is to simply run a set of content entries and calculate the geometric mean of all the cpu_fps_main_thread@3GHz results.

This index can then be used to compare the results from different platforms with different drivers (assuming the content set is the same across all platforms and the CPU cores on them do not differ too much).
