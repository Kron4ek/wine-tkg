The Wine development release 11.3 is now available.

What's new in this release:
  - Mono engine updated to version 11.0.0
  - Bundled vkd3d upgraded to version 1.19.
  - Improved FIR filter in DirectSound.
  - More optimizations in PDB loading.
  - Light theme renamed to Aero for compatibility.
  - Various bug fixes.

The source is available at <https://dl.winehq.org/wine/source/11.x/wine-11.3.tar.xz>

Binary packages for various distributions will be available
from the respective [download sites][1].

You will find documentation [here][2].

Wine is available thanks to the work of many people.
See the file [AUTHORS][3] for the complete list.

[1]: https://gitlab.winehq.org/wine/wine/-/wikis/Download
[2]: https://gitlab.winehq.org/wine/wine/-/wikis/Documentation
[3]: https://gitlab.winehq.org/wine/wine/-/raw/wine-11.3/AUTHORS

----------------------------------------------------------------

### Bugs fixed in 11.3 (total 30):

 - #40435  42Tags (.NET app) doesn't launch (System.Globalization.CultureNotFoundException: Culture ID 0 (0x0000) is not a supported culture.)
 - #44820  Processes might terminate with incorrect exit code due to race-condition when threads are killed during shutdown
 - #45343  Multiple apps (Vavoo Webinstaller,Kodi)  fail with windows version set to Win7: wmic.exe needs support for "/?" switch
 - #46384  Can't run zools backup tool on wine
 - #49453  ExtractNow: buttons do nothing / don't work / function (needs comctl32 version 6)
 - #50177  War Thunder Launcher doesn't render anything, just spams d2d_geometry_sink_AddArc stubs
 - #50449  zdaemon - Program freezes if both keyboard and mouse are used at the same time
 - #50814  Adobe Audition 2020 crashes on startup, reporting 'Direct2D Drawbot error' (d2d_device_context_Flush is a stub)
 - #54610  fusion:asmcache - 32-bit calls to InstallAssembly(..., NULL) crash on Windows 11
 - #54636  fusion:asmenum - test_enumerate() fails on Windows 11
 - #54656  The winscard tests are not run on the GitLab CI
 - #56927  msys2-64/cygwin64: rsync fails with 'Socket operation on non-socket'
 - #57435  D2D1GeometrySinc::AddArc() doesn't properly mark end of the figure segment
 - #57585  Many games focus poorly when alt-tabbing back into them, without UseTakeFocus=N
 - #58582  Rainmeter 4.5.23: crashes on X11 after refreshing default skin Clock.ini
 - #58911  doaxvv(DMM) occasionally crashes (WMV playback bug)
 - #58978  QuarkXPress 2024 crashes on start with "System.InvalidOperationException: The calling thread must be STA"
 - #59005  winhlp / winhelp32: Selecting "Edit > Copy" in Westwood Monopoly (1995) help file crashes
 - #59232  Flight Simulator 2000 has flickers and performance issues when rendering the 3D world with a 2D instrument panel in windowed mode
 - #59257  RTF clipboard reads randomly chosen string (regression from wine 10)
 - #59315  Sysinternals TCPView crashes on unimplemented function IPHLPAPI.DLL.GetOwnerModuleFromTcpEntry
 - #59323  Guild Wars 2 crashes with Wine 11.1 and Winewayland enabled with dev builds of DXVK (newer than Jan 23rd)
 - #59362  Gdiplus LockBits() flags=0 different behavior than Microsoft implementation
 - #59372  Regression: wine completely broken in the presence of non-graphics gl devices since 11.1
 - #59403  Missing parameter validation in printf() family of functions
 - #59404  Framemaker 8 crashes due to patch to sysparams.c
 - #59413  Unable to open a certain PNG image, 'copypixels_to_24bppBGR Unimplemented conversion path'
 - #59418  Potential off-by one in swapchain.c
 - #59426  iWin Games Manager V4 installer fails to create services
 - #59428  Amazon Chime 5.23 installer fails

### Changes since 11.2:
```
Alex Henrie (2):
      winhttp: Use the wcsdup function in set_cookies.
      winecfg: Use the wcsdup function instead of reimplementing it.

Alexandre Julliard (21):
      include: Define more XSTATE bits.
      include: Import apisetcconv.h from mingw-w64.
      include: Import wtypesbase.idl from mingw-w64.
      include: Add intsafe.h based on the mingw-w64 version.
      include: Fix POINTSTOPOINT definition.
      include: Add IsLFNDrive prototype.
      include: Implement strsafe.h based on the mingw-w64 header.
      libs: Add strsafe library.
      include: Update FMExtensionProc function signature.
      d3d11: Use nameless unions/structs.
      makedep: Look for explicitly listed header also in the source file directory.
      makedep: Use booleans where appropriate.
      libs: Remove some unused includes.
      include: Always define cdecl.
      include: Add GetNumberFormatEx prototype.
      msvcrt: Add wcstoimax/wcstoumax.
      include: Update timezone variable definitions.
      include: Use _CRT_ALIGN in msvcrt headers.
      include: Move the div() declaration back into the extern "C" block.
      configure: Treat the Windows build as a normal PE build.
      vkd3d: Import upstream release 1.19.

Alistair Leslie-Hughes (1):
      include: Avoid C++ keyword.

Andrew Nguyen (2):
      advapi32/tests: Add additional tests for interactive service creation.
      services: Allow ".\LocalSystem" account name for an interactive service.

Anton Baskanov (1):
      dsound: Use a better FIR filter generated with the Parks-McClellan algorithm.

Bernhard Übelacker (4):
      xaudio2_7/tests: Dynamically load function XAudio2CreateWithVersionInfo.
      gdiplus: Allow GdipBitmapLockBits without read/write flags.
      ws2_32: Allow using duplicated socket handles in closesocket().
      winegstreamer: Use a single call to free the streams array (ASan).

Brendan McGrath (25):
      mf/tests: Modify test_seek_clock_sink to be ref counted.
      mf/tests: Modify test_media_sink to store test_stream_sink.
      mf/tests: Add ClockStateSink to test media sink.
      mf/tests: Fix leaks in test source.
      mf/tests: Test that there is no pre-roll during scrubbing.
      mf/tests: Check that Rate change in PLAY state is ignored.
      mf: Make flushing on sample grabber largely a no-op.
      mf: Keep count of samples queued.
      mf: Always queue a sample unless stopped.
      mf: Ensure we always have four requests outstanding/satisfied.
      mf: Remove no longer used sample_count and samples array.
      mf: Request new sample before processing markers.
      mf/tests: Test SAR's IMFRateSupport interface.
      mf/tests: Test SAR's IMFPresentationTimeSource interface.
      mf/tests: Test SAR's SetPresentationClock.
      mf/tests: Test SAR timer with pre-roll and no duration.
      mf/tests: Test SAR timer with pre-roll and duration.
      mf/tests: Test SAR timer with ENDOFSEGMENT marker.
      mf/tests: Test SAR scrubbing start.
      mf: Don't perform preroll if we are scrubbing.
      mf/tests: Check for GetPresentationClock after MESessionStarted event.
      mf/tests: Test sequence of event processing on early sample request.
      mf: Fix crash when stream is null during sample request.
      mf/tests: Test media sessions sink request count.
      mf: Only decrement sink requests on delivery of sample.

Brendan Shanks (3):
      win32u: Support real handles to the current process in NtUserGetProcessDpiAwarenessContext().
      win32u: Support real handles to the current process in NtUserGetSystemDpiForProcess().
      joy.cpl: Avoid hangs when closing the panel.

Conor McCarthy (9):
      winegstreamer: Free the stream objects during reader destruction.
      mf/tests: Stop checking samples at the end of the expected array.
      mf/tests: Change todo to flaky for the H.264 decoder ouput type change test.
      mfplat/tests: Test NV12 negative stride in MFCreateMediaBufferFromMediaType().
      mf/tests: Test sample copier 2D buffers.
      mf/tests: Test H.264 decoder 2D buffers.
      mf/tests: Test WMV decoder 2D buffers.
      mf/tests: Test color convert 2D buffers.
      winegstreamer: Support 2D sample buffer.

Daniel Lehman (1):
      cldapi: Add stub dll.

Dmitry Timoshkov (3):
      dlls: Rename light.msstyles to aero.msstyles.
      uxtheme/tests: Add a test to show that current theme name is supposed to be "aero.msstyles".
      windowscodecs: Add fallback path to copypixels_to_24bppBGR().

Elizabeth Figura (11):
      ddrawex: Handle NULL desc in EnumDisplayModes().
      gdi32/tests: Test AddFontResource() search order.
      gdi32: Look for fonts in system32.
      wined3d: Remove the "iPixelFormat" field from struct wined3d_pixel_format.
      ntdll/tests: Add exhaustive tests for all directory information classes.
      kernel32/tests: Test whether EA size is returned from FindFirstFile().
      ntdll: Always initialize the reparse tag in get_file_info().
      ntdll: Return the reparse tag in EaSize for non-Extd directory classes.
      kernelbase: Use FileBothDirectoryInformation in FindFirstFile().
      ntdll: Initialize the entire FileId in FileIdExtdBothDirectoryInformation.
      ntdll: Initialize LockingTransactionId in FileIdGlobalTxDirectoryInformation.

Eric Pouech (16):
      dbghelp/pdb: Remove unused parameter.
      dbghelp/pdb: Fix incorrect offset when extracting S_CONSTANT name.
      dbghelp/pdb: Introduce iterator over DBI hash entries.
      dbghelp/pdb: Set new offset in pdb_reader_symbol_skip_if().
      dbghelp/pdb: Split loading of top level objects in compiland stream.
      dbghelp: Use symref_t as container for top level functions.
      dbghelp/pdb: Load symt_function on demand.
      dbghelp/pdb: No longer store symt_function in symt_compiland.
      dbghelp/pdb: Construct the global information from the hash table.
      dbghelp/pdb: Support rva inside symbol when searching globals.
      msvcrt: Ignore negative precision for float args in printf().
      include: Add BCRYPT_SUCCESS macro to bcrypt.h.
      include: Mark unused arguments as such in ws2tcpip.h.
      include/msvcrt: Use the correct stream for wscanf* implementations.
      include/msvcrt: Match some constants with intsafe.h.
      include/msvcrt: Define gmtime_s() in time.h.

Esme Povirk (5):
      gdiplus: Remove an assumption that an enum type is unsigned.
      mscoree: Update Wine Mono to 11.0.0.
      wine.inf: Add a default association for .reg files.
      wow64win: SPI_GETDEFAULTINPUTLANG result is pointer-sized.
      windowscodecs: Fix size check in gif copy_interlaced_pixels.

Etaash Mathamsetty (1):
      ntoskrnl: Use fastcall for KeInitializeGuardedMutex.

Filip Bakreski (1):
      mshtml: Add XMLSerializer implementation.

Georg Lehmann (1):
      winevulkan: Update to VK spec version 1.4.344.

Giovanni Mascellani (13):
      mmdevapi: Reject wave formats with zero samples per seconds.
      mmdevapi: Reject wave formats with zero block alignment.
      winecoreaudio.drv: Avoid a division by zero.
      winepulse.drv: Avoid a division by zero.
      winealsa.drv: Avoid a division by zero.
      wineoss.drv: Avoid a division by zero.
      winebus.sys: Define BUS_USB if it is missing.
      mmdevapi/tests: Reorganize rendering format tests.
      mmdevapi/tests: Use test contexts in rendering format tests.
      mmdevapi/tests: Introduce a helper to test rendering for a single format.
      mmdevapi/tests: Reorganize capturing format tests.
      mmdevapi/tests: Use test contexts in capturing format tests.
      mmdevapi/tests: Introduce a helper to test capturing for a single format.

Hans Leidekker (3):
      bcrypt: Add support for BCRYPT_ECC_CURVE_25519.
      bcrypt: Add support for importing and exporting generic ECDH keys.
      bcrypt: Compute derived key directly in BCryptSecretAgreement().

Jacek Caban (19):
      winebuild: Check for LLVM tool name too when using cc_command in find_tool.
      winegcc: Add --cc-cmd option support.
      makedep: Pass configured compiler with --cc-cmd to winegcc and winebuild.
      include: Add missing extern C to rtlsupportapi.h.
      include: Add missing exception macros.
      winecrt0: Add support for GCC-style constructors and destructors.
      ucrtbase: Add atexit support.
      include: Add erfc and nan declarations.
      include: Move float_t and double_t declarations from musl.
      include: Add at_quick_exit and quick_exit declarations.
      include: Don't use div wrappers for PE builds.
      include: Add fwide and mbsinit declarations.
      configure: Build PE modules with -mlong-double-64.
      include: Add long double math function declarations.
      compiler-rt: Import more builtin functions.
      configure: Use compiler-rt in mingw mode too.
      winegcc: Use compiler-rt instead of -lgcc on mingw targets.
      include: Don't use explicit alignment for 64-bit msvcrt types on PE targets.
      include: Don't use explicit alignment for 64-bit types on PE targets.

Janne Kekkonen (1):
      ws2_32: Add stub for WSCInstallProvider64_32.

Matteo Bruni (6):
      mmdevapi: Move some declarations to mmdevapi_private.h.
      mmdevapi: Allow querying I{Simple,Channel}AudioVolume from IAudioSessionControl.
      mmdevapi: Add some traces around session retrieval.
      mmdevapi: Add some session_wrapper traces.
      dsound: Set all channel volumes to 0 when DSBVOLUME_MIN is passed in.
      dsound: Flush denormals in the mixing thread.

Nikolay Sivov (11):
      d2d1: Correct arc rotation angles for ellipse segments.
      d2d1: Implement arc approximation with Bezier segments.
      d2d1: Forward geometry group methods to the path representing the group.
      d2d1/tests: Add another test for transformed geometry behavior.
      d2d1: Use non-multiplied transform when forwarding transformed geometry methods.
      msxml3/tests: Add more tests for createNode().
      msxml3: Handle type names in createNode().
      msxml3/tests: Add some SAX tests for different encodings.
      msxml3/tests: Add doctype SAX parsing test.
      msxml3/tests: Use wide strings for property names.
      msxml3/tests: Add a test for handler property types in VB API.

Patrick Zacharias (1):
      wined3d: Access the right pixel format in swapchain_gl_present().

Paul Gofman (7):
      iphlpapi: Implement GetOwnerModuleFromTcpEntry().
      iphlpapi: Implement GetOwnerModuleFromTcp6Entry().
      win32u: Register WGL_ARB_multisample extension for EGL backend.
      win32u: Set pbuffer drawable to internal context in wglBindTexImageARB().
      opengl32: Create compatibility context in wrap_wglCreateContext().
      win32u: Don't present new drawable in context_sync_drawables().
      opengl32: Also bind default framebuffer in pop_default_fbo().

Piotr Caban (4):
      riched20: Fix crash when copying text.
      ucrtbase: Fix scanset character range handling in scanf.
      advapi32: Fix memory leak in ReportEventA.
      bcrypt: Support BCRYPT_PBKDF2_ALG_HANDLE pseudo handle.

Piotr Pawłowski (1):
      msvcrt: Create copy of C++ exception object in std::rethrow_exception().

Rémi Bernon (32):
      winemac: Register clipboard format with NtUserRegisterWindowMessage.
      winewayland: Register clipboard format with NtUserRegisterWindowMessage.
      winex11: Register clipboard format with NtUserRegisterWindowMessage.
      setupapi: Fix some cfgmgr32 function signatures.
      cfgmgr32/tests: Add missing newline.
      include: Update cfgmgr32.h definitions.
      cfgmgr32: Add missing entries to cfgmgr32.spec.
      win32u: Ignore EGL device if context cannot be created.
      imm32/tests: Add some tests with ImmToAsciiEx bypass.
      cfgmgr32: Split sources to separate files.
      cfgmgr32: Implement CM_Get_Class_Key_Name(_Ex)(A|W).
      setupapi: Forward CM_Get_Class_Key_Name(_Ex)(A|W) to cfgmgr32.
      cfgmgr32: Implement CM_Open_Class_Key(_Ex)(A|W).
      setupapi: Forward CM_Open_Class_Key(_Ex)(A|W) to cfgmgr32.
      cfgmgr32: Cache some common root registry keys.
      cfgmgr32: Implement CM_Enumerate_Classes(_Ex).
      setupapi: Forward CM_Enumerate_Classes(_Ex) to cfgmgr32.
      win32u: Only link client surfaces after they are fully created.
      cfgmgr32: Implement CM_Enumerate_Enumerators(_Ex)(A|W).
      setupapi: Forward CM_Enumerate_Enumerators(_Ex)(A|W) to cfgmgr32.
      cfgmgr32: Implement CM_Open_Device_Interface_Key(_Ex)(A|W).
      cfgmgr32: Implement CM_Get_Class_Registry_Property(A|W).
      setupapi: Forward CM_Get_Class_Registry_Property(A|W) to cfgmgr32.
      cfgmgr32: Implement CM_Get_Class_Property(_Ex)W.
      win32u: Don't offset client rect when using D3D present rect.
      win32u: Or the GL_FLUSH_INTERVAL flag when updating the swap interval.
      user32/tests: Test EnableMouseInPointer message translation.
      win32u: Implement NtUserEnableMouseInPointer semi-stub.
      ddraw: Pass the active device to material_activate.
      win32u: Only update host window state if clip_client changes.
      winevulkan: Only filter out CLOCK_MONOTONIC(_RAW)_EXT time domains.
      winevulkan: Implement support for VK_EXT_present_timing.

Tim Clem (1):
      avicap32: Fail gracefully if V4L is unavailable.

Vibhav Pant (5):
      windows.devices.bluetooth/tests: Add initial tests for BluetoothAdapter.
      windows.devices.bluetooth: Implement BluetoothAdapter::GetDefaultAsync.
      windows.devices.bluetooth: Implement BluetoothAdapter::FromIdAsync.
      windows.devices.bluetooth: Implement BluetoothAdapter::get_BluetoothAddress.
      bluetoothapis: Simplify BluetoothGATTGetServices.

Viktor Balogh (1):
      dinput: Return DIERR_UNSUPPORTED in dinput_device_Escape.

Zhiyi Zhang (7):
      comctl32/tests: Add image swapping test for ImageList_Copy().
      comctl32/imagelist: Fix swapping images in ImageList_Copy().
      d2d1: Return S_OK for d2d_device_context_Flush().
      comctl32/tests: Test ACM_OPEN with a NULL LPARAM.
      comctl32/animate: Always return FALSE for ACM_OPEN with a NULL LPARAM.
      gdi32/tests: Add tests for GdiTransparentBlt() with 32-bit DIB bitmaps.
      win32u: Ignore alpha channels for 32-bit DIB bitmaps.

Ziqing Hui (4):
      mfreadwrite: Implement stream_create_transforms helper.
      mfreadwrite: Rename update_media_type_from_upstream and make it non-static.
      mfreadwrite: Implement transforms creation for writer.
      mfreadwrite: Create converter if failed to use a single encoder.
```
