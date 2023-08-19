extern crate strayphotons;

fn main() {
    #[cfg(not(any(target_os = "android", target_os = "ios")))]
    {
        use strayphotons::StrayPhotons;
        StrayPhotons::new().start_app();
    }
}
