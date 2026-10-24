# Faderhouse

Your own audio mixer in the spirit of Elgato Wave Link, without the vendor lock-in.

- Create as many channels as you like and assign apps to them, such as Discord, your game, the browser or TIDAL.
- Every mix has a console-style fader per channel, with mute and solo.
- Your mic goes through its own VST3/AU plugin chain straight to Discord and OBS. For that, Faderhouse has its own virtual microphone: **Mic FX**.

Faderhouse 0.3 runs on **macOS 14.2+**. A Windows version is planned.

## Installing (no Terminal needed)

GitHub builds the installers automatically on every push to `main`.

1. On GitHub, open **Actions** → the latest run of *Build macOS installers*, and download the **Faderhouse-macOS** artifact at the bottom. Tagged versions (`v0.3.0`) are also listed under **Releases**.
2. Pick one of the two installers:
   - **`Faderhouse-x.y.z.pkg`** (recommended): installs Faderhouse into Applications and Mic FX in one go.
   - **`Faderhouse-x.y.z.dmg`**: the familiar window where you drag Faderhouse into Applications. The app offers to install Mic FX on first launch.
3. The installers are not signed with a paid Apple Developer account, so macOS blocks them the first time. Go to System Settings → Privacy & Security and click **Open Anyway** at the bottom. You only need to do this once.

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

## What has and hasn't been tested

- **Tested:**
  - The shared code and the interface run on Linux.
  - The Mic FX ring buffer and the clock bridge (which keeps app audio and your interface in sync) were tested in simulation.
  - Per-app capture, the virtual mic and its installer from version 0.2 worked on a real Mac.
- **Not tested yet on a real Mac** (new in 0.3):
  - the app filter and the app icons;
  - the `.pkg` and `.dmg` installers.

  A successful GitHub build only shows that they compile.
