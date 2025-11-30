#include <homekit/homekit.h>
#include <homekit/characteristics.h>

void accessoryIdentify(homekit_value_t _value) {
    printf("accessory identify\n");
}

homekit_characteristic_t currentPosition = HOMEKIT_CHARACTERISTIC_(CURRENT_POSITION, 0, .format = homekit_format_int);
homekit_characteristic_t targetPosition = HOMEKIT_CHARACTERISTIC_(TARGET_POSITION, 0, .format = homekit_format_int);
homekit_characteristic_t positionState = HOMEKIT_CHARACTERISTIC_(POSITION_STATE, 0, .format = homekit_format_int);

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_window_covering, .services = (homekit_service_t *[]) {
        // Accessory Information: overall accessory metadata
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t *[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Roller Shades"),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Andrei"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "RB-8266-0001"),
            HOMEKIT_CHARACTERISTIC(MODEL, "RB-8266-28BYJ"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.1.0"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, accessoryIdentify),
            NULL
        }),
        // Window Covering service (primary)
        HOMEKIT_SERVICE(WINDOW_COVERING, .primary = true, .characteristics = (homekit_characteristic_t *[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Roller Shades"),
            &currentPosition,
            &targetPosition,
            &positionState,
            NULL
        }),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "281-42-814"
};
