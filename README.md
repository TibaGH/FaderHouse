# Faderhouse

Your own audio mixer in the spirit of Elgato Wave Link, without the vendor lock-in.

- Create as many channels as you like and assign apps to them, such as Discord, your game, the browser or TIDAL.
- Every mix has a console-style fader per channel, with mute and solo.
- Your mic goes through its own VST3/AU plugin chain straight to Discord and OBS. For that, Faderhouse has its own virtual microphone: **Mic FX**.

Faderhouse 1.0 runs on **macOS 14.2 or later** on Apple Silicon Macs. A Windows version is planned.

## Installing

1. Download the latest installer from **[Releases](https://github.com/TibaGH/faderhouse/releases/latest)**:
   - **`Faderhouse-x.y.z.pkg`** (recommended): installs Faderhouse into Applications and the Mic FX virtual microphone in one go.
   - **`Faderhouse-x.y.z.dmg`**: the familiar window where you drag Faderhouse into Applications. Faderhouse offers to install Mic FX on first launch.
2. The installers are not signed with a paid Apple Developer account, so macOS blocks them the first time. Go to System Settings → Privacy & Security and click **Open Anyway** at the bottom. You only need to do this once.
3. On first launch, allow access to the **microphone** and to **system audio recording**. Faderhouse needs these to use your interface's inputs and to capture the audio of individual apps.

## Getting started

1. **Settings → Audio interface:** choose your USB/Thunderbolt interface as input and output and enable the inputs you use. A buffer of 64–128 samples works well.
2. **Mic channel:** pick your mic input in the channel's dropdown.
3. **Plugins → Scan** to load your AU and VST3 plugins.
4. **Discord / OBS:** select **Mic FX** as your microphone, and turn off Discord's own noise suppression, echo cancellation and automatic gain: your plugin chain does that now.
5. **Assign apps** to channels with the button under each channel name. Drag channels by their header to reorder them.

## Updates

Faderhouse checks GitHub Releases once a day (and shortly after launch). When a new version is out, an **Update to x.y.z** button appears at the top of the mixer: it downloads the installer, verifies its SHA-256 checksum, opens it and closes Faderhouse; after installing, Faderhouse opens again. You can also check manually, or turn automatic checks off, in Settings → About & updates.

To publish an update: raise the version in `CMakeLists.txt` (`project(Faderhouse VERSION x.y.z ...)`) and push to `main`. When GitHub sees a version that has no release yet, it builds the installers and publishes release `vx.y.z` automatically. (Pushing a tag `vx.y.z` works too.)

## How it works

```
 Discord ─┐   Core Audio taps     ┌─ Discord ─ FX ─ fader/mute/solo ─┐
 Game    ─┤   (per app; the app   ├─ Game    ─ FX ─ fader/mute/solo ─┤
 Browser ─┤    itself goes        ├─ Browser ─ FX ─ fader/mute/solo ─┼─► Mix "Monitor" ─ FX ─ master ─► interface out 1/2
 Rest    ─┘    silent)            └─ System  ─ FX ─ fader/mute/solo ─┘
 Mic (interface in) ───────────────── Mic     ─ FX ─┬─ fader/mute/solo ──► (in Monitor, off by default)
                                                    └─ direct, no fader ──► Mic FX ─► Discord / OBS
```

- **Mic FX** always carries only your mic, after its plugins and without a fader; set the gain on your interface. Choose which input channels feed it with *Send to Mic FX* in a channel's ⋯ menu.
- **MIC LIVE / MIC MUTED** at the top mutes your mic everywhere: in Mic FX and in your monitor. The shortcut is ⌘⇧M; it works while Faderhouse is the active window.
- **Mute** and **Solo** on a channel work per mix. Solo plays only the soloed channels in that mix. Alt-click Solo to solo just that one channel.
- **Faders:** double-click resets a fader to 0 dB, Shift-drag adjusts finely, and the very bottom is −∞.
- **Plugins:** click a slot to open the plugin, or × to remove it. Right-click for bypass and order.
- **Icons:** click the icon at the top left of a channel. Choose one of the built-in icons or the real app icons on your Mac. *Automatic* uses the icon of the assigned app.
- **Assigning apps:** the list shows only real apps, with their helper processes grouped under them. You can show system processes at the bottom of the list.

**About plugins:** Faderhouse hosts **AU** and **VST3**. **AAX is not possible**: it is Avid's format and only runs inside Pro Tools. Install your plugins as AU or VST3 as well. With most vendors you tick that in their installer, at no extra cost.

Faderhouse saves everything automatically in `~/Library/Application Support/Faderhouse/`.

## Building it yourself

All you need is Apple's compiler: Xcode or the Command Line Tools.

```bash
./build.sh            # builds Faderhouse.app (fetches a private CMake into .tools/ if needed)
./package.sh          # makes dist/Faderhouse-x.y.z.pkg and .dmg
```

## Uninstalling

1. Remove Mic FX in Settings → Mix routing → **Remove**.
2. Move Faderhouse from Applications to the Trash.
3. Optionally delete `~/Library/Application Support/Faderhouse/`.

## Project layout

| Folder | Contents |
|---|---|
| `Source/Engine/` | Mixer engine, clock bridge, plugin chains, extra outputs, session |
| `Source/Capture/` | Per-app capture (macOS: Core Audio process taps) |
| `Source/VirtualMic/` | The Mic FX driver (Core Audio HAL plug-in via libASPL) and its installer |
| `Source/UI/` | Interface: console faders, keys, icons, strips, settings, logo |
| `Resources/` | App icon and DMG background (made with `Tools/IconRender`) |
| `.github/workflows/` | Automatic macOS build and installers |

## Known limitations

- **AAX plugins** can't be hosted (see above).
- **Plugin crashes during use:** plugins are scanned in a separate process, so a crashing plugin can't take Faderhouse down while scanning. While a plugin runs in a channel it does run inside Faderhouse, so a plugin that crashes then also closes Faderhouse.
- **Global shortcut:** ⌘⇧M only works while Faderhouse is the active window.
- **macOS only** for now.
