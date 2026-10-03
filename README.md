# Day & Night Cycle v1.0

Day & Night Cycle adds manual and automatic lighting control to Two Point Museum.

## Features

- Select Dawn, Day, Dusk or Night lighting manually.
- Smooth transitions between lighting states.
- Run one complete 24-hour lighting cycle over an in-game week or month.
- Adjust the relative length of Dawn, Day, Dusk and Night with three timeline dividers.
- Restore the standard 24-hour phase distribution with one button.
- View the current lighting state and active automatic cycle in Cycle Settings.
- Browse Manual Options without disabling an active automatic cycle.
- Save the selected mode, manual state and custom phase lengths independently for each save slot, career and museum.
- Block clicks from passing through the settings window to museum objects behind it.
- Use native-style controls, tab states and tooltips.

## Installation

Copy `TPM-DayNightCycle.dll` and the `assets` folder into the game's `Mods` directory. The resulting layout should be:

```text
Mods/
├── TPM-DayNightCycle.dll
└── assets/
    ├── Dawn.png
    ├── Day.png
    ├── Dusk.png
    ├── Night.png
    └── ...button artwork
```

Keep the asset filenames unchanged.

## Controls

- Left-click the Day & Night Cycle HUD button to open Cycle Settings.
- Right-click the HUD button to advance through the manual lighting states.
- Manual Options selects Dawn, Day, Dusk or Night.
- Automated Options selects a Weekly or Monthly cycle.
- Drag the three timeline dividers to change the four displayed phase lengths.
- Use the refresh control to restore the standard 24-hour distribution.

Settings are stored independently for each save slot, career and museum under `Mods\Saves`.

Day & Night Cycle is self-contained. It does not depend on or call into another mod.

Runtime diagnostics are written to `Mods\Logs\DayNightCycle.log`. The mod creates both its save and log directories when needed.

## Build

Run `src\build.ps1` with the LLVM-MinGW toolchain path:

```powershell
.\src\build.ps1 -Toolchain 'C:\path\to\llvm-mingw' -BuildDirectory '.\build'
```

The build produces `TPM-DayNightCycle.dll` and treats all compiler warnings as errors.
