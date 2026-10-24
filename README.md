# Faderhouse

Een eigen audiomixer à la Elgato Wave Link, zonder vendor lock-in.

- Je maakt zoveel kanalen als je wilt en koppelt er apps aan, zoals Discord, je game, de browser of TIDAL.
- Elke mix heeft per kanaal een analoge fader met mute en solo.
- Je mic gaat met zijn eigen VST3/AU-pluginketen direct naar Discord en OBS. Daarvoor heeft Faderhouse een eigen virtuele microfoon: **Mic FX**.

Faderhouse 0.3 draait op **macOS 14.2+**. De Windows-versie volgt.

## Installeren (zonder terminal)

GitHub bouwt bij elke push naar `main` automatisch de installers.

1. Ga op GitHub naar **Actions** → de laatste run van *Build macOS installers*, en download onderaan het artifact **Faderhouse-macOS**. Bij een versie-tag (`v0.3.0`) staan ze ook onder **Releases**.
2. Kies een van de twee installers:
   - **`Faderhouse-x.y.z.pkg`** (aanbevolen): installeert Faderhouse in Programma's én Mic FX in één keer.
   - **`Faderhouse-x.y.z.dmg`**: het bekende venster waarin je Faderhouse naar Programma's sleept. De app biedt bij de eerste start aan om Mic FX te installeren.
3. De installers zijn niet ondertekend met een betaald Apple Developer-account. Daarom blokkeert macOS de eerste keer. Ga dan naar Systeeminstellingen → Privacy en beveiliging en klik onderaan op **Toch openen**. Dat hoeft maar één keer.

## Hoe het werkt

```
 Discord ─┐   Core Audio taps     ┌─ Discord ─ FX ─ fader/mute/solo ─┐
 Game    ─┤   (per app; de app    ├─ Game    ─ FX ─ fader/mute/solo ─┤
 Browser ─┤    zelf wordt stil)   ├─ Browser ─ FX ─ fader/mute/solo ─┼─► Mix "Monitor" ─ FX ─ master ─► interface out 1/2
 Rest    ─┘                       └─ System  ─ FX ─ fader/mute/solo ─┘
 Mic (interface in) ───────────────── Mic     ─ FX ─┬─ fader/mute/solo ──► (in Monitor, standaard uit)
                                                    └─ direct, zonder fader ─► Mic FX ─► Discord / OBS
```

- **Mic FX** krijgt altijd alleen je mic, na de plugins en zonder fader; de gain stel je op je interface in. Welke inputkanalen erheen gaan, kies je in het ⋯-menu van een kanaal met *Send to Mic FX*.
- **MIC LIVE / MIC MUTED** bovenaan mute je mic overal: in Mic FX en in je monitor. De sneltoets is ⌘⇧M. Die werkt wanneer Faderhouse het actieve venster is.
- **Mute** en **Solo** op een kanaal werken per mix. Solo laat in die mix alleen de gesoloede kanalen horen. Met Alt-klik op Solo solo je alleen dat ene kanaal.
- **Faders:** dubbelklik zet een fader terug op 0 dB, Shift-slepen verstelt fijn, en helemaal onderaan is −∞.
- **Plugins:** klik op een slot om de plugin te openen, of op × om hem te verwijderen. Rechtsklik geeft bypass en volgorde.
- **Icoontjes:** klik op het icoon linksboven in een kanaal. Je kiest uit eigen icoontjes of uit de echte app-iconen van je Mac. *Automatic* gebruikt het icoon van de gekoppelde app.
- **App koppelen:** de lijst toont alleen echte apps, met hun helper-processen eronder gegroepeerd. Onderaan zet je de systeemprocessen alsnog aan.

**Over plugins:** Faderhouse host **AU** en **VST3**. **AAX kan niet**; dat formaat is van Avid en kan alleen in Pro Tools draaien. Installeer je plugins dus ook als AU of VST3. Bij de meeste fabrikanten vink je dat aan in hun installer, zonder extra licentie.

Faderhouse slaat alles automatisch op in `~/Library/Application Support/Faderhouse/`.

## Zelf bouwen

Je hebt alleen Apple's compiler nodig: Xcode of de Command Line Tools.

```bash
./build.sh            # bouwt Faderhouse.app (haalt zo nodig een eigen CMake op in .tools/)
./package.sh          # maakt dist/Faderhouse-x.y.z.pkg en .dmg
```

## Verwijderen

1. Verwijder Mic FX in Settings → Mix routing → **Remove**.
2. Gooi Faderhouse uit Programma's weg.
3. Verwijder eventueel ook `~/Library/Application Support/Faderhouse/`.

## Projectstructuur

| Map | Inhoud |
|---|---|
| `Source/Engine/` | Mixer-engine, klokbrug, pluginketens, extra outputs, sessie |
| `Source/Capture/` | Per-app capture (macOS: Core Audio process taps) |
| `Source/VirtualMic/` | De Mic FX-driver (Core Audio HAL-plugin via libASPL) en de installer |
| `Source/UI/` | Interface: console-faders, knoppen, iconen, strips, instellingen, logo |
| `Resources/` | App-icoon en DMG-achtergrond (gemaakt met `Tools/IconRender`) |
| `.github/workflows/` | Automatische macOS-build en installers |

## Wat wel en niet getest is

- **Getest:**
  - De gedeelde code en de interface draaien op Linux.
  - De ringbuffer van Mic FX en de klokbrug (die app-audio en je interface synchroon houdt) zijn met simulaties getest.
  - De oude app-capture, Mixwerk Mic en de installer uit versie 0.2 werkten bij jou op je Mac.
- **Niet getest, omdat ik geen Mac heb:** de onderdelen die alleen op macOS draaien en die nieuw zijn in 0.3:
  - de app-filter en de app-iconen;
  - de `.pkg`- en `.dmg`-installers.

  Dat de build op GitHub slaagt, laat zien of ze compileren.
