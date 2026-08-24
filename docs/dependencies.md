# Dependency bootstrap

HOTAS BF6 v1.8.4 keeps driver ownership with the user. The Inno Setup wizard
detects vJoy and HidHide from uninstall registration plus their service/driver
registration; HidHide also uses Nefarius' documented dependency registry key.
Already-present dependencies are never downloaded, reinstalled, or downgraded.

If a component is missing, the user sees an explicitly selected option before
any download or vendor installer starts. The installer downloads only the exact
HTTPS release asset and SHA-256 pin in `packaging/dependencies.json`, then also
requires a valid Authenticode signature from the pinned subject. A checksum or
signature failure deletes the temporary payload and prevents execution.

The selected pins were verified on 2026-08-23 from their upstream GitHub
release assets:

| Component | Pin | Official asset | SHA-256 |
| --- | --- | --- | --- |
| vJoy | 2.1.9.1 x64 | `jshafer817/vJoy` `vJoySetup.exe` | `f103ced4e7ff7ccb49c8415a542c56768ed4da4fea252b8f4ffdac343074654a` |
| HidHide | 1.5.230 x64 | `nefarius/HidHide` `HidHide_1.5.230_x64.exe` | `f4bbbcb82e6258641b887c74bc81c4c5f66e4aa811808dfc304347687b7605f6` |

Both upstream installers were Authenticode-valid at verification time. Neither
vendor documents a silent switch used by this release, so HOTAS BF6 launches
the opted-in installer normally and waits for its result. UAC and any reboot
prompt remain under Windows and the vendor installer's control. HOTAS BF6
never restarts Windows automatically.

HidHide's current upstream release supports Windows 10/11 x64. Its upstream
release notes advise rebooting when prompted. vJoy Device 1 is not overwritten:
after any install, the user should configure X/Y/Z/Rz, 32 buttons, and at least
one continuous POV using the vendor's configuration tool if the existing device
does not already expose those capabilities.
