powershll里依次执行
```powershell
meson setup build --buildtype=release --reconfigure -Ddefault_library=static --force-fallback-for=zlib,harfbuzz,freetype2,fribidi,libpng -Dfreetype2:harfbuzz=disabled -Dharfbuzz:freetype=disabled -Dharfbuzz:cairo=disabled -Dharfbuzz:glib=disabled -Dharfbuzz:gobject=disabled -Dharfbuzz:tests=disabled -Dharfbuzz:docs=disabled -Dharfbuzz:icu=disabled -Dfribidi:tests=false -Dfribidi:docs=false -Dlibass:fontconfig=disabled -Davisynth=enabled -Dbestsource=enabled -Dvapoursynth=enabled --optimization=3
meson compile -C build -j(Get-WmiObject -Class Win32_Processor).NumberOfLogicalProcessors
#meson compile -C build --clean
#meson install -C build --destdir=install
meson compile win-installer -C build -j(Get-WmiObject -Class Win32_Processor).NumberOfLogicalProcessors
meson compile win-portable -C build -j(Get-WmiObject -Class Win32_Processor).NumberOfLogicalProcessors
```