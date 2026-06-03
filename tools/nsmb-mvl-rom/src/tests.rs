use super::{
    big_star_selector, build_direct_loadlevel_stub, encode_load_imm, encode_str_imm, initial_lives,
    life_mode_selector, stage_scene_settings,
};

#[test]
fn stage_scene_settings_follow_mvl_course_ids() {
    assert_eq!(stage_scene_settings(0).unwrap(), 0x00b4_ff00);
    assert_eq!(stage_scene_settings(4).unwrap(), 0x00b8_ff00);
    assert!(stage_scene_settings(5).is_err());
}

#[test]
fn initial_lives_keep_rules_separate_from_stage_settings() {
    assert_eq!(initial_lives("3").unwrap(), 3);
    assert_eq!(initial_lives("5").unwrap(), 5);
    assert_eq!(initial_lives("endless").unwrap(), 3);
    assert_eq!(life_mode_selector("3").unwrap(), 0);
    assert_eq!(life_mode_selector("5").unwrap(), 0);
    assert_eq!(life_mode_selector("endless").unwrap(), 2);
}

#[test]
fn big_star_targets_use_the_native_selector_table() {
    assert_eq!(big_star_selector(3).unwrap(), 0);
    assert_eq!(big_star_selector(5).unwrap(), 1);
    assert_eq!(big_star_selector(10).unwrap(), 2);
}

#[test]
fn direct_loadlevel_uses_network_random_seed() {
    let stub = build_direct_loadlevel_stub(
        0x0215_0000,
        0x0200_0000,
        0x0210_0000,
        2,
        0,
        0x00b6_ff00,
        3,
        0,
        1,
    )
    .expect("build direct MvL stub");
    let load_network_rng_seed = encode_load_imm(12, 0xffff_ffff).expect("encode rng seed");
    let store_rng_seed = encode_str_imm(12, 13, 0x30).expect("encode rng seed store");

    assert!(
        stub.windows(2)
            .any(|pair| pair == [load_network_rng_seed, store_rng_seed]),
        "loadLevel rngSeed stack argument must be 0xffffffff so match-seeded Net/Game RNG is used"
    );
}
