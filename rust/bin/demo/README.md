# Cargo APK Build

```bash
export NDK_HOME="path/to/ndk"
export ANDROID_HOME="path/to/sdk"

rustup target add aarch64-linux-android
cargo install cargo-apk

cargo apk run
```
