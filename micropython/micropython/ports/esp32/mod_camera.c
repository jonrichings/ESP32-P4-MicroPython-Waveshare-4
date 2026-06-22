/*
 * MicroPython ESP32-P4 MIPI CSI Camera Module
 */

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "soc/soc.h"
#include "soc/isp_reg.h"

#include <string.h>





// Linker symbols for registered camera sensor detection functions
extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_start;
extern esp_cam_sensor_detect_fn_t __esp_cam_sensor_detect_fn_array_end;

// Handles (Singletons)
static i2c_master_bus_handle_t camera_i2c_bus = NULL;
static esp_cam_sensor_device_t *camera_sensor = NULL;
static esp_cam_ctlr_handle_t camera_ctlr = NULL;
static isp_proc_handle_t camera_isp = NULL;
static bool camera_init_done = false;

static void sync_cache_m2c(void *addr, size_t size) {
    uint32_t start = (uint32_t)addr;
    uint32_t end = start + size;
    uint32_t aligned_start = start & ~63;
    uint32_t aligned_end = (end + 63) & ~63;
    uint32_t aligned_size = aligned_end - aligned_start;
    esp_cache_msync((void *)aligned_start, aligned_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

// Transaction callback synchronization structure
static esp_cam_ctlr_trans_t active_trans = {0};

static bool camera_on_get_new_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data) {
    esp_cam_ctlr_trans_t *active = (esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = active->buffer;
    trans->buflen = active->buflen;
    return false;
}

static bool camera_on_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data) {
    return false;
}

static mp_obj_t camera_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_sda, ARG_scl, ARG_hres, ARG_vres, ARG_hmirror, ARG_vflip, ARG_exposure };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sda, MP_ARG_INT, {.u_int = 7} },
        { MP_QSTR_scl, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_hres, MP_ARG_INT, {.u_int = 800} },
        { MP_QSTR_vres, MP_ARG_INT, {.u_int = 640} },
        { MP_QSTR_hmirror, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_vflip, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_exposure, MP_ARG_INT, {.u_int = -1} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int sda_pin = args[ARG_sda].u_int;
    int scl_pin = args[ARG_scl].u_int;
    int hres = args[ARG_hres].u_int;
    int vres = args[ARG_vres].u_int;
    int hmirror = args[ARG_hmirror].u_int;
    int vflip = args[ARG_vflip].u_int;
    int exposure = args[ARG_exposure].u_int;


    if (camera_init_done) {
        mp_printf(&mp_plat_print, "mod_camera: Camera already initialized. Reusing handles.\n");
        return mp_const_none;
    }

    esp_err_t ret;

    // 1. Initialize I2C SCCB Bus (Using I2C_NUM_0 to avoid display touch conflicts on I2C_NUM_1)
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = (gpio_num_t)sda_pin,
        .scl_io_num = (gpio_num_t)scl_pin,
        .i2c_port = I2C_NUM_0,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_bus_conf, &camera_i2c_bus);
    if (ret != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create I2C bus: esp_err=0x%x"), ret);
    }

    // 2. Probing and Initializing Camera Sensor via SCCB
    esp_cam_sensor_config_t cam_config = {
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
    };

    camera_sensor = NULL;
    // Walk the registered sensors in the linker array
    for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start; p < &__esp_cam_sensor_detect_fn_array_end; ++p) {
        sccb_i2c_config_t i2c_config = {
            .scl_speed_hz = 100000,
            .device_address = p->sccb_addr,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        ret = sccb_new_i2c_io(camera_i2c_bus, &i2c_config, &cam_config.sccb_handle);
        if (ret != ESP_OK) {
            continue;
        }

        cam_config.sensor_port = p->port;

        camera_sensor = (*(p->detect))(&cam_config);
        if (camera_sensor) {
            mp_printf(&mp_plat_print, "mod_camera: Detected sensor port=%d, SCCB addr=0x%x\n", p->port, p->sccb_addr);
            break;
        }
        esp_sccb_del_i2c_io(cam_config.sccb_handle);
    }

    if (!camera_sensor) {
        i2c_del_master_bus(camera_i2c_bus);
        camera_i2c_bus = NULL;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to detect or initialize camera sensor"));
    }

    // 3. Query and configure camera resolution format
    esp_cam_sensor_format_array_t cam_fmt_array = {0};
    esp_cam_sensor_query_format(camera_sensor, &cam_fmt_array);

    mp_printf(&mp_plat_print, "mod_camera: Supported formats:\n");
    for (int i = 0; i < cam_fmt_array.count; i++) {
        mp_printf(&mp_plat_print, "  - %s\n", cam_fmt_array.format_array[i].name);
    }

    char format_name[64];
    int fps = (hres == 1024 && vres == 600) ? 30 : 50;
    snprintf(format_name, sizeof(format_name), "MIPI_2lane_24Minput_RAW8_%dx%d_%dfps", hres, vres, fps);

    esp_cam_sensor_format_t *cam_cur_fmt = NULL;
    for (int i = 0; i < cam_fmt_array.count; i++) {
        if (strcmp(cam_fmt_array.format_array[i].name, format_name) == 0) {
            cam_cur_fmt = (esp_cam_sensor_format_t *)&(cam_fmt_array.format_array[i]);
            break;
        }
    }

    if (!cam_cur_fmt) {
        // Fall back to first supported format if requested one is not found
        if (cam_fmt_array.count > 0) {
            cam_cur_fmt = (esp_cam_sensor_format_t *)&(cam_fmt_array.format_array[0]);
            mp_printf(&mp_plat_print, "mod_camera: Requested format %s not found. Falling back to %s\n", format_name, cam_cur_fmt->name);
        } else {
            esp_cam_sensor_del_dev(camera_sensor);
            camera_sensor = NULL;
            i2c_del_master_bus(camera_i2c_bus);
            camera_i2c_bus = NULL;
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor returned zero supported formats"));
        }
    }

    mp_printf(&mp_plat_print, "mod_camera: Setting format...\n");
    ret = esp_cam_sensor_set_format(camera_sensor, cam_cur_fmt);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "mod_camera: Failed to set sensor format: esp_err=0x%x\n", ret);
    }
    mp_printf(&mp_plat_print, "mod_camera: Format set. hmirror=%d, vflip=%d, exposure=%d\n", hmirror, vflip, exposure);

    // Apply camera mirror, flip, and exposure settings to sensor
    const char *sensor_name = esp_cam_sensor_get_name(camera_sensor);
    if (sensor_name && (strcmp(sensor_name, "ov5647") == 0 || strcmp(sensor_name, "OV5647") == 0)) {
        mp_printf(&mp_plat_print, "mod_camera: Applying OV5647 specific register overrides...\n");
        uint8_t val = 0;
        esp_err_t err;

        // Horizontal Mirror (0x3821) - Set both Sensor & ISP Mirror (bits 1 and 2, mask 0x06)
        err = esp_sccb_transmit_receive_reg_a16v8(camera_sensor->sccb_handle, 0x3821, &val);
        if (err == ESP_OK) {
            if (hmirror != 0) {
                val = (val & ~0x06) | 0x06;
            } else {
                val = (val & ~0x06);
            }
            esp_sccb_transmit_reg_a16v8(camera_sensor->sccb_handle, 0x3821, val);
            mp_printf(&mp_plat_print, "mod_camera: OV5647 mirror register 0x3821 set to 0x%02x\n", val);
        }

        // Vertical Flip (0x3820) - Set both Sensor & ISP Flip (bits 1 and 2, mask 0x06)
        err = esp_sccb_transmit_receive_reg_a16v8(camera_sensor->sccb_handle, 0x3820, &val);
        if (err == ESP_OK) {
            if (vflip != 0) {
                val = (val & ~0x06) | 0x06;
            } else {
                val = (val & ~0x06);
            }
            esp_sccb_transmit_reg_a16v8(camera_sensor->sccb_handle, 0x3820, val);
            mp_printf(&mp_plat_print, "mod_camera: OV5647 flip register 0x3820 set to 0x%02x\n", val);
        }
    } else {
        // Fall back to standard set_para_value for other sensors
        if (hmirror != 0) {
            ret = esp_cam_sensor_set_para_value(camera_sensor, ESP_CAM_SENSOR_HMIRROR, &hmirror, sizeof(hmirror));
            if (ret != ESP_OK) {
                mp_printf(&mp_plat_print, "mod_camera: Failed to set hmirror: esp_err=0x%x\n", ret);
            }
        }
        if (vflip != 0) {
            ret = esp_cam_sensor_set_para_value(camera_sensor, ESP_CAM_SENSOR_VFLIP, &vflip, sizeof(vflip));
            if (ret != ESP_OK) {
                mp_printf(&mp_plat_print, "mod_camera: Failed to set vflip: esp_err=0x%x\n", ret);
            }
        }
    }

    if (exposure != -1) {
        ret = esp_cam_sensor_set_para_value(camera_sensor, ESP_CAM_SENSOR_EXPOSURE_VAL, &exposure, sizeof(exposure));
        if (ret != ESP_OK) {
            mp_printf(&mp_plat_print, "mod_camera: Failed to set exposure target: esp_err=0x%x\n", ret);
        }
    }




    // 4. Initialize MIPI CSI Receiver controller
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = cam_cur_fmt->width,
        .v_res = cam_cur_fmt->height,
        .lane_bit_rate_mbps = 200,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = 2,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    ret = esp_cam_new_csi_ctlr(&csi_config, &camera_ctlr);
    if (ret != ESP_OK) {
        esp_cam_sensor_del_dev(camera_sensor);
        camera_sensor = NULL;
        i2c_del_master_bus(camera_i2c_bus);
        camera_i2c_bus = NULL;
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create CSI controller: esp_err=0x%x"), ret);
    }

    // 5. Register CSI transaction callbacks
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = NULL,
        .on_trans_finished = camera_on_trans_finished,
    };
    ret = esp_cam_ctlr_register_event_callbacks(camera_ctlr, &cbs, &active_trans);
    if (ret != ESP_OK) {
        esp_cam_ctlr_del(camera_ctlr);
        camera_ctlr = NULL;
        esp_cam_sensor_del_dev(camera_sensor);
        camera_sensor = NULL;
        i2c_del_master_bus(camera_i2c_bus);
        camera_i2c_bus = NULL;
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to register callbacks: esp_err=0x%x"), ret);
    }

    ret = esp_cam_ctlr_enable(camera_ctlr);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "mod_camera: Failed to enable camera controller: esp_err=0x%x\n", ret);
    }

    // 6. Initialize and enable the hardware ISP processor
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = 80 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = cam_cur_fmt->width,
        .v_res = cam_cur_fmt->height,
    };
    ret = esp_isp_new_processor(&isp_config, &camera_isp);
    if (ret != ESP_OK) {
        esp_cam_ctlr_disable(camera_ctlr);
        esp_cam_ctlr_del(camera_ctlr);
        camera_ctlr = NULL;
        esp_cam_sensor_del_dev(camera_sensor);
        camera_sensor = NULL;
        i2c_del_master_bus(camera_i2c_bus);
        camera_i2c_bus = NULL;
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create ISP processor: esp_err=0x%x"), ret);
    }

    ret = esp_isp_enable(camera_isp);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "mod_camera: Failed to enable ISP: esp_err=0x%x\n", ret);
    }

    // Set ISP Bayer mode register to match the sensor's current bayer pattern (taking mirror/flip into account)
    if (cam_cur_fmt->isp_info) {
        esp_cam_sensor_bayer_pattern_t bayer_type = cam_cur_fmt->isp_info->isp_v1_info.bayer_type;
        const char *sensor_name = esp_cam_sensor_get_name(camera_sensor);
        if (sensor_name && (strcmp(sensor_name, "ov5647") == 0 || strcmp(sensor_name, "OV5647") == 0)) {
            bayer_type = 3; // OV5647 default bayer pattern is BGGR (3) for hmirror=0, vflip=0
        }
        // Mapping of bayer pattern to hardware register bayer_mode:
        // BGGR (3) -> 0 (BG/GR), GBRG (2) -> 1 (GB/RG), GRBG (1) -> 2 (GR/BG), RGGB (0) -> 3 (RG/GB)
        int final_bayer_type = bayer_type ^ (vflip << 1) ^ hmirror;
        uint32_t bayer_mode = 3 - final_bayer_type;

        mp_printf(&mp_plat_print, "mod_camera: Writing ISP bayer_mode = %u...\n", (unsigned int)bayer_mode);
        REG_SET_FIELD(ISP_FRAME_CFG_REG, ISP_BAYER_MODE, bayer_mode);

        // Commit shadow registers to active registers by setting ISP_CAM_UPDATE_REG in ISP_CAM_CNTL_REG
        REG_SET_BIT(ISP_CAM_CNTL_REG, ISP_CAM_UPDATE_REG);

        mp_printf(&mp_plat_print, "mod_camera: ISP bayer_mode configured (sensor bayer: %d, flip: %d, mirror: %d)\n",
                  (int)bayer_type, vflip, hmirror);
    }




    // 7. Start Camera Sensor output stream
    int enable_flag = 1;
    ret = esp_cam_sensor_ioctl(camera_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &enable_flag);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "mod_camera: Failed to start sensor streaming: esp_err=0x%x\n", ret);
    }

    // 8. Start CSI controller frame acquisition
    ret = esp_cam_ctlr_start(camera_ctlr);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "mod_camera: Failed to start CSI controller: esp_err=0x%x\n", ret);
    }

    camera_init_done = true;
    mp_printf(&mp_plat_print, "mod_camera: Camera pipeline initialized successfully (RAW8 -> CSI -> ISP -> RGB565).\n");

    mp_obj_t tuple[2] = {
        mp_obj_new_int(cam_cur_fmt->width),
        mp_obj_new_int(cam_cur_fmt->height)
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_init_obj, 0, camera_init);

static mp_obj_t camera_deinit(void) {
    if (camera_init_done) {
        mp_printf(&mp_plat_print, "mod_camera: Deinitializing camera pipeline...\n");

        if (camera_ctlr) {
            esp_cam_ctlr_stop(camera_ctlr);
            esp_cam_ctlr_disable(camera_ctlr);
            esp_cam_ctlr_del(camera_ctlr);
            camera_ctlr = NULL;
        }

        if (camera_isp) {
            esp_isp_disable(camera_isp);
            esp_isp_del_processor(camera_isp);
            camera_isp = NULL;
        }

        if (camera_sensor) {
            // Turn off camera streaming first
            int enable_flag = 0;
            esp_cam_sensor_ioctl(camera_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &enable_flag);
            esp_cam_sensor_del_dev(camera_sensor);
            camera_sensor = NULL;
        }

        if (camera_i2c_bus) {
            i2c_del_master_bus(camera_i2c_bus);
            camera_i2c_bus = NULL;
        }

        camera_init_done = false;
        mp_printf(&mp_plat_print, "mod_camera: Deinit completed.\n");
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_deinit_obj, camera_deinit);

static mp_obj_t camera_capture(size_t n_args, const mp_obj_t *args) {
    if (!camera_init_done || !camera_ctlr) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("camera is not initialized"));
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);

    uint32_t timeout_ms = 1000;
    if (n_args > 1) {
        timeout_ms = mp_obj_get_int(args[1]);
    }

    // Set the buffer coordinates in active transaction
    active_trans.buffer = bufinfo.buf;
    active_trans.buflen = bufinfo.len;
    active_trans.received_size = bufinfo.len;

    // Trigger D-cache invalidate to ensure DMA writes are visible to CPU
    sync_cache_m2c(bufinfo.buf, bufinfo.len);

    esp_err_t ret = esp_cam_ctlr_receive(camera_ctlr, &active_trans, timeout_ms);
    if (ret != ESP_OK) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("camera receive failed: esp_err=0x%x"), ret);
    }

    // Trigger D-cache invalidate again to make sure CPU reads correct data from memory
    sync_cache_m2c(bufinfo.buf, bufinfo.len);

    return mp_obj_new_int(active_trans.received_size);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(camera_capture_obj, 1, 2, camera_capture);

static const mp_rom_map_elem_t camera_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&camera_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&camera_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_capture), MP_ROM_PTR(&camera_capture_obj) },
};
static MP_DEFINE_CONST_DICT(camera_module_globals, camera_module_globals_table);

const mp_obj_module_t mp_module_camera = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&camera_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_camera, mp_module_camera);
