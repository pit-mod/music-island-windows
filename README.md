# MusicIsland

A small music island at the top of your screen, built in C++ for Windows. It shows the current song, album cover and visualizer, with playback controls a click away.

[Download for Windows](https://github.com/pit-mod/music-island/releases/latest). Extract the zip and run `MusicIsland.exe`. No installation needed.

- Click to expand. Click outside to close.
- Drag the cover/title area to move it to another screen. It settles at the top center when released.
- Pull upward to hide it. Click the small handle to bring it back.
- Right-click for options. Optional arrow controls: left/right skip, up plays or pauses, down opens or closes. Text fields keep their arrows.

The island hides when no music is available. Works with Spotify and other players that support Windows media controls.

Requires Windows 10 1809+ or Windows 11, 64-bit.

To build, install Visual Studio's Desktop development with C++ workload and a Windows SDK, then run `./build.ps1` in PowerShell.
