/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/util/attribute-storage.h>
#include <app_priv.h>
#include "driver/gpio.h"
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace esp_matter::cluster;
using namespace chip::app::Clusters;

uint16_t aggregator_endpoint_id = chip::kInvalidEndpointId;
uint16_t motor1_endpoint_id = chip::kInvalidEndpointId;
uint16_t motor2_endpoint_id = chip::kInvalidEndpointId;

#define MOTOR1_FORWARD_PIN GPIO_NUM_10 // 电机1正转继电器
#define MOTOR1_REVERSE_PIN GPIO_NUM_11 // 电机1反转继电器
#define MOTOR2_FORWARD_PIN GPIO_NUM_12 // 电机2正转继电器
#define MOTOR2_REVERSE_PIN GPIO_NUM_13 // 电机2反转继电器
#define FULL_MOVEMENT_TIME_MS 36000
// static int motor1_current_position = 0;
// static int motor2_current_position = 0;

// 定时器回调周期，单位毫秒
#define TIMER_INTERVAL_MS 100

static const char *TAG = "app_main";

// 位置单位为“十分之一百分比”（0～1000），例如 700 表示 70%
volatile int motor1_current_position = 0;
volatile int motor1_target_position = 0;
volatile int motor2_current_position = 0;
volatile int motor2_target_position = 0;
volatile uint16_t motor1_target_lift_percent_100ths = 0;
volatile uint16_t motor2_target_lift_percent_100ths = 0;
volatile bool motor1_move_in_last_interval = false;
volatile bool motor2_move_in_last_interval = false;
TimerHandle_t motor_timer = NULL;

/**
 * @brief 定时器回调函数
 *
 * 每 TIMER_INTERVAL_MS 毫秒调用一次，根据当前与目标位置的差异，
 * 控制继电器驱动电机向前或向后移动 1 个步进单位，直到到达目标位置。
 */
void motor_timer_callback(TimerHandle_t xTimer)
{
    if (motor1_target_position > 0 && motor1_current_position < 0) {
        motor1_current_position = 0;
    }
    if (motor2_target_position > 0 && motor2_current_position < 0) {
        motor2_current_position = 0;
    }

    if (motor1_target_position < FULL_MOVEMENT_TIME_MS && motor1_current_position > FULL_MOVEMENT_TIME_MS) {
        motor1_current_position = FULL_MOVEMENT_TIME_MS;
    }
    if (motor2_target_position < FULL_MOVEMENT_TIME_MS && motor2_current_position > FULL_MOVEMENT_TIME_MS) {
        motor2_current_position = FULL_MOVEMENT_TIME_MS;
    }

    // 电机1控制（假设 homekit 中 motor_id==2 对应 motor1）
    if (motor1_current_position < motor1_target_position) {
        // 需要正转（增加位置）
        gpio_set_level(MOTOR1_FORWARD_PIN, 1);
        gpio_set_level(MOTOR1_REVERSE_PIN, 0);
        motor1_current_position += TIMER_INTERVAL_MS;
        if (motor1_current_position >= motor1_target_position) {
            motor1_current_position = motor1_target_position;
            gpio_set_level(MOTOR1_FORWARD_PIN, 0);
            gpio_set_level(MOTOR1_REVERSE_PIN, 0);
        }
        motor1_move_in_last_interval = true;
    } else if (motor1_current_position > motor1_target_position) {
        // 需要反转（降低位置）
        gpio_set_level(MOTOR1_FORWARD_PIN, 0);
        gpio_set_level(MOTOR1_REVERSE_PIN, 1);
        motor1_current_position -= TIMER_INTERVAL_MS;
        if (motor1_current_position <= motor1_target_position) {
            motor1_current_position = motor1_target_position;
            gpio_set_level(MOTOR1_FORWARD_PIN, 0);
            gpio_set_level(MOTOR1_REVERSE_PIN, 0);
        }
        motor1_move_in_last_interval = true;
    } else {
        if (motor1_move_in_last_interval) {
            //     chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, target_lift_percent_100ths]() {
            //         esp_matter_attr_val_t x;
            //         x.val.u16 = target_lift_percent_100ths;
            //         x.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT16;
            //         attribute::update(endpoint_id, WindowCovering::Id,
            //         WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &x);
            //    });
            chip::DeviceLayer::SystemLayer().ScheduleLambda([motor1_target_lift_percent_100ths, motor1_endpoint_id]() {
                esp_matter_attr_val_t x;
                x.val.u16 = motor1_target_lift_percent_100ths;
                x.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT16;
                attribute::update(motor1_endpoint_id, WindowCovering::Id,
                                  WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &x);
            });
        }
        // 达到目标，确保继电器关闭
        gpio_set_level(MOTOR1_FORWARD_PIN, 0);
        gpio_set_level(MOTOR1_REVERSE_PIN, 0);
        motor1_move_in_last_interval = false;
    }

    // 电机2控制（假设 homekit 中 motor_id==3 对应 motor2）
    if (motor2_current_position < motor2_target_position) {
        gpio_set_level(MOTOR2_FORWARD_PIN, 1);
        gpio_set_level(MOTOR2_REVERSE_PIN, 0);
        motor2_current_position += TIMER_INTERVAL_MS;
        if (motor2_current_position >= motor2_target_position) {
            motor2_current_position = motor2_target_position;
            gpio_set_level(MOTOR2_FORWARD_PIN, 0);
            gpio_set_level(MOTOR2_REVERSE_PIN, 0);
        }
        motor2_move_in_last_interval = true;
    } else if (motor2_current_position > motor2_target_position) {
        gpio_set_level(MOTOR2_FORWARD_PIN, 0);
        gpio_set_level(MOTOR2_REVERSE_PIN, 1);
        motor2_current_position -= TIMER_INTERVAL_MS;
        if (motor2_current_position <= motor2_target_position) {
            motor2_current_position = motor2_target_position;
            gpio_set_level(MOTOR2_FORWARD_PIN, 0);
            gpio_set_level(MOTOR2_REVERSE_PIN, 0);
        }
        motor2_move_in_last_interval = true;
    } else {
        if (motor2_move_in_last_interval) {
            ESP_LOGI(TAG, "Update motor2 status");
            chip::DeviceLayer::SystemLayer().ScheduleLambda([motor2_target_lift_percent_100ths, motor2_endpoint_id]() {
                esp_matter_attr_val_t x;
                x.val.u16 = motor2_target_lift_percent_100ths;
                x.type = ESP_MATTER_VAL_TYPE_NULLABLE_UINT16;
                attribute::update(motor2_endpoint_id, WindowCovering::Id,
                                  WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &x);
            });
        }
        gpio_set_level(MOTOR2_FORWARD_PIN, 0);
        gpio_set_level(MOTOR2_REVERSE_PIN, 0);
        motor2_move_in_last_interval = false;
    }

    // ESP_LOGI(TAG, "Motor1: current %d, target %d; Motor2: current %d, target %d",
    //          motor1_current_position, motor1_target_position,
    //          motor2_current_position, motor2_target_position);
}

/**
 * @brief motor_move 仅用于提交目标位置（单位：百分比）
 *
 * homekit 调用时传入的 target_position 为 0~100，
 * 此函数将其转换为十分之一百分比单位，并更新对应电机的 target_position 变量，
 * 实际运动由定时器周期性更新。
 *
 * @param motor_id 电机编号（homekit 中例如为 2 或 3）
 * @param target_position 目标位置（百分比，0～100）
 */
void motor_move(int motor_id, int target_position)
{
    int target = FULL_MOVEMENT_TIME_MS * target_position / 100 / TIMER_INTERVAL_MS * TIMER_INTERVAL_MS;
    if (target == 0) {
        target = -10000;
    }
    if (target == FULL_MOVEMENT_TIME_MS) {
        target = FULL_MOVEMENT_TIME_MS + 10000;
    }
    if (motor_id == 1) {
        motor1_target_position = target;
        ESP_LOGI(TAG, "Motor %d target set to %d (tenth-percent)", motor_id, target);
    } else {
        motor2_target_position = target;
        ESP_LOGI(TAG, "Motor %d target set to %d (tenth-percent)", motor_id, target);
    }
}

/**
 * @brief 初始化继电器 GPIO
 */
void init_relays(void)
{
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    // 配置四个继电器引脚为输出
    io_conf.pin_bit_mask = (1ULL << MOTOR1_FORWARD_PIN) | (1ULL << MOTOR1_REVERSE_PIN) | (1ULL << MOTOR2_FORWARD_PIN) |
        (1ULL << MOTOR2_REVERSE_PIN);
    io_conf.pull_down_en = (gpio_pulldown_t)0;
    io_conf.pull_up_en = (gpio_pullup_t)0;
    gpio_config(&io_conf);

    // 初始化所有继电器为关闭（假设低电平为关闭）
    gpio_set_level(MOTOR1_FORWARD_PIN, 0);
    gpio_set_level(MOTOR1_REVERSE_PIN, 0);
    gpio_set_level(MOTOR2_FORWARD_PIN, 0);
    gpio_set_level(MOTOR2_REVERSE_PIN, 0);
}

// /**
//  * @brief Matter 属性回调函数
//  *
//  * 当 Matter 收到窗帘设备目标位置更新时，会调用此回调函数。
//  * context 参数用来区分是哪个窗帘设备（1 对应电机1，2 对应电机2）。
//  *
//  * @param context         上下文数据，此处传入电机编号（1 或 2）
//  * @param target_position 目标位置百分比
//  */
// void matter_window_covering_target_position_callback(void *context, int target_position)
// {
//     int motor_id = (int)context;
//     ESP_LOGI(TAG, "Matter 命令：电机 %d 目标位置更新为 %d%%", motor_id, target_position);
//     motor_move(motor_id, target_position);
// }

static uint16_t configured_buttons = 0;
static button_endpoint button_list[CONFIG_MAX_CONFIGURABLE_BUTTONS];

namespace {
// Please refer to https://github.com/CHIP-Specifications/connectedhomeip-spec/blob/master/src/namespaces
constexpr const uint8_t kNamespaceSwitches = 43;
// Common Number Namespace: 7, tag 0 (Zero)
constexpr const uint8_t kTagSwitchOn = 0;
// Common Number Namespace: 7, tag 1 (One)
constexpr const uint8_t kTagSwitchOff = 1;

constexpr const uint8_t kNamespacePosition = 8;
// Common Position Namespace: 8, tag: 0 (Left)
constexpr const uint8_t kTagPositionLeft = 0;
// Common Position Namespace: 8, tag: 1 (Right)
constexpr const uint8_t kTagPositionRight = 1;

const Descriptor::Structs::SemanticTagStruct::Type gEp1TagList[] = {
    {.namespaceID = kNamespaceSwitches, .tag = kTagSwitchOn},
    {.namespaceID = kNamespacePosition, .tag = kTagPositionRight}};
const Descriptor::Structs::SemanticTagStruct::Type gEp2TagList[] = {
    {.namespaceID = kNamespaceSwitches, .tag = kTagSwitchOff},
    {.namespaceID = kNamespacePosition, .tag = kTagPositionLeft}};

} // namespace

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    if (cluster_id == WindowCovering::Id &&
        attribute_id == WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id) {
        // 目标升降百分比（单位：百分比*100）
        uint16_t target_lift_percent_100ths = val->val.u16;
        uint8_t target_percentage = target_lift_percent_100ths / 100;
        ESP_LOGI(TAG, "Endpoint %d, Target Lift Position updated to %d%% type: %d", endpoint_id, target_percentage,
                 val->type);
        int motor_id = 0;
        if (endpoint_id == motor1_endpoint_id) {
            motor_id = 1;
            motor1_target_lift_percent_100ths = target_lift_percent_100ths;
        } else if (endpoint_id == motor2_endpoint_id) {
            motor_id = 2;
            motor1_target_lift_percent_100ths = target_lift_percent_100ths;
        }
        ESP_LOGI(TAG, "Motor id %d, Target Lift Position updated to %d%%", motor_id, target_percentage);
        chip::DeviceLayer::SystemLayer().ScheduleLambda(
            [motor_id, target_percentage]() { motor_move(motor_id, target_percentage); });

    } else if (cluster_id == WindowCovering::Id &&
               attribute_id == WindowCovering::Attributes::TargetPositionTiltPercent100ths::Id) {
        // 如果需要对 tilt（倾斜）做处理，可类似处理。此处仅作日志输出
        uint16_t target_tilt_percent_100ths = val->val.u16;
        uint8_t target_percentage = target_tilt_percent_100ths / 100;
        ESP_LOGI(TAG, "Endpoint %d, Target Tilt Position updated to %d%%", endpoint_id, target_percentage);
        // 如有独立的 tilt 控制函数，可在此调用，比如：motor_move_tilt(endpoint_id, target_percentage);
    }

    return err;
}

static esp_err_t create_button(struct gpio_button *button, node_t *node)
{
    esp_err_t err = ESP_OK;

    /* Initialize driver */
    app_driver_handle_t button_handle = app_driver_button_init(button);

    /* Create a new endpoint. */
    generic_switch::config_t switch_config;
    endpoint_t *endpoint = generic_switch::create(node, &switch_config, ENDPOINT_FLAG_NONE, button_handle);

    cluster_t *descriptor = cluster::get(endpoint, Descriptor::Id);
    descriptor::feature::taglist::add(descriptor);

    /* These node and endpoint handles can be used to create/add other endpoints and clusters. */
    if (!node || !endpoint) {
        ESP_LOGE(TAG, "Matter node creation failed");
        err = ESP_FAIL;
        return err;
    }

    for (int i = 0; i < configured_buttons; i++) {
        if (button_list[i].button == button) {
            break;
        }
    }

    /* Check for maximum physical buttons that can be configured. */
    if (configured_buttons < CONFIG_MAX_CONFIGURABLE_BUTTONS) {
        button_list[configured_buttons].button = button;
        button_list[configured_buttons].endpoint = endpoint::get_id(endpoint);
        configured_buttons++;
    } else {
        ESP_LOGI(TAG, "Cannot configure more buttons");
        err = ESP_FAIL;
        return err;
    }

    static uint16_t generic_switch_endpoint_id = 0;
    generic_switch_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Generic Switch created with endpoint_id %d", generic_switch_endpoint_id);

    /* Add additional features to the node */
    cluster_t *cluster = cluster::get(endpoint, Switch::Id);

#if CONFIG_GENERIC_SWITCH_TYPE_LATCHING
    cluster::switch_cluster::feature::latching_switch::add(cluster);
#endif

#if CONFIG_GENERIC_SWITCH_TYPE_MOMENTARY
    cluster::switch_cluster::feature::momentary_switch::add(cluster);
    cluster::switch_cluster::feature::action_switch::add(cluster);
    cluster::switch_cluster::feature::momentary_switch_multi_press::config_t msm;
    msm.multi_press_max = 5;
    cluster::switch_cluster::feature::momentary_switch_multi_press::add(cluster, &msm);
#endif

    return err;
}

int get_endpoint(gpio_button *button)
{
    for (int i = 0; i < configured_buttons; i++) {
        if (button_list[i].button == button) {
            return button_list[i].endpoint;
        }
    }
    return -1;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    init_relays();
    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    aggregator::config_t aggregator_config;
    endpoint_t *aggregator = endpoint::aggregator::create(node, &aggregator_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(aggregator != nullptr, ESP_LOGE(TAG, "Failed to create aggregator endpoint"));

    aggregator_endpoint_id = endpoint::get_id(aggregator);

    // /* Call for Boot button */
    // err = create_button(NULL, node);
    // ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to create generic switch button"));

    // 创建并启动周期性定时器，周期为 TIMER_INTERVAL_MS 毫秒
    motor_timer = xTimerCreate("motor_timer", pdMS_TO_TICKS(TIMER_INTERVAL_MS), pdTRUE, NULL, motor_timer_callback);
    if (motor_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create motor timer");
    } else {
        xTimerStart(motor_timer, 0);
    }
    /******************** 注册电机 1 对应的窗帘设备 ********************/
    {
        esp_matter::endpoint::window_covering_device::config_t wc_config1 = {};
        // 根据需要设置 wc_config1 的各项参数（例如设备名称、描述等）
        // context 参数（第四个参数）传入 (void*)1 用于区分电机 1
        endpoint_t *endpoint1 =
            esp_matter::endpoint::window_covering_device::create(node, &wc_config1, ENDPOINT_FLAG_NONE, (void *)1);
        if (endpoint1 == nullptr) {
            ESP_LOGE(TAG, "创建电机1窗帘端点失败");
            return;
        }
        cluster_t *wc_cluster1 = cluster::get(endpoint1, WindowCovering::Id);
        if (wc_cluster1 == nullptr) {
            ESP_LOGE(TAG, "获取电机1窗帘集群失败");
            return;
        }

        // 配置窗帘特性：举升、位置感知举升
        window_covering::feature::lift::config_t lift1 = {};
        window_covering::feature::position_aware_lift::config_t pos_lift1 = {};

        // 初始化相关属性，下面使用 nullable 类型将初始百分比设置为 0
        nullable<uint8_t> percentage(0);
        nullable<uint16_t> percentage_100ths(0);
        pos_lift1.current_position_lift_percentage = percentage;
        pos_lift1.target_position_lift_percent_100ths = percentage_100ths;
        pos_lift1.current_position_lift_percent_100ths = percentage_100ths;

        window_covering::feature::lift::add(wc_cluster1, &lift1);
        window_covering::feature::position_aware_lift::add(wc_cluster1, &pos_lift1);
        endpoint::set_parent_endpoint(endpoint1, aggregator);
        motor1_endpoint_id = endpoint::get_id(endpoint1);
    }

    /******************** 注册电机 2 对应的窗帘设备 ********************/
    {
        esp_matter::endpoint::window_covering_device::config_t wc_config2 = {};
        // context 参数传入 (void*)2 表示该端点对应电机 2
        endpoint_t *endpoint2 =
            esp_matter::endpoint::window_covering_device::create(node, &wc_config2, ENDPOINT_FLAG_NONE, (void *)2);
        if (endpoint2 == nullptr) {
            ESP_LOGE(TAG, "创建电机2窗帘端点失败");
            return;
        }
        cluster_t *wc_cluster2 = cluster::get(endpoint2, WindowCovering::Id);
        if (wc_cluster2 == nullptr) {
            ESP_LOGE(TAG, "获取电机2窗帘集群失败");
            return;
        }

        window_covering::feature::lift::config_t lift2 = {};
        window_covering::feature::position_aware_lift::config_t pos_lift2 = {};

        nullable<uint8_t> percentage(0);
        nullable<uint16_t> percentage_100ths(0);
        pos_lift2.current_position_lift_percentage = percentage;
        pos_lift2.target_position_lift_percent_100ths = percentage_100ths;
        pos_lift2.current_position_lift_percent_100ths = percentage_100ths;

        window_covering::feature::lift::add(wc_cluster2, &lift2);
        window_covering::feature::position_aware_lift::add(wc_cluster2, &pos_lift2);
        endpoint::set_parent_endpoint(endpoint2, aggregator);
        motor1_endpoint_id = endpoint::get_id(endpoint2);
    }

    /* Use the code snippet commented below to create more physical buttons. */

    /*  // Creating a gpio button. More buttons can be created in the same fashion specifying GPIO_PIN_VALUE.
     *  struct gpio_button button;
     *  button.GPIO_PIN_VALUE = GPIO_NUM_6;
     *  // Call to createButton function to configure your button.
     *  create_button(&button, node);
     */
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    // SetTagList(1, chip::Span<const Descriptor::Structs::SemanticTagStruct::Type>(gEp1TagList));
    // SetTagList(2, chip::Span<const Descriptor::Structs::SemanticTagStruct::Type>(gEp2TagList));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
