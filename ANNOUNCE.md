The Wine development release 11.2 is now available.

What's new in this release:
  - More optimizations in PDB loading.
  - Support for MSVC constructors in C runtime.
  - Easier mechanism for creating version resources.
  - Various bug fixes.

The source is available at <https://dl.winehq.org/wine/source/11.x/wine-11.2.tar.xz>

Binary packages for various distributions will be available
from the respective [download sites][1].

You will find documentation [here][2].

Wine is available thanks to the work of many people.
See the file [AUTHORS][3] for the complete list.

[1]: https://gitlab.winehq.org/wine/wine/-/wikis/Download
[2]: https://gitlab.winehq.org/wine/wine/-/wikis/Documentation
[3]: https://gitlab.winehq.org/wine/wine/-/raw/wine-11.2/AUTHORS

----------------------------------------------------------------

### Bugs fixed in 11.2 (total 32):

 - #27269  Bitcoin 0.3.21 toolbar not displayed correctly
 - #33058  Visual Basic 6 crashes when object browser is clicked
 - #38183  King of Dragon Pass crashes when loading a saved game
 - #44548  Imperium GBR doesn't reproduce audio associated with videos when native dsound.dll is loaded
 - #45968  explorer.exe needs "Cascade Windows" function -> 'user32.CascadeWindows' implementation
 - #46197  explorer.exe needs "Tile Windows" implementation -> user32.TileWindows()
 - #46577  Black Mirror (2017) low performance in d3d11 mode
 - #46630  Futuremark 3DMark Vantage 1.1.x requires support for D3D11_FORMAT_SUPPORT_***
 - #50480  No audio in some movies in some Daedalic games (A new beginning & Night of the rabbit)
 - #50501  Default wrapping mode DWRITE_WORD_WRAPPING_WRAP is not handled correctly
 - #50681  The Hong Kong Massacre floor texture is glitchy with OpenGL renderer
 - #51426  Alacritty crashes on start (needs ResizePseudoConsole implementation)
 - #52497  Sam & Max Save the World: episode Culture Shock (GOG Original Edition) crashes when gameplay starts
 - #52592  MilkyTracker does not work: no valid waveout devices.
 - #54119  Fifa 2005 demo opens menu in 5 minutes
 - #54247  4Story launcher tries to download file but doesn't work
 - #57733  MMH7Editor is not started
 - #57856  Drop list is not working including Winecfg
 - #57893  Progress is not going until you move a mouse
 - #58167  Pegasus Mail immediately restores window
 - #58575  Low performance in an old Directx8 Sonic fangame in wined3d
 - #58919  osu!stable freezes when running on winewayland
 - #59120  xactengine3_7:xact3 is crashing since 2025-11-19 with debian 12 and older.
 - #59159  Divinity II: Developer's Cut crashes after showing the loading screen
 - #59181  GTA: San andreas/Vice city - Intro videos aren't properly rendered, showing only a white screen
 - #59234  Wine: Windows batch "WHERE" command return a wrong exit code in quiet mode
 - #59280  Wine 11.0 Shop Titans crash
 - #59290  Serial baud rates above 115200 not supported due to bug in serial.c
 - #59320  Gothic 1 Demo hangs on startup
 - #59350  Nexus Terminal install abended (regression)
 - #59373  Kyodai Mahjongg runs without audio in WOW64
 - #59374  Codename Panzers Phase 1 and 2: fail to start with EGL

### Changes since 11.1:
```
Alex Schwartz (1):
      winewayland: Fix non-square icons with xdg-toplevel-icon protocol.

Alexandre Julliard (25):
      winewayland: Fix build error with older wl_pointer interface.
      makefiles: Generate version resources from makefile variables.
      ntdll: Don't use 64-bit arguments for NtSetLdtEntries().
      winebuild: Disallow int64 and int128 types for syscall entry points.
      win32u: Specify NtUserCreateWindowEx handle arguments as ptr.
      faudio: Import upstream release 26.02.
      png: Import upstream release 1.6.54.
      xslt: Import upstream release 1.1.45.
      jpeg: Import upstream release 10.
      ntdll: Rename some builtin unixlib functions to reflect current usage.
      ntdll: Add a helper to retrieve a builtin module.
      ntdll: Add a helper to load the unixlib functions.
      ntdll: Add SIGQUIT to the blocked signals.
      ntdll: Add support for loading a unixlib with an explicit name.
      winecrt0: Add a helper for loading a unixlib by name.
      mmdevapi: Load the unixlib directly for audio drivers.
      makefiles: Add support for building pure unixlibs without a PE side.
      makefiles: Fix a typo.
      wow64: Add missing Unicode string mapping for MemoryWineLoadUnixLibByName.
      ntdll: Support a __wine_unix_lib_init entry point in unix libs.
      win32u: Use the __wine_unix_lib_init entry point.
      winex11.drv: Use the __wine_unix_lib_init entry point.
      include: Add a few more SAL macros.
      include: Add BitScanForward64 and BitScanReverse64.
      include: Add Int32x32To64 and UInt32x32To64.

Alistair Leslie-Hughes (1):
      inkobj: Correct install path.

Anton Baskanov (7):
      dmsynth: Set the event on the error path in synth_sink_render_thread.
      dmsynth: Exit the render thread when initialization fails.
      dmsynth: Try to maintain a fixed write latency by varying the wait time.
      dmsynth: Simplify synth_sink_wait_play_end by making it similar to the main rendering loop.
      dmsynth: Call GetCurrentPosition from a separate thread.
      dmsynth: Estimate a continuously-advancing buffer position for a more precise timing.
      dmsynth: Clear the buffer notifications before closing the event handle.

Bartosz Kosiorek (1):
      gdiplus/tests: Add tests for GdipWarpPath.

Bernhard Übelacker (3):
      shell32: Create parent directories when creating trash directory.
      winhttp/tests: Add broken in test_WinHttpGetProxyForUrl.
      shell32: Create parent directories when creating trash directory.

Biswapriyo Nath (8):
      include: Add D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR in d3d12video.idl.
      include: Add enum flag operators for D3D11_RLDO_FLAGS.
      include: Add ID3D12SDKConfiguration1 definition in d3d12.idl.
      include: Add ID3D12DeviceFactory definition in d3d12.idl.
      include: Add ID3D12DeviceConfiguration definition in d3d12.idl.
      include: Replace new D3D12_BARRIER_SYNC_INDEX_INPUT name in d3d12.idl.
      include: Add D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW in d3d12.idl.
      include: Add D3D12_MESSAGE_ID_INCOMPATIBLE_BARRIER_LAYOUT in d3d12sdklayers.idl.

Conor McCarthy (8):
      winegstreamer: Do not clear the output type in resampler SetInputType().
      winegstreamer: Support null type in resampler SetInputType().
      winegstreamer: Support null type in resampler SetOutputType().
      mf/tests: Test resampler output type after setting the input type.
      mf/tests: Validate the input type in the topology test sink.
      mf/tests: Do not expect stereo audio when a decoder and resampler are used.
      mf/tests: Add more topology loader tests.
      mf/tests: Test topology loader transform enumeration.

Dmitry Timoshkov (3):
      advapi32/tests: Add a test for creating service with empty display name.
      services: Treat empty service display name same way as NULL.
      advapi32/tests: Retry on failure instead of using unconditional Sleep().

Elizabeth Figura (9):
      ddraw: Enumerate the ramp device.
      ntoskrnl/tests: Test FileFsDeviceInformation.
      ntoskrnl/tests: Test DEVICE_OBJECT fields.
      ntoskrnl: Fill the Characteristics field of DEVICE_OBJECT.
      ntoskrnl: Handle FileFsDeviceInformation.
      wined3d: Clear backup_dc and backup_wnd when deleting them.
      d3d9/tests: Test more shaders and the FFP in shadow_test().
      d3d8/tests: Test more shaders and the FFP in shadow_test().
      wined3d/spirv: Pass vkd3d_shader_d3dbc_source_info.

Eric Pouech (15):
      dbghelp: Use same request as native to get TLS variable offset.
      dbghelp/pdb: Always load TPI header in init_DBI().
      dbghelp/pdb: Create symref for top and compilands.
      dbghelp/pdb: Introduce helper to search in DBI globals.
      dbghelp: Start implementing symbol information from symref_t.
      dbghelp: Allow symbol lookup methods to return symref instead of ptr.
      dbghelp/pdb: No longer create symt for top level global variables.
      dbghelp/pdb: No longer create symt for (file) local variables.
      dbghelp: Simplify check for local scope when removing a module.
      dbghelp: Pass a symref for lexical parent when creating a compiland.
      dbghelp/pdb: No longer use symt_module to store compilands.
      winedump: Misc improvements for dumping PDB files.
      winedump: Support ranges DBI (globals), TPI, IPI sections (PDB).
      winedump: Add ability to filter compilands (PDB).
      winedump: Dump PDB arm switch table and annotation codeview record.

Esme Povirk (7):
      wminet_utils: Add stub dll.
      wminet_utils: Stub Initialize.
      wminet_utils: Implement GetCurrentApartmentType.
      wminet_utils: Implement ConnectServerWmi.
      wminet_utils: Implement ExecQueryWmi.
      wminet_utils: Implement CloneEnumWbemClassObject.
      win32u: Actually return HKL for SPI_GETDEFAULTINPUTLANG.

Etaash Mathamsetty (2):
      ntoskrnl.exe: Implement KeAcquireGuardedMutex.
      ntoskrnl.exe: Implement KeReleaseGuardedMutex.

Gabriel Ivăncescu (2):
      jscript: Always treat DISPATCH_METHOD | DISPATCH_PROPERTYGET as method call if arguments are supplied.
      mshtml/tests: Test calling function object method with return value and arg in legacy modes.

Hans Leidekker (2):
      winedump: Print CLR string offset instead of index.
      odbcad32: Add stub program.

Jacek Caban (2):
      mshtml: Remove event handler when setting its property to a string in IE9+ modes.
      mshtml: Update element event handlers when the corresponding attribute value changes.

Jactry Zeng (4):
      include: Update STORAGE_BUS_TYPE in ntddstor.h.
      mountmgr.sys: Stub StorageDeviceTrimProperty query.
      kernel32/tests: Add tests of StorageDeviceTrimProperty query.
      winebus.sys: Add INOTIFY_CFLAGS to UNIX_CFLAGS.

Louis Lenders (1):
      kernelbase: Return S_OK in ResizePseudoConsole.

Myles Gray (1):
      ntdll: Report all possible serial baud rates.

Nikolay Sivov (23):
      dwrite/tests: Add more tests for GetClusterMetrics().
      dwrite/layout: Set RTL flag for clusters representing inline objects.
      dwrite/layout: Fix itemization with inline objects.
      dwrite/tests: Add more tests for whitespace flag of inline clusters.
      dwrite/layout: Add a helper for producing lines.
      dwrite/tests: Add some tests for DetermineMinWidth().
      dwrite/layout: Use whole text buffer to set line 'newline' length metric.
      dwrite/layout: Preserve whitespace flag for all types of clusters.
      dwrite/tests: Add another HitTestTextPosition() test.
      dwrite/layout: Rework line helper to take cluster count instead of upper boundary.
      dwrite/layout: Store full resolved level for each run.
      dwrite/tests: Add a test for newline clusters with HitTestTextPosition().
      dwrite/layout: Partially implement HitTestTextPosition().
      dwrite/layout: Improve support for wrapping modes.
      dwrite/layout: Implement run reordering.
      msxml3/sax: Add some traces for setting handlers.
      msxml3/tests: Add some tests for IVBSAXContentHandler behavior.
      msxml3/saxreader: Make sure VB startElement/endElement are never called with a NULL uri.
      d2d1/tests: Add some more tests for geometry groups.
      d2d1: Store original segment data when building paths.
      d2d1: Implement Stream() method for paths.
      d2d1: Add a way to stream any type of geometry internally.
      d2d1: Create a path internally for the geometry group.

Paul Gofman (1):
      opengl32: Pass app's FBO to set_current_fbo().

Piotr Caban (4):
      msado15/tests: Cleanup after _Recordset_put_Filter tests.
      msado15: Add helper for obtaining bookmark data.
      msado15: Add _Recordset::Find implementation.
      msado15/tests: Add _Recordset::Find tests.

Rémi Bernon (41):
      win32u: Extract pbuffer create/destroy to dedicated helpers.
      opengl32: Generate function pointers with wrapper types.
      opengl32: Move pbuffer handle allocation to the client side.
      opengl32: Return early on memory allocation failure.
      opengl32: Move context handle allocation to the client side.
      opengl32: Move current context error to the client wrapper.
      opengl32: Move sync handle allocation to the client side.
      winevulkan: Rename make_vulkan VkVariable type to type_name.
      winevulkan: Hoist some type info in local variables.
      winevulkan: Introduce a new Type base class for types.
      winevulkan: Implement require and set_order in the base class.
      winevulkan: Implement type aliasing with the base class.
      winevulkan: Get rid of make_vulkan type_info.
      opengl32: Fix a typo in client-side GLsync allocated object.
      winevulkan: Use the Define class for constants too.
      winevulkan: Simplify make_vulkan structure generation.
      winevulkan: Take all type dependencies into account when ordering.
      winevulkan: Enumerate types from the base Type class.
      winevulkan: Remove unnecessary make_vulkan is_alias.
      winevulkan: Generate function pointers interleaved with structs.
      winevulkan: Always sort constant and defines.
      winevulkan: Filter out non required types by default when enumerating.
      winevulkan: Inline make_vulkan loader_body method.
      winevulkan: Factor out pNext and sType name checks.
      winevulkan: Get rid of debug handles unwraps in struct chains.
      opengl32: Rename wgl_context to opengl_context.
      opengl32: Avoid creating contexts with unsupported HDCs.
      opengl32: Remove unnecessary null_get_pixel_formats.
      opengl32: Create a temporary window in copy_context_attributes.
      opengl32: Use separate functions to create / destroy / reset contexts.
      opengl32: Update make_opengl to latest spec revision.
      opengl32: Don't generate thunks for unexposed functions.
      opengl32: Alias GL_EXT_copy_texture and GL_VERSION_1_2.
      opengl32: Alias GL_ARB_texture_compression to GL_VERSION_1_3.
      opengl32: Remove remaining glVertexAttribDivisor altenative.
      opengl32: Generate GL/WGL/EGL extension list macros.
      opengl32: Parse extension aliases from the registry.
      opengl32: Support GLES and pass through extensions.
      opengl32: Add more extension aliases manually.
      win32u/tests: Test that window properties don't actually require atoms.
      server: Only try to grab atom for string window properties.

Stefan Dösinger (2):
      kernelbase: Don't write result on async NtWriteFile returns.
      kernelbase: Don't write result on async NtReadFile returns.

Thomas Csovcsity (1):
      where: Add quiet mode.

Tim Clem (4):
      winemac.drv: Only set the app icon once per process.
      ntdll: Report free space for "important" data on macOS.
      mountmgr.sys: Report free space for "important" data on macOS.
      winemac.drv: Unconditionally use CreateIconFromResourceEx for app icons.

Twaik Yont (7):
      explorer: Keep systray visible with taskbar enabled.
      wineandroid: Fix ANDROID_WindowPosChanged prototype in android.h.
      wineandroid: Drop leftover drawable_mutex after win32u OpenGL changes.
      wineandroid: Fix start_device_callback assignment type.
      win32u: Fix wineandroid build after OpenGL drawable refactoring.
      ntdll: Export Java globals for dlsym lookup.
      wineandroid: Fix WineAndroid device access path.

Yeshun Ye (3):
      cmd: Allow '/' in quoted 'WCMD_pushd' args.
      cmd/tests: Add test for 'start' with '/d'.
      start: Remove quotes from the path specified by '/d'.

Yuxuan Shui (13):
      winebuild: Generate start and end symbols for .CRT sections.
      winegcc: Merge .CRT sections for windows targets.
      include: Add prototype for _initterm.
      crt: Run MSVC constructors and destructors.
      qasf: Stop the WMReader first in asf_reader_destroy.
      qasf: Don't start a stopped stream in media_seeking_ChangeCurrent.
      kernel32/tests: Fix CreateToolhelp32Snapshot failure check.
      kernel32/tests: Handle ERROR_BAD_LENGTH from CreateToolhelp32Snapshot.
      include: Add TH32CS_SNAPMODULE32.
      kernel32: Fix CreateToolhelp32Snapshot on old WoW64.
      kernel32/tests: Test CreateToolhelp32Snapshot with TH32CS_SNAPMODULE32.
      kernel32: Implement TH32CS_SNAPMODULE32 support for CreateToolhelp32Snapshot.
      dbghelp: Rewrite EnumerateLoadedModulesW64 in terms of CreateToolhelp32Snapshot.

Zhiyi Zhang (10):
      twinapi.appcore: Add Windows.ApplicationModel.Core.CoreApplication activation factory.
      twinapi.appcore/tests: Add tests for Windows.ApplicationModel.Core.CoreApplication activation factory.
      twinapi.appcore/tests: Add tests for statics2_GetForCurrentView().
      twinapi.appcore: Add statics2_GetForCurrentView() stub.
      windows.ui: Implement uisettings_UIElementColor().
      windows.ui: Add ICoreWindowStatic stub.
      windows.ui/tests: Add tests for corewindow_static_GetForCurrentThread().
      windows.ui: Add corewindow_static_GetForCurrentThread() stub.
      d2d1: Warn in d2d_device_context_set_error().
      wined3d: Reset internal_format_set when using the backup DC.
```
