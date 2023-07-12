; This file implements checking for and installing runtime libraries for Aegisub

[Files]
DestDir: {tmp}; Source: "{#DEPS_DIR}\VC_redist\VC_redist.x{#ARCH}.exe"; Flags: nocompression deleteafterinstall
DestDir: {app}; Source: "{#DEPS_DIR}\XAudio2_redist\build\native\release\bin\x{#ARCH}\xaudio2_9redist.dll"; DestName: "XAudio2_9.dll"; OnlyBelowVersion: 10.0

[Run]
Filename: {tmp}\VC_redist.x{#ARCH}.exe; StatusMsg: {cm:InstallRuntime}; Parameters: "/install /quiet /norestart"
