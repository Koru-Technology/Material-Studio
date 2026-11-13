# Material Studio

A 3D texture painting app for iPad and Mac, based on [ArmorPaint](https://armorpaint.org). The engine (`base/`) is a fork of iron; the app lives in `paint/`. More developer docs are in [base/docs](base/docs/readme.md).

## Building

All commands run from `paint/`.

**Mac**

```sh
../base/make
cd build
xcodebuild -project MaterialStudio.xcodeproj -scheme MaterialStudio -configuration Release \
  -derivedDataPath dd_mac CODE_SIGN_IDENTITY="-"
```

The app is at `build/dd_mac/Build/Products/Release/MaterialStudio.app`.

**iPad**

```sh
../base/make --target ios
cd build
xcodebuild -project MaterialStudio.xcodeproj -scheme MaterialStudio -configuration Release \
  -destination generic/platform=iOS -derivedDataPath dd -allowProvisioningUpdates \
  PRODUCT_BUNDLE_IDENTIFIER=technology.koru.materialstudio DEVELOPMENT_TEAM=<team id> \
  CODE_SIGN_IDENTITY="Apple Development"
```

`CODE_SIGN_IDENTITY="Apple Development"` is needed; the generated project's default identity fails to sign. The team id is shown in Xcode under Signing & Capabilities, or in your Apple developer account.

## Installing on the iPad

The iPad must be connected by cable (or paired over the network), have Developer Mode on, and be unlocked when you install or launch.

Find the device id:

```sh
xcrun devicectl list devices
```

This lists the CoreDevice identifier (e.g. `188AF0D5-...`), used by `devicectl`. Tools such as pymobiledevice3 use the hardware UDID instead (e.g. `00008103-...`), shown by `xcrun xctrace list devices`.

Install the build:

```sh
xcrun devicectl device install app --device <device id> \
  build/dd/Build/Products/Release-iphoneos/MaterialStudio.app
```

The first install on a new device needs the developer certificate trusted on the iPad (Settings → General → VPN & Device Management).

## Running scripts and tests

The app runs a C script (minic) given with `--script`. The script API is listed in `paint/sources/minic_api_list.h`; the test suites are in `paint/tests/`. A script ends with `script_quit()`, which exits the app, so runs can be chained.

**Mac**

```sh
build/dd_mac/Build/Products/Release/MaterialStudio.app/Contents/MacOS/MaterialStudio \
  --script $PWD/tests/test.c > test.log 2>&1
```

Script output (`printf`) goes to stdout. Each suite prints a final `... PASS` or `... FAIL` line. The Mac renders very slowly while its display is off.

**iPad**

Copy the script into the app's Documents folder, then launch the app with it. `--console` streams the app's output to the terminal and waits until the app exits:

```sh
xcrun devicectl device copy to --device <device id> \
  --domain-type appDataContainer --domain-identifier technology.koru.materialstudio \
  --source tests/test.c --destination Documents/test.c

xcrun devicectl device process launch --console --terminate-existing \
  --device <device id> technology.koru.materialstudio --script "~/Documents/test.c"
```

Files the app writes into Documents (for example screenshots from a script) can be fetched with `xcrun devicectl device copy from` and the same `--domain-type`/`--domain-identifier` arguments.

## Screenshots

**From a script (Mac or iPad).** `script_screenshot(path)` saves the app's next frame as a PNG. This is how tests capture specific moments, e.g. mid-gesture:

```c
if (frame == 60) {
	script_screenshot("/tmp/orbit_moving.png");
}
```

On the iPad, write to the app's Documents folder and fetch the file with `devicectl device copy from`.

**Of the iPad screen as it is now.** This uses [pymobiledevice3](https://github.com/doronz88/pymobiledevice3), installed in a virtualenv:

```sh
python3 -m venv ~/.venvs/pmd
~/.venvs/pmd/bin/pip install pymobiledevice3

~/.venvs/pmd/bin/pymobiledevice3 developer dvt screenshot --udid <hardware udid> screen.png
```

- Always pass `--udid`. Without it, it may take a screenshot of another connected device (e.g. an iPhone).
- On iOS 17+ it sets up a userspace tunnel by itself the first time (it logs "Trying again over a no-root userspace tunnel"). No `sudo` or `tunneld` is needed.
- iPad screenshots are large. Shrink them before viewing or sharing with `sips -Z 1400 screen.png --out screen_small.png`.

Crash logs from the iPad can be listed with `pymobiledevice3 crash ls --udid <hardware udid>`.
