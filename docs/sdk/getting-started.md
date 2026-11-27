# Getting Started with FinnSDK

Step-by-step walkthrough for creating, validating, building, running, registering, and packaging a native FinnOS app with `finn-sdk`.

## 1. Check your environment

```sh
finn-sdk check
```

Confirms `FinnSDK` headers, `libfinnui`, `libfinnui_qml`, `libfinnplatformtheme.so`, `libfinnstyle.so`, `FinnSDKConfig.cmake`, `cmake`, and `ninja` are installed.

## 2. Scaffold a project

```sh
finn-sdk new hello --name "Hello Finn"
cd hello
```

Copies `sdk/template/` (`CMakeLists.txt`, `Makefile`, `finn-app.json`, QML/C++20 starter source, and `.desktop` file).

## 3. Validate the manifest

```sh
finn-sdk validate finn-app.json
```

Validates `finn-app.json` against `sdk/schema/finn-app.schema.json`.

## 4. Build and run

```sh
finn-sdk build .
finn-sdk run .
```

To preview responsive layout adaptation across form factors:

```sh
finn-sdk simulate --form-factor desktop .
finn-sdk simulate --form-factor tablet .
finn-sdk simulate --form-factor phone .
finn-sdk simulate --form-factor tv .
```

## 5. Register locally

```sh
finn-sdk register .
```

Writes the app manifest into `~/.local/share/finn/apps/`, where `finn-session` and `finn-shell` pick it up live without restarting the session.

## 6. Package & sign a `.finnapp` bundle

```sh
finn-sdk keygen                 # generates Ed25519 keypair outside the repo
finn-sdk package .              # creates deterministic signed hello.finnapp
finn-sdk verify hello.finnapp   # verifies signature and manifest
```

See [publishing.md](publishing.md) for the `.finnapp` format and [api-overview.md](api-overview.md) for the QML `FinnUI 1.0` and C++20 widget APIs.
