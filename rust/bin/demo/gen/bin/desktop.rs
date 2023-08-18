extern crate strayphotons;
use strayphotons::StrayPhotons;

fn main() {
    #[cfg(not(any(target_os = "android", target_os = "ios")))]
    {
        StrayPhotons::new().start_app();
    }
}
