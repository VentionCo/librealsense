// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020 Intel Corporation. All Rights Reserved.

//#cmake: static!

#include "../func-common.h"
#include "presets-common.h"

using namespace rs2;

TEST_CASE( "presets sanity while streaming", "[l500][live]" )
{
    auto devices = find_devices_by_product_line_or_exit( RS2_PRODUCT_LINE_L500 );
    auto dev = devices[0];

    exit_if_fw_version_is_under( dev, MIN_GET_DEFAULT_FW_VERSION );

    auto depth_sens = dev.first< rs2::depth_sensor >();

    auto preset_to_expected_map = build_preset_to_expected_values_map( depth_sens );
    auto preset_to_expected_defaults_map = build_preset_to_expected_defaults_map( dev, depth_sens );

    reset_camera_preset( depth_sens );

    // set preset and mode before stream start
    check_presets_values_while_streaming(
        depth_sens,
        preset_to_expected_map,
        preset_to_expected_defaults_map,
        [&]( rs2_sensor_mode mode, rs2_l500_visual_preset preset ) {
            set_mode_preset( depth_sens, mode, preset );
        });

    // set preset and mode after stream start
    check_presets_values_while_streaming(
        depth_sens,
        preset_to_expected_map,
        preset_to_expected_defaults_map,
        []( rs2_sensor_mode mode, rs2_l500_visual_preset preset ) {},
        [&]( rs2_sensor_mode mode, rs2_l500_visual_preset preset ) {
            set_mode_preset( depth_sens, mode, preset );
        } );
}
