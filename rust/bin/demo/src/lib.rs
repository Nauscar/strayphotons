extern crate strayphotons;
use strayphotons::StrayPhotons;

#[cfg(target_os = "android")]
use strayphotons::AndroidApp;

#[cfg(target_os = "android")]
#[no_mangle]
fn android_main(app: AndroidApp) {
    StrayPhotons::new(app).start_app();
}
