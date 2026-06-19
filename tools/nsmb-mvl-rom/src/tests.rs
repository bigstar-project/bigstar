use super::{
    big_star_selector, build_direct_loadlevel_stub, encode_ldr_imm, encode_load_imm,
    encode_mov_imm, encode_str_imm, encode_strb_imm, initial_lives, life_mode_selector,
    stage_scene_settings, with_cond, DirectMvlConfig, GAME_PLAYER_INVENTORY_POWERUP_ADDR,
    MVL_NATIVE_COURSE_SELECTOR_ADDR, MVL_RUNTIME_CONFIG_STAGE_OFFSET, PLAYER_POWERUP_MEGA,
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
fn mega_powerup_exception_uses_the_native_mega_enum() {
    assert_eq!(
        PLAYER_POWERUP_MEGA, 3,
        "PowerupState 3 is Mega; 4 is Mini and 5 is Shell"
    );
}

#[test]
fn direct_loadlevel_uses_network_random_seed() {
    let config = DirectMvlConfig {
        stage: 2,
        player_id: 0,
        scene_settings: 0x00b6_ff00,
        initial_lives: 3,
        life_mode_selector: 0,
        big_star_selector: 1,
    };
    let stub = build_direct_loadlevel_stub(0x0215_0000, 0x0200_0000, 0x0210_0000, &config)
        .expect("build direct MvL stub");
    let load_network_rng_seed = encode_load_imm(12, 0xffff_ffff).expect("encode rng seed");
    let store_rng_seed = encode_str_imm(12, 13, 0x30).expect("encode rng seed store");

    assert!(
        stub.windows(2)
            .any(|pair| pair == [load_network_rng_seed, store_rng_seed]),
        "loadLevel rngSeed stack argument must be 0xffffffff so match-seeded Net/Game RNG is used"
    );
}

#[test]
fn direct_loadlevel_matches_normal_mvl_control_args() {
    let config = DirectMvlConfig {
        stage: 2,
        player_id: 0,
        scene_settings: 0x00b6_ff00,
        initial_lives: 3,
        life_mode_selector: 0,
        big_star_selector: 1,
    };
    let stub = build_direct_loadlevel_stub(0x0215_0000, 0x0200_0000, 0x0210_0000, &config)
        .expect("build direct MvL stub");

    let store_control_flag = encode_str_imm(12, 13, 0x20).expect("encode control flag store");
    let load_control_options = encode_load_imm(12, 0xff).expect("encode control options");
    let store_control_options = encode_str_imm(12, 13, 0x24).expect("encode control options store");

    assert!(
        stub.contains(&store_control_flag),
        "loadLevel stack arg 0x20 must match the normal MvL load path"
    );
    assert!(
        stub.windows(2)
            .any(|pair| pair == [load_control_options, store_control_options]),
        "loadLevel stack arg 0x24 must match the normal MvL load path"
    );
}

#[test]
fn direct_loadlevel_updates_native_course_selector() {
    let config = DirectMvlConfig {
        stage: 4,
        player_id: 0,
        scene_settings: 0x00b8_ff00,
        initial_lives: 3,
        life_mode_selector: 2,
        big_star_selector: 1,
    };
    let stub = build_direct_loadlevel_stub(0x0215_0000, 0x0200_0000, 0x0210_0000, &config)
        .expect("build direct MvL stub");

    let load_fallback_stage = encode_mov_imm(1, config.stage as u32).expect("encode stage load");
    let load_runtime_stage = with_cond(
        encode_ldr_imm(1, 4, MVL_RUNTIME_CONFIG_STAGE_OFFSET).expect("encode runtime stage load"),
        0,
    );
    let store_course_selector = encode_strb_imm(1, 0, 0).expect("encode course selector store");

    assert!(
        stub.contains(&MVL_NATIVE_COURSE_SELECTOR_ADDR),
        "stub must reference the native MvL course selector used by 8-coin item filtering"
    );
    assert!(
        stub.windows(3).any(|window| window
            == [
                load_fallback_stage,
                load_runtime_stage,
                store_course_selector
            ]),
        "stub must store fallback/runtime stage into the native MvL course selector"
    );
}

#[test]
fn direct_loadlevel_clears_initial_inventory_powerups() {
    let config = DirectMvlConfig {
        stage: 0,
        player_id: 1,
        scene_settings: 0x00b4_ff00,
        initial_lives: 3,
        life_mode_selector: 0,
        big_star_selector: 1,
    };
    let stub = build_direct_loadlevel_stub(0x0215_0000, 0x0200_0000, 0x0210_0000, &config)
        .expect("build direct MvL stub");

    let clear_value = encode_mov_imm(1, 0).expect("encode inventory clear value");
    let clear_player0 = encode_strb_imm(1, 0, 0).expect("encode player0 inventory clear");
    let clear_player1 = encode_strb_imm(1, 0, 1).expect("encode player1 inventory clear");

    assert!(
        stub.contains(&GAME_PLAYER_INVENTORY_POWERUP_ADDR),
        "stub must reference Game::playerInventoryPowerup"
    );
    assert!(
        stub.windows(3)
            .any(|window| window == [clear_value, clear_player0, clear_player1]),
        "stub must clear Mario and Luigi initial stock items after direct MvL load"
    );
}
