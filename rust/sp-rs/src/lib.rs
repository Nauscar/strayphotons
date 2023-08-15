/*
 * Stray Photons - Copyright (C) 2023 Jacob Wirth & Justin Li
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#[cfg(feature = "api")]
mod api;
#[cfg(feature = "api")]
pub use api::*;

#[cfg(feature = "wasm")]
mod wasm;

#[cfg(feature = "window")]
mod window;

#[cfg(target_os = "android")]
#[no_mangle]
fn android_main(_app: winit::platform::android::activity::AndroidApp) {}
