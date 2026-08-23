# HOTAS BF6 App Icon Package v2

This revision fixes the outer transparency mask so the icon ends cleanly at the dark rounded body edge. The prior pale/white fringe and bottom-corner halo are removed.

## Included assets

- `hotas-bf6.ico`
  - 16×16
  - 24×24
  - 32×32
  - 48×48
  - 64×64
  - 128×128
  - 256×256
- `HOTAS-BF6-App.ico`
- `HOTAS-BF6-Launcher.ico`
- `HOTAS-BF6-Setup.ico`
- `png/`
  - transparent PNGs from 16×16 through 1024×1024
- `source/HOTAS_BF6_ICON_master_clean.png`
  - cleaned 1024×1024 transparent master
- `windows/resource.h`
- `windows/hotas-bf6.rc`

## Main application and launcher

Embed the `.ico` as a Windows resource.

Example:

```cmake
target_sources(HOTASMapper PRIVATE
    resources/hotas-bf6.rc
)
```

```rc
#include "resource.h"
IDI_APP_ICON ICON "hotas-bf6.ico"
```

Use the same approach for the launcher target.

## Qt window / taskbar icon

Also set the runtime window icon so title bar, taskbar, and Alt+Tab use the same artwork:

```cpp
#include <QGuiApplication>
#include <QIcon>

QGuiApplication app(argc, argv);
app.setWindowIcon(QIcon(":/icons/hotas-bf6-256.png"));
```

## Inno Setup

```ini
[Setup]
SetupIconFile=resources\hotas-bf6.ico
UninstallDisplayIcon={app}\HOTAS BF6 Launcher.exe

[Icons]
Name: "{autodesktop}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"; IconFilename: "{app}\HOTAS BF6 Launcher.exe"
Name: "{group}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"; IconFilename: "{app}\HOTAS BF6 Launcher.exe"
```

Use the same visual identity for the mapper, launcher/updater, installer, Desktop shortcut, Start Menu, taskbar, Alt+Tab, and Installed Apps entry.
