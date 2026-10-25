# Faderhouse

Your own audio mixer in the spirit of Elgato Wave Link, without the vendor lock-in.

- Create as many channels as you like and assign apps to them, such as Discord, your game, the browser or TIDAL.
- Every mix has a console-style fader per channel, with mute and solo.
- Your mic goes through its own VST3/AU plugin chain straight to Discord and OBS, through a virtual microphone: **Mic FX**.

## Download

Get the installer for your computer from **[Releases](https://github.com/TibaGH/faderhouse/releases/latest)**:

| Your computer | Download | Needs |
|---|---|---|
| **Mac** | `Faderhouse-x.y.z.pkg` (recommended) or `Faderhouse-x.y.z.dmg` | macOS 14.2 or later, Apple Silicon |
| **Windows PC** | `Faderhouse-x.y.z-Setup.exe` | Windows 11, 64-bit. An ASIO driver for your interface is recommended. |

Both versions have the same mixer, plugin chains, mixes and updates. The differences are listed under [Windows](#windows).

## Installing on a Mac

1. Download one of the two:
   - **`Faderhouse-x.y.z.pkg`** (recommended): installs Faderhouse into Applications and the Mic FX virtual microphone in one go.
   - **`Faderhouse-x.y.z.dmg`**: the familiar window where you drag Faderhouse into Applications. Faderhouse offers to install Mic FX on first launch.
2. The installers are not signed with a paid Apple Developer account, so macOS blocks them the first time. Go to System Settings → Privacy & Security and click **Open Anyway** at the bottom. You only need to do this once.
3. On first launch, allow access to the **microphone** and to **system audio recording**. Faderhouse needs these to use your interface's inputs and to capture the audio of individual apps.

## Installing on Windows

1. Download **`Faderhouse-x.y.z-Setup.exe`** and run it. The installer isn't signed with a paid certificate, so Windows SmartScreen may warn you the first time: click **More info → Run anyway**.
2. **Mic FX:** Windows only loads audio drivers that Microsoft has signed, so Faderhouse can't bring its own virtual microphone there. The installer therefore also installs **[VB-CABLE](https://vb-audio.com/Cable/)** by VB-Audio (tick box, on by default). Windows asks once whether to trust VB-Audio's driver; click **Install**, and restart your PC when the installer asks. Faderhouse then sends your processed mic into it and makes **CABLE Output** your Windows default microphone, so Discord / OBS set to *Default* pick it up. VB-CABLE is donationware: if you like it, support VB-Audio at [www.vb-cable.com](https://vb-audio.com/Cable/).
3. In Faderhouse, **Settings → Audio interface:** choose your interface's **ASIO** driver. See [Windows](#windows) for why.

## Getting started

1. **Settings → Audio interface:** choose your USB/Thunderbolt interface as input and output and enable the inputs you use. A buffer of 64–128 samples works well. On Windows, pick the **ASIO** device type.
2. **Mic channel:** pick your mic input in the channel's dropdown.
3. **Plugins → Scan** to load your plugins (AU and VST3 on a Mac, VST3 on Windows).
4. **Discord / OBS:** select **Mic FX** (Mac) or **CABLE Output** (Windows) as your microphone, and turn off Discord's own noise suppression, echo cancellation and automatic gain: your plugin chain does that now.
5. **Assign apps** to channels with the button under each channel name. Drag channels by their header to reorder them.

## Updates

Faderhouse checks GitHub Releases once a day (and shortly after launch). When a new version is out, an **Update to x.y.z** button appears at the top of the mixer: it downloads the installer for your platform, verifies its SHA-256 checksum, starts it and closes Faderhouse; after installing, Faderhouse opens again. You can also check manually, or turn automatic checks off, in Settings → About & updates.

To publish an update: raise the version in `CMakeLists.txt` (`project(Faderhouse VERSION x.y.z ...)`) and push to `main`. When GitHub sees a version that has no release yet, it builds the Mac and Windows installers and publishes release `vx.y.z` with both automatically. (Pushing a tag `vx.y.z` works too.)

## How it works

```
 Discord ─┐   per-app capture     ┌─ Discord ─ FX ─ fader/mute/solo ─┐
 Game    ─┤   (Mac: Core Audio    ├─ Game    ─ FX ─ fader/mute/solo ─┤
 Browser ─┤    taps; Windows:     ├─ Browser ─ FX ─ fader/mute/solo ─┼─► Mix "Monitor" ─ FX ─ master ─► interface out 1/2
 Rest    ─┘    process loopback)  └─ System  ─ FX ─ fader/mute/solo ─┘
 Mic (interface in) ───────────────── Mic     ─ FX ─┬─ fader/mute/solo ──► (in Monitor, off by default)
                                                    └─ direct, no fader ──► Mic FX ─► Discord / OBS
                                                                            (Windows: VB-CABLE)
```

- **Mic FX** always carries only your mic, after its plugins and without a fader; set the gain on your interface. Choose which input channels feed it with *Send to Mic FX* in a channel's ⋯ menu.
- **MIC LIVE / MIC MUTED** at the top mutes your mic everywhere: in Mic FX and in your monitor. The shortcut is ⌘⇧M (Ctrl+Shift+M on Windows); it works while Faderhouse is the active window.
- **Mute** and **Solo** on a channel work per mix. Solo plays only the soloed channels in that mix. Alt-click Solo to solo just that one channel.
- **Faders:** double-click resets a fader to 0 dB, Shift-drag adjusts finely, and the very bottom is −∞.
- **Plugins:** click a slot to open the plugin, or × to remove it. Right-click for bypass and order.
- **Icons:** click the icon at the top left of a channel. Choose one of the built-in icons or the real app icons of your apps. *Automatic* uses the icon of the assigned app.
- **Assigning apps:** the list shows only real apps, with their helper processes grouped under them. You can show system processes at the bottom of the list.

**About plugins:** Faderhouse hosts **AU** and **VST3** on a Mac, and **VST3** on Windows. **AAX is not possible**: it is Avid's format and only runs inside Pro Tools. Install your plugins as AU or VST3 as well. With most vendors you tick that in their installer, at no extra cost.

Faderhouse saves everything automatically in `~/Library/Application Support/Faderhouse/` (Mac) or `%APPDATA%\Faderhouse\` (Windows).

## Windows

Faderhouse for Windows works like the Mac version, with a few differences that come from Windows itself:

- **Per-app capture** uses WASAPI *process loopback*, which needs **Windows 11** (build 20348 or newer). On Windows 10 the mixer, inputs, plugins and Mic FX work, but apps can't be put on channels.
- **Silencing the apps' normal output:** macOS can mute an app while Faderhouse captures it; Windows can't. Instead, while Faderhouse plays your mix on a different output than the Windows default (an **ASIO** driver, or another device), it **mutes the Windows default output device**. The apps then only reach your ears through Faderhouse. When Faderhouse closes, the mute is undone (also after a crash, on the next start). If Faderhouse plays on the same device as the Windows default through Windows Audio, it can't mute it and you'd hear apps twice; the status line in the mixer tells you.
- **ASIO and Pro Tools:** many ASIO drivers allow only one program at a time. If Pro Tools uses your interface's ASIO driver, Faderhouse can't use it at the same time (unless your driver is multi-client).
- **Mic FX** goes into [VB-CABLE](https://vb-audio.com/Cable/) by VB-Audio (donationware, installed with Faderhouse): Windows only loads audio drivers signed by Microsoft, which needs a company certificate. Faderhouse makes **CABLE Output** the Windows default microphone once, and moves the Windows default output away from **CABLE Input** if Windows put it there, so apps don't play into your mic.
- **System channel:** everything that plays and isn't on another channel. Windows' own notification sounds that don't belong to an app may not be captured.
- **Plugins:** VST3 only (AU is a Mac format).

## Building it yourself

**Mac:** all you need is Apple's compiler: Xcode or the Command Line Tools.

```bash
./build.sh            # builds Faderhouse.app (fetches a private CMake into .tools/ if needed)
./package.sh          # makes dist/Faderhouse-x.y.z.pkg and .dmg
```

**Windows:** Visual Studio 2022 or newer with "Desktop development with C++" (includes CMake). For ASIO, download the [Steinberg ASIO SDK](https://www.steinberg.net/developers/) and point CMake at it; without it Faderhouse builds with Windows Audio devices only. The installer is made with [Inno Setup](https://jrsoftware.org/isinfo.php).

```powershell
cmake -S . -B build -A x64 -DFADERHOUSE_ASIO_SDK_DIR=C:\path\to\asiosdk
cmake --build build --config Release
iscc /DAppVersion=x.y.z installer\windows\Faderhouse.iss    # makes dist\Faderhouse-x.y.z-Setup.exe
```

The GitHub workflow does all of this for both platforms on every push.

## Uninstalling

**Mac:**
1. Remove Mic FX in Settings → Mix routing → **Remove**.
2. Move Faderhouse from Applications to the Trash.
3. Optionally delete `~/Library/Application Support/Faderhouse/`.

**Windows:** Settings → Apps → Installed apps → Faderhouse → **Uninstall**. Optionally delete `%APPDATA%\Faderhouse\`. VB-CABLE stays installed; to remove it, download the driver pack from [VB-Audio](https://vb-audio.com/Cable/), run `VBCABLE_Setup_x64.exe` as administrator and click *Remove Driver*.

## Project layout

| Folder | Contents |
|---|---|
| `Source/Engine/` | Mixer engine, clock bridge, plugin chains, extra outputs, session |
| `Source/Capture/` | Per-app capture (macOS: Core Audio process taps; Windows: WASAPI process loopback) |
| `Source/VirtualMic/` | The Mic FX driver for macOS (Core Audio HAL plug-in via libASPL) and its installer |
| `Source/UI/` | Interface: console faders, keys, icons, strips, settings, logo |
| `Resources/` | App icon and DMG background (made with `Tools/IconRender`) |
| `installer/windows/` | Inno Setup script for the Windows installer |
| `.github/workflows/` | Automatic Mac and Windows builds, installers and releases |

## Known limitations

- **AAX plugins** can't be hosted (see above).
- **Plugin crashes during use:** plugins are scanned in a separate process, so a crashing plugin can't take Faderhouse down while scanning. While a plugin runs in a channel it does run inside Faderhouse, so a plugin that crashes then also closes Faderhouse.
- **Global shortcut:** ⌘⇧M / Ctrl+Shift+M only works while Faderhouse is the active window.
- **Windows:** see [Windows](#windows) for what works differently there.
