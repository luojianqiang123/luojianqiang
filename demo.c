#include "common.h"
#include "factory_testing.h"
#include "isp.h"
#include "media_player/ffmpeg/media_player_ffmpeg.h"
#include "mqtt_handle.h"
#include "parking_guide_register.h"
#include "poweroff_timer.h"
#include "rt_task.h"
#include "timelapse.h"
#include "video_play.h"
#include <signal.h>
#include "fm.h"
/**
 * 初始化
 */
#define KEY_DOWN_POWER_OFF_TIME 3000
//static uint32_t keypower_by_int_time_id = 0;//按键开机处理定时器
extern int parking_guide_flag;
ret_t on_window_manager_event(void* ctx, event_t* e)			//------------//---------------------按键中断状态切换函数---------//
{
    widget_t* win = window_manager_get_top_main_window(window_manager());	//---流媒体获取函数
    static uint32_t key_down_power_off_time_id = 0;        //记录按键按下的次数
    switch (e->type) {		//????按键按下
    case EVT_POINTER_UP:		//按键up
        power_off_timer_reset();   	//-----重置按键开机处理定时器的时间
        break;
    case EVT_KEY_DOWN: {		//按键按下
        key_event_t* key_event = (key_event_t*)e;
        log_debug("key_event->key = %d ，TK_KEY_ESCAPE =%d", key_event->key, TK_KEY_ESCAPE);      //输出当前key和TK-key

        if (key_event->key == TK_KEY_ESCAPE) {
            if (key_down_power_off_time_id == 0)        	//判断按下的次数
                key_down_power_off_time_id = timer_add(power_off_handle_on_time_ex, NULL, KEY_DOWN_POWER_OFF_TIME);
        } else if (key_event->key == AUX_INSERTED) {
            log_debug("AUX_INSERTED EVENT\n");		//输出AUX_INSERTED的值
            audio_aux_gpio_control(TRUE);
        } else if (key_event->key == AUX_UNPLUGGED) {	
            log_debug("AUX_UNPLUGGED EVENT\n");	//输出AUX_UNPLUGGED的值
            audio_aux_gpio_control(FALSE);		//错误		
        }

    } break;
    case EVT_KEY_UP_BEFORE_CHILDREN: {		//????
        key_event_t* key_event = (key_event_t*)e;
        log_debug("key_event->key = %d ，TK_KEY_ESCAPE =%d", key_event->key, TK_KEY_ESCAPE);
        return_value_if_fail(key_event->key == TK_KEY_ESCAPE, RET_OK);

        if (!tk_str_eq(win->name, "screen_saver_page")) {
            on_screen_saver(NULL);
        }

        if (key_down_power_off_time_id != 0) {
            timer_remove(key_down_power_off_time_id);
            key_down_power_off_time_id = 0;
        }

    } break;
    case EVT_KEY_LONG_PRESS:		//按键长按
        break;
    default:
        break;
    }

    return RET_OK;
}
static ret_t on_sd_card_popup_state_alert_time(const timer_info_t* info)
{
    sd_card_popup_state_alert();		//SD卡状态报警
    return RET_REMOVE;
}
static ret_t on_rear_sensor_rfh_event(const timer_info_t* info)	//追尾传感器事件
{
    if (!get_rfh_switch_status()) {	//获取情况
        stop_screen_saver();		//停止屏幕保护
        screen_set_lcd_brightness_level_255(screen_get_brightness_level(), FALSE);	//LCD屏幕闪烁控制255
        extern ret_t parking_guide_page_init(void);					//停车场定位初始化
        parking_guide_page_init();
        parking_guide_flag = 1;
    }
    return RET_REMOVE;
}
static ret_t on_get_sensor_gain(const timer_info_t* info)		//-------根据光照传感器调节屏幕亮度-------
{
    widget_t* win = window_manager_get_top_main_window(window_manager());	//获取流媒体
    if(app_config_get_int(APP_CONFIG_AUTO_BRIGHTNESS_ENABLE) && (!tk_str_eq(win->name, "screen_saver_page"))) {
        uint32_t senser_vaule_cam0 = isp_get_senser_gain(0, 0%4);
        uint32_t senser_vaule_cam1 = isp_get_senser_gain(3, 12%4);
        // log_debug("senser_vaule_cam0: %d\n",senser_vaule_cam0);
        static uint32_t bright_level = 0;
        static uint32_t bright_level_old = 0;
        static uint8_t cnt = 0;
        if(senser_vaule_cam0 < (1 * 1024 + 512)) {  //环境明亮
            bright_level = 120;
        } else if(senser_vaule_cam0 < (2 * 1024)) {
            bright_level = 100;
        } else if(senser_vaule_cam0 < (3 * 1024)) {
            bright_level = 70;
        } else if(senser_vaule_cam0 < (1041 * 1024)) {
            bright_level = 30;  //环境昏暗
        }

        if (bright_level_old!= bright_level) {  // 亮度变化
            bright_level_old = bright_level;
            cnt = 0;
        } else {    //亮度稳定 5s
            if(++cnt > 4) {
                cnt = 0;
                if(bright_level_old != screen_get_brightness_level())
                    screen_set_lcd_brightness_level_255(bright_level_old,TRUE);
            }
        }
    }
    return RET_REPEAT;
}
static ret_t unload_all_image_time(void* ctx, event_t* e)		//卸载所有图像
{
    // log_debug("\n");
    image_manager_unload_all(image_manager());
    return RET_OK;
}
//关机提示
static ret_t on_poweroff_tip_on_time(const timer_info_t* info)
{
    static int i = 5;
    char tmp_buf[64] = { 0 };
    sprintf(tmp_buf, "%s %ds", TIP_TEXT_TIME_POWER_OFF_AFTER, i);
    if (i != 0)
    {
        common_page_creating_prompt_window_ex(BG_COLOR_GREY, 0xFFFFFFFF, tmp_buf, 1100);
        i--;
    }
    else
    {
        power_off_handle(1000);
        return RET_REMOVE;
    }

    return RET_REPEAT;
}
//定时开机处理
static ret_t on_poweroff_timer_on_time(const timer_info_t* info)
{
    timer_add(on_poweroff_tip_on_time, NULL, 1000);
    return RET_REMOVE;
}
/*static ret_t on_check_power_by_int_on_time(const timer_info_t* info)
{
    if (!power_is_acc_on() && !gsensor_is_power_on_by_int() && ui_config_list.power_supply_mode) {//降压线
        log_debug("acc disconnet disconnect! gsensor no collision!");
        if (keypower_is_power_on_by_int()){
            keypower_by_int_time_id = timer_add(on_poweroff_timer_on_time, NULL, 60000); //按键开机60秒无操作关机
        }else
            power_off_handle(1);
    } else if (!power_is_acc_on() && gsensor_is_power_on_by_int()){
        if (!sdcard_is_mounted())//gsenser碰撞开机sd卡没有挂载
            timer_add(on_poweroff_tip_on_time, NULL, 1000);
    }
    return RET_REMOVE;
}*/

static ret_t on_gsensor_power_by_int_on_time(const timer_info_t* info)
{
    power_off_handle(1);
    return RET_REMOVE;
}
static ret_t on_check_power_by_in_on_time(const timer_info_t* info)
{
    log_debug("ui_config_list.power_supply_mode:[%d]\n", ui_config_list.power_supply_mode);
    if (gsensor_is_power_on_by_int()){//碰撞开机
        if (!sdcard_is_mounted() || ipcsync_get_video_recording_status(ipcsync_read_record_info()) == eNone) {
            if(!sdcard_is_mounted()){
                sd_status_inter_hald();
            }
            if (ui_config_list.power_supply_mode == 1){//1. 降压线模式
                //timer_add(on_poweroff_timer_on_time, NULL, 10);
                strat_power_off_time(10);
            }else{//车充模式
                if (!power_is_acc_on() && !power_is_dc_on()){//没有acc dc 即电池开机
                    //timer_add(on_poweroff_timer_on_time, NULL, 10);
                    strat_power_off_time(10);
                    log_debug("[%s][%d]\n", __func__, __LINE__);
                }
            }
        } else {
            if (ui_config_list.power_supply_mode == 1) { // 1. 降压线模式
                camera_set_record_lock_or_unkock(LOCK);
                timer_add(on_gsensor_power_by_int_on_time, NULL, 17000);
                log_debug("[%s][%d]\n", __func__, __LINE__);
            } else { //车充模式
                if (!power_is_acc_on() && !power_is_dc_on()) { //没有acc dc 即电池开机
                    camera_set_record_lock_or_unkock(LOCK);
                    timer_add(on_gsensor_power_by_int_on_time, NULL, 17000);
                    log_debug("[%s][%d]\n", __func__, __LINE__);
                } else {
                    camera_set_record_lock_or_unkock(LOCK);
                }
            }
        }
    } else if (keypower_is_power_on_by_int() && !power_is_acc_on()) {
        if (ui_config_list.power_supply_mode == 1)
            strat_power_off_time(60000); //按键开机60秒无操作关机
    }
    return RET_REMOVE;
}
static ret_t power_supply_mode_auto_switch(void)
{
    if (!gsensor_is_power_on_by_int() && !keypower_is_power_on_by_int()) {//碰撞开机，按键开机不能做切换
        if (!power_is_acc_on() && !is_timelaps()){//是否接上acc 缩时录影中
            if (ui_config_list.power_supply_mode == 1){
                set_power_supply_mode(0);
            }
        } else if (power_is_acc_on() && ui_config_list.power_supply_mode == 0) {
            set_power_supply_mode(1);
        }
    }
    log_debug("ui_config_list.power_supply_mode:[%d]\n", ui_config_list.power_supply_mode);
    return RET_OK;
}
static ret_t on_power_supply_mode_auto_switch_time(const timer_info_t* info)
{
    power_supply_mode_auto_switch();
    return RET_REMOVE;
}
#if 1
static ret_t on_signal_int_time(const timer_info_t* info){
    tk_quit();
    return RET_REMOVE;
}

static ret_t on_signal_int_idle(const idle_info_t* info)
{
    static int i = 0;
    if (ipcsync_get_video_recording_status(ipcsync_read_record_info()) != eNone && i < 120) {
        i++;
        return RET_REPEAT;
    }
    timer_add(on_signal_int_time, NULL, 0);
    return RET_REMOVE;
}
#endif
static void on_sigterm_signal_int(int sig){
    idle_add(on_signal_int_idle, NULL);
    return;
}

static ret_t on_watdog_keepalive(const timer_info_t* info)
{
    system_watchdog_keepalive();
    return RET_REPEAT;
}

ret_t application_init(void)
{
    runtime_init();
    if(fm_is_need_open())
        fm_open();
    gpio_export(AUDIO_SPK_AUX_INT_DET_GPIO);
    audio_aux_gpio_control(!gpio_get_val(AUDIO_SPK_AUX_INT_DET_GPIO));
    gpio_unexport(AUDIO_SPK_AUX_INT_DET_GPIO);
    app_ui_config_load();

    video_play_register();
    parking_guide_register();
    media_player_set(media_player_ffmpeg_create());
    screen_set_lcd_brightness_level_255(screen_get_brightness_level(), TRUE);

    edog_play_register();
    edog_manager_init();

    media_player_init(TRUE);
    audio_play_init();
    // audio_play_beep_on();
    audio_play_beep_idle_load(FALSE);
    set_sound_volume(ui_config_list.audio_play_volume, FALSE);
    audio_play_play_file(AUDIO_POWER_ON);

    common_page_change_locale(ui_config_list.language_sta);
    //fifo_set_osd_stamp_status(1);
    extern int32_t half_voswitch_flag;
    extern ret_t home_setting_page_init(void);
    camera_set_preview_mode(CAMERA_PREVIEW_MODE_LYLINK_HALF_CAM0_HALF);
    half_voswitch_flag = 2;

    if (home_setting_page_init()!= RET_OK) {
        return RET_FAIL;
    }
    log_debug("[%s][%d]\n", __func__, __LINE__);


    widget_on(window_manager(), EVT_POINTER_UP, on_window_manager_event, NULL);
    screen_saver_init();
    window_start_screen_saver(ui_config_list.screen_saver_time * 30000);

    //ui启动1秒后才去查询sd卡
    timer_add(on_sd_card_popup_state_alert_time, NULL, 3000);
    //timer_add(on_rear_sensor_rfh_event, NULL, 4000);
    timer_add(on_check_power_by_in_on_time, NULL, 5000);
    timer_add(on_power_supply_mode_auto_switch_time, NULL,60000);
    timer_add(on_get_sensor_gain, NULL,1000);
    timer_add(on_watdog_keepalive, NULL, 3000);
    widget_on(window_manager(), EVT_KEY_DOWN, on_window_manager_event, NULL);
    widget_on(window_manager(), EVT_KEY_UP_BEFORE_CHILDREN, on_window_manager_event, NULL);
    widget_on(window_manager(), EVT_KEY_LONG_PRESS, on_window_manager_event, NULL);
    signal(SIGTERM, on_sigterm_signal_int);

    factory_testing_register();

    widget_on(window_manager(), EVT_WINDOW_CLOSE, unload_all_image_time, NULL);
    sync();

    return RET_OK;
}

/**
 * 退出
 */
ret_t application_exit(void)
{
    log_debug("application_exit\n");
    // runtime_deinit();
    // IPC_CarInfo_Close();
    shell_executor("poweroff -f");
    return RET_OK;
}
