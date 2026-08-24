# Third-Party Notices

HOTAS BF6 Simple source code is licensed under the repository [MIT License](LICENSE). This notice does not grant rights to any third-party software, drivers, names, logos, or services.

## Runtime and packaging dependencies

- **Qt** — the application is built with Qt. Distributed Qt runtime files, when present in a release package, remain subject to the Qt license terms that accompany those files. See [Qt licensing](https://www.qt.io/licensing/).
- **vJoy** — HOTAS BF6 Simple dynamically uses an installed vJoy interface and can invoke the installed `vJoyConfig.exe` console utility. It does not vendor vJoy source or binaries. Obtain vJoy and its license terms from its upstream project.
- **HidHide** — HOTAS BF6 Simple can invoke an installed `HidHideCLI.exe` or open the installed HidHide client. It does not vendor HidHide source or binaries. Obtain HidHide and its license terms from the [official HidHide repository](https://github.com/nefarius/HidHide).
- **Inno Setup** — release packaging uses Inno Setup; its own license applies to that tool and any redistributed components.
- **Microsoft Windows / DirectInput** — Windows, DirectInput, ShellExecute, and related platform components are Microsoft technologies and remain subject to Microsoft terms.

## Trademarks and compatibility names

Battlefield, EA, EA Javelin, Thrustmaster, Logitech, Saitek, VKB, Virpil, WinWing, vJoy, HidHide, Qt, Inno Setup, and Microsoft are trademarks or product names of their respective owners. They are used here only to describe interoperability or configuration targets. No affiliation, sponsorship, endorsement, approval, or ownership claim is implied.

## Scope

HOTAS BF6 Simple does not include Battlefield, EA assets, controller vendor assets, vJoy source, HidHide source, or third-party driver binaries merely for setup convenience. Users remain responsible for obtaining and using third-party components under their applicable terms.
