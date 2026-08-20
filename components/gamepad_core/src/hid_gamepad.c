#include "gamepad/hid_gamepad.h"

#include <limits.h>
#include <string.h>

_Static_assert(sizeof(gamepad_hid_layout_t) <= UINT16_MAX,
               "gamepad_hid_layout_t size must fit its public size field");

#define HID_LONG_ITEM_PREFIX 0xFEU

#define HID_ITEM_TYPE_MAIN 0U
#define HID_ITEM_TYPE_GLOBAL 1U
#define HID_ITEM_TYPE_LOCAL 2U

#define HID_MAIN_INPUT 8U
#define HID_MAIN_COLLECTION 10U
#define HID_MAIN_END_COLLECTION 12U

#define HID_GLOBAL_USAGE_PAGE 0U
#define HID_GLOBAL_LOGICAL_MINIMUM 1U
#define HID_GLOBAL_LOGICAL_MAXIMUM 2U
#define HID_GLOBAL_REPORT_SIZE 7U
#define HID_GLOBAL_REPORT_ID 8U
#define HID_GLOBAL_REPORT_COUNT 9U
#define HID_GLOBAL_PUSH 10U
#define HID_GLOBAL_POP 11U

#define HID_LOCAL_USAGE 0U
#define HID_LOCAL_USAGE_MINIMUM 1U
#define HID_LOCAL_USAGE_MAXIMUM 2U
#define HID_LOCAL_DELIMITER 10U

#define HID_COLLECTION_APPLICATION 1U

#define HID_INPUT_CONSTANT (1U << 0)
#define HID_INPUT_VARIABLE (1U << 1)
#define HID_INPUT_RELATIVE (1U << 2)
#define HID_INPUT_NULL_STATE (1U << 6)
#define HID_INPUT_BUFFERED_BYTES (1U << 8)

typedef struct {
    uint32_t usage_page;
    int32_t logical_min;
    uint32_t logical_max_raw;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t logical_max_size;
    uint8_t report_id;
    bool has_logical_min;
    bool has_logical_max;
} hid_globals_t;

typedef struct {
    uint16_t page;
    uint16_t identifier;
} hid_usage_t;

typedef struct {
    hid_usage_t usages[GAMEPAD_HID_MAX_LOCAL_USAGES];
    hid_usage_t usage_min;
    hid_usage_t usage_max;
    uint8_t usage_count;
    bool has_usage_min;
    bool has_usage_max;
} hid_locals_t;

typedef struct {
    bool selected_context;
} hid_collection_t;

typedef struct {
    hid_globals_t globals;
    hid_globals_t global_stack[GAMEPAD_HID_MAX_GLOBAL_STACK_DEPTH];
    hid_locals_t locals;
    hid_collection_t collection_stack[GAMEPAD_HID_MAX_COLLECTION_DEPTH];
    uint16_t input_bits[256];
    uint8_t global_depth;
    uint8_t collection_depth;
    bool selected_application;
    bool saw_report_id_zero;
    bool saw_report_id_nonzero;
} hid_parser_t;

static bool array_mapping_supported(int32_t logical_min,
                                    int32_t logical_max,
                                    uint16_t usage_min,
                                    uint16_t usage_max);
static bool logical_range_fits_bits(int32_t logical_min,
                                    int32_t logical_max,
                                    uint8_t bit_size);

static bool layout_valid(const gamepad_hid_layout_t *layout)
{
    if (layout == NULL || layout->version != GAMEPAD_HID_LAYOUT_VERSION ||
        layout->size != sizeof(*layout) || layout->field_count > GAMEPAD_HID_MAX_FIELDS ||
        layout->field_count == 0U || layout->report_count > GAMEPAD_HID_MAX_REPORTS ||
        layout->report_count == 0U || layout->uses_report_ids > 1U ||
        layout->application_collection_count == 0U ||
        (layout->selected_application_usage != GAMEPAD_HID_USAGE_JOYSTICK &&
         layout->selected_application_usage != GAMEPAD_HID_USAGE_GAME_PAD &&
         layout->selected_application_usage != GAMEPAD_HID_USAGE_MULTI_AXIS_CONTROLLER)) {
        return false;
    }

    for (uint8_t report_index = 0; report_index < layout->report_count; ++report_index) {
        const gamepad_hid_report_t *report = &layout->reports[report_index];
        if (report->payload_bits == 0U ||
            report->payload_bits > GAMEPAD_HID_MAX_REPORT_BYTES * 8U ||
            report->payload_bytes != (report->payload_bits + 7U) / 8U ||
            (layout->uses_report_ids != 0U && report->report_id == 0U) ||
            (layout->uses_report_ids == 0U && report->report_id != 0U)) {
            return false;
        }
        for (uint8_t prior = 0; prior < report_index; ++prior) {
            if (layout->reports[prior].report_id == report->report_id) {
                return false;
            }
        }
    }

    for (uint16_t field_index = 0; field_index < layout->field_count; ++field_index) {
        const gamepad_hid_field_t *field = &layout->fields[field_index];
        if (field->logical_max <= field->logical_min || field->bit_size == 0U ||
            field->bit_size > 32U || field->map_target > GAMEPAD_HID_MAP_DPAD_Y ||
            !logical_range_fits_bits(field->logical_min, field->logical_max,
                                     field->bit_size) ||
            (field->flags & ~(GAMEPAD_HID_FIELD_NULL_STATE | GAMEPAD_HID_FIELD_ARRAY)) !=
                0U) {
            return false;
        }
        if ((field->flags & GAMEPAD_HID_FIELD_ARRAY) != 0U &&
            (field->usage_page != GAMEPAD_HID_USAGE_PAGE_BUTTON ||
             field->usage > field->usage_max || field->usage_max == 0U ||
             field->usage > GAMEPAD_BUTTON_COUNT ||
             !array_mapping_supported(field->logical_min, field->logical_max,
                                      field->usage, field->usage_max) ||
             (field->map_target != GAMEPAD_HID_MAP_IGNORE &&
              field->map_target != GAMEPAD_HID_MAP_BUTTON))) {
            return false;
        }
        if ((field->flags & GAMEPAD_HID_FIELD_ARRAY) == 0U &&
            field->map_target == GAMEPAD_HID_MAP_BUTTON &&
            field->map_index >= GAMEPAD_BUTTON_COUNT) {
            return false;
        }

        bool report_found = false;
        for (uint8_t report_index = 0; report_index < layout->report_count;
             ++report_index) {
            const gamepad_hid_report_t *report = &layout->reports[report_index];
            if (report->report_id == field->report_id) {
                report_found = true;
                if ((uint32_t)field->bit_offset + field->bit_size >
                    report->payload_bits) {
                    return false;
                }
                break;
            }
        }
        if (!report_found) {
            return false;
        }
    }
    return true;
}

static bool array_mapping_supported(int32_t logical_min,
                                    int32_t logical_max,
                                    uint16_t usage_min,
                                    uint16_t usage_max)
{
    if (logical_max <= logical_min || usage_min > usage_max) {
        return false;
    }

    const int64_t logical_count = (int64_t)logical_max - logical_min + 1;
    const int64_t usage_count = (int64_t)usage_max - usage_min + 1;
    const bool ordinal = logical_count == usage_count;
    const bool direct = logical_min <= (int32_t)usage_min &&
                        logical_max >= (int32_t)usage_max;
    return ordinal || direct;
}

static bool logical_range_fits_bits(int32_t logical_min,
                                    int32_t logical_max,
                                    uint8_t bit_size)
{
    if (logical_max <= logical_min || bit_size == 0U || bit_size > 32U) {
        return false;
    }

    if (logical_min < 0) {
        if (bit_size == 32U) {
            return true;
        }
        const int64_t signed_limit = INT64_C(1) << (bit_size - 1U);
        return logical_min >= -signed_limit && logical_max < signed_limit;
    }

    const uint64_t unsigned_max = bit_size == 32U
                                      ? UINT32_MAX
                                      : (UINT64_C(1) << bit_size) - 1U;
    return (uint64_t)logical_max <= unsigned_max;
}

static uint32_t read_unsigned(const uint8_t *data, uint8_t size)
{
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; ++index) {
        value |= (uint32_t)data[index] << (8U * index);
    }
    return value;
}

static int32_t sign_extend_bytes(uint32_t value, uint8_t size)
{
    const uint8_t bit_count = (uint8_t)(size * 8U);
    if (bit_count == 0U) {
        return 0;
    }
    if (bit_count < 32U) {
        const uint32_t sign_bit = UINT32_C(1) << (bit_count - 1U);
        if ((value & sign_bit) != 0U) {
            const uint32_t mask = (UINT32_C(1) << bit_count) - 1U;
            value |= ~mask;
        }
    }
    return (int32_t)value;
}

static int64_t sign_extend_bits(uint32_t value, uint8_t bit_count)
{
    if (bit_count == 32U) {
        return (int64_t)(int32_t)value;
    }

    const uint32_t sign_bit = UINT32_C(1) << (bit_count - 1U);
    if ((value & sign_bit) != 0U) {
        const uint32_t mask = (UINT32_C(1) << bit_count) - 1U;
        value |= ~mask;
    }
    return (int64_t)(int32_t)value;
}

static void reset_locals(hid_locals_t *locals)
{
    memset(locals, 0, sizeof(*locals));
}

static bool decode_usage(uint32_t raw_usage,
                         uint8_t item_size,
                         uint32_t default_page,
                         hid_usage_t *usage)
{
    if (usage == NULL || item_size == 0U || default_page > UINT16_MAX) {
        return false;
    }

    uint32_t page = default_page;
    uint32_t identifier = raw_usage;
    if (item_size == 4U) {
        page = raw_usage >> 16U;
        identifier = raw_usage & UINT16_MAX;
    }
    if (page > UINT16_MAX || identifier > UINT16_MAX) {
        return false;
    }
    usage->page = (uint16_t)page;
    usage->identifier = (uint16_t)identifier;
    return true;
}

static bool resolve_usage(const hid_parser_t *parser,
                          uint32_t field_index,
                          uint16_t *usage_page,
                          uint16_t *usage)
{
    const hid_locals_t *locals = &parser->locals;

    if (locals->usage_count > 0U) {
        const uint32_t selected = field_index < locals->usage_count
                                      ? field_index
                                      : (uint32_t)locals->usage_count - 1U;
        *usage_page = locals->usages[selected].page;
        *usage = locals->usages[selected].identifier;
        return true;
    } else if (locals->has_usage_min && locals->has_usage_max) {
        if (locals->usage_min.page != locals->usage_max.page ||
            locals->usage_min.identifier > locals->usage_max.identifier) {
            return false;
        }
        const uint32_t candidate = (uint32_t)locals->usage_min.identifier + field_index;
        *usage_page = locals->usage_min.page;
        *usage = candidate <= locals->usage_max.identifier
                     ? (uint16_t)candidate
                     : locals->usage_max.identifier;
        return true;
    } else {
        return false;
    }
}

static bool resolve_contiguous_usage_range(const hid_parser_t *parser,
                                           uint16_t *usage_page,
                                           uint16_t *usage_min,
                                           uint16_t *usage_max)
{
    const hid_locals_t *locals = &parser->locals;
    if (locals->usage_count > 0U) {
        const uint16_t first_page = locals->usages[0].page;
        const uint16_t first_usage = locals->usages[0].identifier;

        uint16_t previous = first_usage;
        for (uint8_t index = 1; index < locals->usage_count; ++index) {
            const uint16_t current_page = locals->usages[index].page;
            const uint16_t current_usage = locals->usages[index].identifier;
            if (current_page != first_page || previous == UINT16_MAX ||
                current_usage != (uint16_t)(previous + 1U)) {
                return false;
            }
            previous = current_usage;
        }

        *usage_page = first_page;
        *usage_min = first_usage;
        *usage_max = previous;
        return true;
    }

    if (!locals->has_usage_min || !locals->has_usage_max) {
        return false;
    }
    *usage_page = locals->usage_min.page;
    *usage_min = locals->usage_min.identifier;
    *usage_max = locals->usage_max.identifier;
    if (locals->usage_max.page != *usage_page || *usage_min > *usage_max) {
        return false;
    }
    return true;
}

static bool current_collection_selected(const hid_parser_t *parser)
{
    return parser->collection_depth > 0U &&
           parser->collection_stack[parser->collection_depth - 1U].selected_context;
}

static bool is_gamepad_application(uint16_t usage_page, uint16_t usage)
{
    return usage_page == GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP &&
           (usage == GAMEPAD_HID_USAGE_JOYSTICK || usage == GAMEPAD_HID_USAGE_GAME_PAD ||
            usage == GAMEPAD_HID_USAGE_MULTI_AXIS_CONTROLLER);
}

static bool mapping_target_used(const gamepad_hid_layout_t *layout,
                                gamepad_hid_map_target_t target)
{
    for (uint16_t index = 0; index < layout->field_count; ++index) {
        if (layout->fields[index].map_target == (uint8_t)target) {
            return true;
        }
    }
    return false;
}

static bool generic_mapping(const gamepad_hid_layout_t *layout,
                            uint16_t usage_page,
                            uint16_t usage,
                            int32_t logical_min,
                            int32_t logical_max,
                            gamepad_hid_map_target_t *target,
                            uint8_t *map_index)
{
    *target = GAMEPAD_HID_MAP_IGNORE;
    *map_index = 0;

    if (usage_page == GAMEPAD_HID_USAGE_PAGE_BUTTON && usage >= 1U &&
        usage <= GAMEPAD_BUTTON_COUNT && logical_min == 0 && logical_max >= 1) {
        *target = GAMEPAD_HID_MAP_BUTTON;
        *map_index = (uint8_t)(usage - 1U);
        return true;
    }

    if (usage_page != GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP) {
        return false;
    }

    switch (usage) {
    case GAMEPAD_HID_USAGE_X:
        *target = GAMEPAD_HID_MAP_LEFT_X;
        break;
    case GAMEPAD_HID_USAGE_Y:
        *target = GAMEPAD_HID_MAP_LEFT_Y;
        break;
    case GAMEPAD_HID_USAGE_RX:
        *target = GAMEPAD_HID_MAP_RIGHT_X;
        break;
    case GAMEPAD_HID_USAGE_RY:
        *target = GAMEPAD_HID_MAP_RIGHT_Y;
        break;
    case GAMEPAD_HID_USAGE_Z:
        *target = GAMEPAD_HID_MAP_LEFT_TRIGGER;
        break;
    case GAMEPAD_HID_USAGE_RZ:
        *target = GAMEPAD_HID_MAP_RIGHT_TRIGGER;
        break;
    case GAMEPAD_HID_USAGE_HAT_SWITCH: {
        const int64_t positions = (int64_t)logical_max - logical_min + 1;
        if (positions == 4 || positions == 8) {
            *target = GAMEPAD_HID_MAP_HAT;
        }
        break;
    }
    default:
        return false;
    }

    if (*target == GAMEPAD_HID_MAP_IGNORE || mapping_target_used(layout, *target)) {
        return false;
    }
    return true;
}

static bool potentially_mappable_usage(uint16_t usage_page, uint16_t usage)
{
    if (usage_page == GAMEPAD_HID_USAGE_PAGE_BUTTON) {
        return usage >= 1U && usage <= GAMEPAD_BUTTON_COUNT;
    }
    if (usage_page != GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP) {
        return false;
    }
    return usage == GAMEPAD_HID_USAGE_X || usage == GAMEPAD_HID_USAGE_Y ||
           usage == GAMEPAD_HID_USAGE_Z || usage == GAMEPAD_HID_USAGE_RX ||
           usage == GAMEPAD_HID_USAGE_RY || usage == GAMEPAD_HID_USAGE_RZ ||
           usage == GAMEPAD_HID_USAGE_HAT_SWITCH;
}

static gamepad_status_t resolve_logical_range(const hid_globals_t *globals,
                                              int32_t *logical_min,
                                              int32_t *logical_max)
{
    if (!globals->has_logical_min || !globals->has_logical_max ||
        globals->logical_max_size == 0U) {
        return GAMEPAD_ERR_MALFORMED;
    }

    *logical_min = globals->logical_min;
    if (*logical_min < 0) {
        *logical_max = sign_extend_bytes(globals->logical_max_raw,
                                         globals->logical_max_size);
    } else {
        if (globals->logical_max_raw > INT32_MAX) {
            return GAMEPAD_ERR_UNSUPPORTED;
        }
        *logical_max = (int32_t)globals->logical_max_raw;
    }

    if (*logical_max <= *logical_min) {
        return GAMEPAD_ERR_MALFORMED;
    }
    return GAMEPAD_OK;
}

static gamepad_status_t add_field(gamepad_hid_layout_t *layout,
                                  const hid_parser_t *parser,
                                  uint16_t usage_page,
                                  uint16_t usage,
                                  uint16_t bit_offset,
                                  uint8_t input_flags)
{
    if (!potentially_mappable_usage(usage_page, usage)) {
        return GAMEPAD_OK;
    }

    int32_t logical_min = 0;
    int32_t logical_max = 0;
    const gamepad_status_t range_status =
        resolve_logical_range(&parser->globals, &logical_min, &logical_max);
    if (range_status != GAMEPAD_OK) {
        return range_status;
    }
    if (parser->globals.report_size == 0U || parser->globals.report_size > 32U) {
        return GAMEPAD_ERR_UNSUPPORTED;
    }
    if (!logical_range_fits_bits(logical_min, logical_max,
                                 (uint8_t)parser->globals.report_size)) {
        return GAMEPAD_ERR_MALFORMED;
    }

    gamepad_hid_map_target_t target = GAMEPAD_HID_MAP_IGNORE;
    uint8_t map_index = 0;
    if (!generic_mapping(layout, usage_page, usage, logical_min, logical_max, &target,
                         &map_index)) {
        return GAMEPAD_OK;
    }
    if (layout->field_count >= GAMEPAD_HID_MAX_FIELDS) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }

    gamepad_hid_field_t *field = &layout->fields[layout->field_count++];
    field->logical_min = logical_min;
    field->logical_max = logical_max;
    field->usage_page = usage_page;
    field->usage = usage;
    field->usage_max = usage;
    field->bit_offset = bit_offset;
    field->bit_size = (uint8_t)parser->globals.report_size;
    field->report_id = parser->globals.report_id;
    field->map_target = (uint8_t)target;
    field->map_index = map_index;
    field->flags = (input_flags & HID_INPUT_NULL_STATE) != 0U
                       ? GAMEPAD_HID_FIELD_NULL_STATE
                       : 0U;
    return GAMEPAD_OK;
}

static gamepad_status_t add_button_array_fields(gamepad_hid_layout_t *layout,
                                                const hid_parser_t *parser,
                                                uint16_t base_offset,
                                                uint8_t input_flags)
{
    uint16_t usage_page = 0;
    uint16_t usage_min = 0;
    uint16_t usage_max = 0;
    if (!resolve_contiguous_usage_range(parser, &usage_page, &usage_min, &usage_max) ||
        usage_page != GAMEPAD_HID_USAGE_PAGE_BUTTON || usage_max == 0U ||
        usage_min > GAMEPAD_BUTTON_COUNT) {
        return GAMEPAD_OK;
    }

    if (parser->globals.report_size == 0U || parser->globals.report_size > 32U) {
        return GAMEPAD_ERR_UNSUPPORTED;
    }

    int32_t logical_min = 0;
    int32_t logical_max = 0;
    const gamepad_status_t range_status =
        resolve_logical_range(&parser->globals, &logical_min, &logical_max);
    if (range_status != GAMEPAD_OK) {
        return range_status;
    }
    if (!array_mapping_supported(logical_min, logical_max, usage_min, usage_max)) {
        return GAMEPAD_OK;
    }
    if (!logical_range_fits_bits(logical_min, logical_max,
                                 (uint8_t)parser->globals.report_size)) {
        return GAMEPAD_ERR_MALFORMED;
    }

    for (uint32_t index = 0; index < parser->globals.report_count; ++index) {
        if (layout->field_count >= GAMEPAD_HID_MAX_FIELDS) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        const uint64_t bit_offset =
            (uint64_t)base_offset + (uint64_t)index * parser->globals.report_size;
        if (bit_offset > UINT16_MAX) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }

        gamepad_hid_field_t *field = &layout->fields[layout->field_count++];
        field->logical_min = logical_min;
        field->logical_max = logical_max;
        field->usage_page = usage_page;
        field->usage = usage_min;
        field->usage_max = usage_max;
        field->bit_offset = (uint16_t)bit_offset;
        field->bit_size = (uint8_t)parser->globals.report_size;
        field->report_id = parser->globals.report_id;
        field->map_target = GAMEPAD_HID_MAP_BUTTON;
        field->map_index = 0U;
        field->flags = GAMEPAD_HID_FIELD_ARRAY;
        if ((input_flags & HID_INPUT_NULL_STATE) != 0U) {
            field->flags |= GAMEPAD_HID_FIELD_NULL_STATE;
        }
    }
    return GAMEPAD_OK;
}

static gamepad_hid_report_t *find_report(gamepad_hid_layout_t *layout, uint8_t report_id)
{
    for (uint8_t index = 0; index < layout->report_count; ++index) {
        if (layout->reports[index].report_id == report_id) {
            return &layout->reports[index];
        }
    }
    return NULL;
}

static const gamepad_hid_report_t *find_const_report(const gamepad_hid_layout_t *layout,
                                                     uint8_t report_id)
{
    for (uint8_t index = 0; index < layout->report_count; ++index) {
        if (layout->reports[index].report_id == report_id) {
            return &layout->reports[index];
        }
    }
    return NULL;
}

static gamepad_status_t update_report_size(gamepad_hid_layout_t *layout,
                                           uint8_t report_id,
                                           uint32_t total_bits)
{
    if (total_bits > GAMEPAD_HID_MAX_REPORT_BYTES * 8U || total_bits > UINT16_MAX) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }

    gamepad_hid_report_t *report = find_report(layout, report_id);
    if (report == NULL) {
        if (layout->report_count >= GAMEPAD_HID_MAX_REPORTS) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        report = &layout->reports[layout->report_count++];
        report->report_id = report_id;
    }
    report->payload_bits = (uint16_t)total_bits;
    report->payload_bytes = (uint16_t)((total_bits + 7U) / 8U);
    return GAMEPAD_OK;
}

static gamepad_status_t parse_main_input(uint32_t input_flags,
                                         hid_parser_t *parser,
                                         gamepad_hid_layout_t *layout)
{
    if (parser->globals.report_count > GAMEPAD_HID_MAX_REPORT_COUNT) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }
    if (parser->globals.report_count == 0U || parser->globals.report_size == 0U) {
        return GAMEPAD_ERR_MALFORMED;
    }

    const uint64_t added_bits =
        (uint64_t)parser->globals.report_size * parser->globals.report_count;
    if (added_bits > GAMEPAD_HID_MAX_REPORT_BYTES * 8U) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }

    if (!current_collection_selected(parser)) {
        return GAMEPAD_OK;
    }

    const uint64_t old_bits = parser->input_bits[parser->globals.report_id];
    if (old_bits + added_bits > GAMEPAD_HID_MAX_REPORT_BYTES * 8U) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }

    if (parser->globals.report_id == 0U) {
        parser->saw_report_id_zero = true;
    } else {
        parser->saw_report_id_nonzero = true;
    }
    if (parser->saw_report_id_zero && parser->saw_report_id_nonzero) {
        return GAMEPAD_ERR_MALFORMED;
    }

    const uint16_t base_offset = parser->input_bits[parser->globals.report_id];
    const bool is_data_absolute = (input_flags & HID_INPUT_CONSTANT) == 0U &&
                                  (input_flags & HID_INPUT_RELATIVE) == 0U &&
                                  (input_flags & HID_INPUT_BUFFERED_BYTES) == 0U;

    if (is_data_absolute) {
        if ((input_flags & HID_INPUT_VARIABLE) != 0U) {
            for (uint32_t index = 0; index < parser->globals.report_count; ++index) {
                uint16_t usage_page = 0;
                uint16_t usage = 0;
                if (!resolve_usage(parser, index, &usage_page, &usage)) {
                    continue;
                }
                const uint64_t bit_offset =
                    (uint64_t)base_offset + (uint64_t)index * parser->globals.report_size;
                if (bit_offset > UINT16_MAX) {
                    return GAMEPAD_ERR_LIMIT_EXCEEDED;
                }
                const gamepad_status_t add_status =
                    add_field(layout, parser, usage_page, usage, (uint16_t)bit_offset,
                              (uint8_t)input_flags);
                if (add_status != GAMEPAD_OK) {
                    return add_status;
                }
            }
        } else {
            const gamepad_status_t add_status =
                add_button_array_fields(layout, parser, base_offset,
                                        (uint8_t)input_flags);
            if (add_status != GAMEPAD_OK) {
                return add_status;
            }
        }
    }

    const uint32_t new_bits = (uint32_t)(old_bits + added_bits);
    parser->input_bits[parser->globals.report_id] = (uint16_t)new_bits;
    return update_report_size(layout, parser->globals.report_id, new_bits);
}

static gamepad_status_t parse_collection(uint32_t collection_type,
                                         hid_parser_t *parser,
                                         gamepad_hid_layout_t *layout)
{
    if (parser->collection_depth >= GAMEPAD_HID_MAX_COLLECTION_DEPTH) {
        return GAMEPAD_ERR_LIMIT_EXCEEDED;
    }

    uint16_t usage_page = 0;
    uint16_t usage = 0;
    const bool has_usage = resolve_usage(parser, 0, &usage_page, &usage);
    const bool gamepad_application = parser->collection_depth == 0U && has_usage &&
                                     collection_type == HID_COLLECTION_APPLICATION &&
                                     is_gamepad_application(usage_page, usage);
    const bool parent_selected = current_collection_selected(parser);
    bool selected_context = parent_selected;

    if (gamepad_application) {
        if (layout->application_collection_count == UINT8_MAX) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        layout->application_collection_count++;
        if (!parser->selected_application) {
            parser->selected_application = true;
            layout->selected_application_usage = (uint8_t)usage;
            selected_context = true;
        }
    }

    parser->collection_stack[parser->collection_depth++].selected_context = selected_context;
    return GAMEPAD_OK;
}

static gamepad_status_t parse_global_item(uint8_t tag,
                                          uint8_t size,
                                          uint32_t value,
                                          hid_parser_t *parser)
{
    hid_globals_t *globals = &parser->globals;
    switch (tag) {
    case HID_GLOBAL_USAGE_PAGE:
        if (size == 0U || value > UINT16_MAX) {
            return GAMEPAD_ERR_MALFORMED;
        }
        globals->usage_page = value;
        break;
    case HID_GLOBAL_LOGICAL_MINIMUM:
        if (size == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        globals->logical_min = sign_extend_bytes(value, size);
        globals->has_logical_min = true;
        break;
    case HID_GLOBAL_LOGICAL_MAXIMUM:
        if (size == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        globals->logical_max_raw = value;
        globals->logical_max_size = size;
        globals->has_logical_max = true;
        break;
    case HID_GLOBAL_REPORT_SIZE:
        if (size == 0U || value == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (value > GAMEPAD_HID_MAX_REPORT_BYTES * 8U) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        globals->report_size = value;
        break;
    case HID_GLOBAL_REPORT_ID:
        if (size == 0U || value == 0U || value > UINT8_MAX) {
            return GAMEPAD_ERR_MALFORMED;
        }
        globals->report_id = (uint8_t)value;
        break;
    case HID_GLOBAL_REPORT_COUNT:
        if (size == 0U || value == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (value > GAMEPAD_HID_MAX_REPORT_COUNT) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        globals->report_count = value;
        break;
    case HID_GLOBAL_PUSH:
        if (size != 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (parser->global_depth >= GAMEPAD_HID_MAX_GLOBAL_STACK_DEPTH) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        parser->global_stack[parser->global_depth++] = *globals;
        break;
    case HID_GLOBAL_POP:
        if (size != 0U || parser->global_depth == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        *globals = parser->global_stack[--parser->global_depth];
        break;
    default:
        break;
    }
    return GAMEPAD_OK;
}

static gamepad_status_t parse_local_item(uint8_t tag,
                                         uint8_t size,
                                         uint32_t value,
                                         hid_parser_t *parser)
{
    hid_locals_t *locals = &parser->locals;
    switch (tag) {
    case HID_LOCAL_USAGE:
        if (size == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (locals->usage_count >= GAMEPAD_HID_MAX_LOCAL_USAGES) {
            return GAMEPAD_ERR_LIMIT_EXCEEDED;
        }
        if (!decode_usage(value, size, parser->globals.usage_page,
                          &locals->usages[locals->usage_count])) {
            return GAMEPAD_ERR_MALFORMED;
        }
        locals->usage_count++;
        break;
    case HID_LOCAL_USAGE_MINIMUM:
        if (size == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (!decode_usage(value, size, parser->globals.usage_page,
                          &locals->usage_min)) {
            return GAMEPAD_ERR_MALFORMED;
        }
        locals->has_usage_min = true;
        break;
    case HID_LOCAL_USAGE_MAXIMUM:
        if (size == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }
        if (!decode_usage(value, size, parser->globals.usage_page,
                          &locals->usage_max)) {
            return GAMEPAD_ERR_MALFORMED;
        }
        locals->has_usage_max = true;
        break;
    case HID_LOCAL_DELIMITER:
        return GAMEPAD_ERR_UNSUPPORTED;
    default:
        break;
    }
    return GAMEPAD_OK;
}

static gamepad_status_t parse_descriptor_impl(const uint8_t *descriptor,
                                              size_t descriptor_size,
                                              gamepad_hid_layout_t *layout)
{
    hid_parser_t parser;
    memset(&parser, 0, sizeof(parser));

    size_t offset = 0;
    while (offset < descriptor_size) {
        const uint8_t prefix = descriptor[offset++];
        if (prefix == HID_LONG_ITEM_PREFIX) {
            if (descriptor_size - offset < 2U) {
                return GAMEPAD_ERR_TRUNCATED;
            }
            const size_t payload_size = descriptor[offset];
            offset += 2U; /* Size and long-item tag. */
            if (payload_size > descriptor_size - offset) {
                return GAMEPAD_ERR_TRUNCATED;
            }
            offset += payload_size;
            continue;
        }

        const uint8_t size_code = prefix & 0x03U;
        const uint8_t item_size = size_code == 3U ? 4U : size_code;
        const uint8_t item_type = (prefix >> 2U) & 0x03U;
        const uint8_t item_tag = (prefix >> 4U) & 0x0FU;
        if (item_size > descriptor_size - offset) {
            return GAMEPAD_ERR_TRUNCATED;
        }
        const uint32_t value = read_unsigned(&descriptor[offset], item_size);
        offset += item_size;

        gamepad_status_t status = GAMEPAD_OK;
        if (item_type == HID_ITEM_TYPE_MAIN) {
            switch (item_tag) {
            case HID_MAIN_INPUT:
                status = item_size == 0U
                             ? GAMEPAD_ERR_MALFORMED
                             : parse_main_input(value, &parser, layout);
                break;
            case HID_MAIN_COLLECTION:
                status = item_size != 1U
                             ? GAMEPAD_ERR_MALFORMED
                             : parse_collection(value, &parser, layout);
                break;
            case HID_MAIN_END_COLLECTION:
                if (item_size != 0U || parser.collection_depth == 0U) {
                    status = GAMEPAD_ERR_MALFORMED;
                } else {
                    parser.collection_depth--;
                }
                break;
            default:
                break;
            }
            reset_locals(&parser.locals);
        } else if (item_type == HID_ITEM_TYPE_GLOBAL) {
            status = parse_global_item(item_tag, item_size, value, &parser);
        } else if (item_type == HID_ITEM_TYPE_LOCAL) {
            status = parse_local_item(item_tag, item_size, value, &parser);
        }

        if (status != GAMEPAD_OK) {
            return status;
        }
    }

    if (parser.collection_depth != 0U || parser.global_depth != 0U) {
        return GAMEPAD_ERR_MALFORMED;
    }
    if (!parser.selected_application || layout->application_collection_count == 0U) {
        return GAMEPAD_ERR_NO_GAMEPAD;
    }
    if (layout->field_count == 0U) {
        return GAMEPAD_ERR_NO_MAPPABLE_INPUT;
    }
    layout->uses_report_ids = parser.saw_report_id_nonzero ? 1U : 0U;
    return layout_valid(layout) ? GAMEPAD_OK : GAMEPAD_ERR_MALFORMED;
}

void gamepad_hid_layout_init(gamepad_hid_layout_t *layout)
{
    if (layout == NULL) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    layout->version = GAMEPAD_HID_LAYOUT_VERSION;
    layout->size = (uint16_t)sizeof(*layout);
}

gamepad_status_t gamepad_hid_parse_descriptor(const uint8_t *descriptor,
                                               size_t descriptor_size,
                                               gamepad_hid_layout_t *layout)
{
    if (layout == NULL) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    gamepad_hid_layout_init(layout);
    if (descriptor == NULL) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (descriptor_size > GAMEPAD_HID_MAX_DESCRIPTOR_BYTES) {
        return GAMEPAD_ERR_DESCRIPTOR_TOO_LARGE;
    }
    if (descriptor_size == 0U) {
        return GAMEPAD_ERR_NO_GAMEPAD;
    }

    const gamepad_status_t status = parse_descriptor_impl(descriptor, descriptor_size, layout);
    if (status != GAMEPAD_OK) {
        gamepad_hid_layout_init(layout);
    }
    return status;
}

gamepad_status_t gamepad_hid_set_field_mapping(gamepad_hid_layout_t *layout,
                                               size_t field_index,
                                               gamepad_hid_map_target_t target,
                                               uint8_t map_index)
{
    if (!layout_valid(layout)) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    if (field_index >= layout->field_count || target < GAMEPAD_HID_MAP_IGNORE ||
        target > GAMEPAD_HID_MAP_DPAD_Y) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (target == GAMEPAD_HID_MAP_BUTTON && map_index >= GAMEPAD_BUTTON_COUNT) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }

    const bool is_array =
        (layout->fields[field_index].flags & GAMEPAD_HID_FIELD_ARRAY) != 0U;
    if (is_array && target != GAMEPAD_HID_MAP_IGNORE &&
        target != GAMEPAD_HID_MAP_BUTTON) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }

    layout->fields[field_index].map_target = (uint8_t)target;
    layout->fields[field_index].map_index =
        target == GAMEPAD_HID_MAP_BUTTON && !is_array ? map_index : 0U;
    return GAMEPAD_OK;
}

static bool usb_gamepad_0079_0011_layout_matches(
    const gamepad_hid_layout_t *layout)
{
    if (!layout_valid(layout) || layout->field_count != 12U ||
        layout->report_count != 1U || layout->uses_report_ids != 0U ||
        layout->application_collection_count != 1U ||
        layout->selected_application_usage != GAMEPAD_HID_USAGE_JOYSTICK ||
        layout->reports[0].report_id != 0U ||
        layout->reports[0].payload_bits != 64U ||
        layout->reports[0].payload_bytes != 8U) {
        return false;
    }

    const gamepad_hid_field_t *left_x = &layout->fields[0];
    const gamepad_hid_field_t *left_y = &layout->fields[1];
    if (left_x->logical_min != 0 || left_x->logical_max != 255 ||
        left_x->usage_page != GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP ||
        left_x->usage != GAMEPAD_HID_USAGE_X || left_x->bit_offset != 0U ||
        left_x->bit_size != 8U || left_x->report_id != 0U ||
        left_x->map_target != GAMEPAD_HID_MAP_LEFT_X || left_x->flags != 0U ||
        left_y->logical_min != 0 || left_y->logical_max != 255 ||
        left_y->usage_page != GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP ||
        left_y->usage != GAMEPAD_HID_USAGE_Y || left_y->bit_offset != 32U ||
        left_y->bit_size != 8U || left_y->report_id != 0U ||
        left_y->map_target != GAMEPAD_HID_MAP_LEFT_Y || left_y->flags != 0U) {
        return false;
    }

    for (uint16_t button = 0U; button < 10U; ++button) {
        const gamepad_hid_field_t *field = &layout->fields[button + 2U];
        if (field->logical_min != 0 || field->logical_max != 1 ||
            field->usage_page != GAMEPAD_HID_USAGE_PAGE_BUTTON ||
            field->usage != button + 1U || field->usage_max != button + 1U ||
            field->bit_offset != 44U + button || field->bit_size != 1U ||
            field->report_id != 0U ||
            field->map_target != GAMEPAD_HID_MAP_BUTTON ||
            field->map_index != button || field->flags != 0U) {
            return false;
        }
    }
    return true;
}

static gamepad_status_t apply_usb_gamepad_0079_0011_profile(
    gamepad_hid_layout_t *layout)
{
    if (!usb_gamepad_0079_0011_layout_matches(layout) ||
        layout->field_count >= GAMEPAD_HID_MAX_FIELDS) {
        return GAMEPAD_ERR_UNSUPPORTED;
    }

    gamepad_hid_layout_t next = *layout;
    next.fields[0].map_target = GAMEPAD_HID_MAP_IGNORE;
    next.fields[1].map_target = GAMEPAD_HID_MAP_DPAD_Y;

    /*
     * This controller reports its SNES face buttons in Y, B, A, X order.
     * Publish physical labels into the canonical layout so Console OS sees
     * A as South/accept and B as East/back. The remaining two unnamed HID
     * buttons are deliberately ignored; Select and Start are usages 9/10.
     */
    static const gamepad_button_t button_map[10] = {
        GAMEPAD_BUTTON_WEST,
        GAMEPAD_BUTTON_EAST,
        GAMEPAD_BUTTON_SOUTH,
        GAMEPAD_BUTTON_NORTH,
        GAMEPAD_BUTTON_LEFT_SHOULDER,
        GAMEPAD_BUTTON_RIGHT_SHOULDER,
        GAMEPAD_BUTTON_MISC_1,
        GAMEPAD_BUTTON_PADDLE_1,
        GAMEPAD_BUTTON_BACK,
        GAMEPAD_BUTTON_START,
    };
    for (size_t button = 0U; button < 10U; ++button) {
        next.fields[button + 2U].map_index = (uint8_t)button_map[button];
    }
    next.fields[8].map_target = GAMEPAD_HID_MAP_IGNORE;
    next.fields[9].map_target = GAMEPAD_HID_MAP_IGNORE;

    /*
     * Retrolink's exact mapping uses axis 3 for left/right and axis 4 for
     * up/down. The malformed descriptor declares four consecutive X usages,
     * so the generic parser intentionally retains only the first one. Restore
     * the fourth declared axis from bit 24 and pair it with the retained Y
     * axis at bit 32. The exact descriptor hash below keeps this quirk narrow.
     */
    next.fields[next.field_count++] = (gamepad_hid_field_t){
        .logical_min = 0,
        .logical_max = 255,
        .usage_page = GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP,
        .usage = GAMEPAD_HID_USAGE_X,
        .usage_max = GAMEPAD_HID_USAGE_X,
        .bit_offset = 24U,
        .bit_size = 8U,
        .report_id = 0U,
        .map_target = GAMEPAD_HID_MAP_DPAD_X,
        .map_index = 0U,
        .flags = 0U,
        .reserved = 0U,
    };
    if (!layout_valid(&next)) {
        return GAMEPAD_ERR_UNSUPPORTED;
    }
    *layout = next;
    return GAMEPAD_OK;
}

gamepad_status_t gamepad_hid_apply_known_profile(
    uint16_t vendor_id,
    uint16_t product_id,
    const uint8_t descriptor_sha256[GAMEPAD_HID_DESCRIPTOR_SHA256_BYTES],
    gamepad_hid_layout_t *layout,
    gamepad_hid_profile_t *applied_profile)
{
    static const uint8_t usb_gamepad_0079_0011_descriptor_sha256[] = {
        0x05, 0xa1, 0x51, 0xc9, 0x32, 0x36, 0x2f, 0xee,
        0x13, 0x50, 0x39, 0x05, 0x96, 0x28, 0x80, 0xce,
        0x75, 0x21, 0x2b, 0xbf, 0x8c, 0x43, 0xc9, 0x57,
        0x01, 0x52, 0x7b, 0x82, 0x90, 0x83, 0x31, 0x62,
    };

    if (descriptor_sha256 == NULL || layout == NULL ||
        applied_profile == NULL) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    *applied_profile = GAMEPAD_HID_PROFILE_NONE;
    if (!layout_valid(layout)) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    if (vendor_id != 0x0079U || product_id != 0x0011U) {
        return GAMEPAD_OK;
    }
    if (memcmp(descriptor_sha256, usb_gamepad_0079_0011_descriptor_sha256,
               sizeof(usb_gamepad_0079_0011_descriptor_sha256)) != 0) {
        return GAMEPAD_ERR_UNSUPPORTED;
    }

    const gamepad_status_t status =
        apply_usb_gamepad_0079_0011_profile(layout);
    if (status == GAMEPAD_OK) {
        *applied_profile = GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011;
    }
    return status;
}

const char *gamepad_hid_profile_name(gamepad_hid_profile_t profile)
{
    switch (profile) {
    case GAMEPAD_HID_PROFILE_NONE:
        return "generic";
    case GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011:
        return "usb-gamepad-0079-0011";
    default:
        return "unknown";
    }
}

uint32_t gamepad_hid_capabilities(const gamepad_hid_layout_t *layout)
{
    if (!layout_valid(layout)) {
        return 0;
    }

    uint32_t capabilities = 0;
    for (uint16_t index = 0; index < layout->field_count; ++index) {
        switch ((gamepad_hid_map_target_t)layout->fields[index].map_target) {
        case GAMEPAD_HID_MAP_BUTTON:
            capabilities |= GAMEPAD_CAP_BUTTONS;
            break;
        case GAMEPAD_HID_MAP_HAT:
        case GAMEPAD_HID_MAP_DPAD_X:
        case GAMEPAD_HID_MAP_DPAD_Y:
            capabilities |= GAMEPAD_CAP_DPAD;
            break;
        case GAMEPAD_HID_MAP_LEFT_X:
        case GAMEPAD_HID_MAP_LEFT_Y:
            capabilities |= GAMEPAD_CAP_LEFT_STICK;
            break;
        case GAMEPAD_HID_MAP_RIGHT_X:
        case GAMEPAD_HID_MAP_RIGHT_Y:
            capabilities |= GAMEPAD_CAP_RIGHT_STICK;
            break;
        case GAMEPAD_HID_MAP_LEFT_TRIGGER:
            capabilities |= GAMEPAD_CAP_LEFT_TRIGGER;
            break;
        case GAMEPAD_HID_MAP_RIGHT_TRIGGER:
            capabilities |= GAMEPAD_CAP_RIGHT_TRIGGER;
            break;
        case GAMEPAD_HID_MAP_IGNORE:
        default:
            break;
        }
    }
    return capabilities;
}

static bool extract_bits(const uint8_t *payload,
                         size_t payload_size,
                         uint16_t bit_offset,
                         uint8_t bit_size,
                         uint32_t *value)
{
    if (bit_size == 0U || bit_size > 32U ||
        (uint64_t)bit_offset + bit_size > (uint64_t)payload_size * 8U) {
        return false;
    }

    uint32_t result = 0;
    for (uint8_t bit = 0; bit < bit_size; ++bit) {
        const uint32_t source_bit = (uint32_t)bit_offset + bit;
        const uint8_t source = payload[source_bit / 8U];
        if ((source & (uint8_t)(1U << (source_bit % 8U))) != 0U) {
            result |= UINT32_C(1) << bit;
        }
    }
    *value = result;
    return true;
}

static int64_t field_value(const gamepad_hid_field_t *field, uint32_t raw)
{
    if (field->logical_min < 0) {
        return sign_extend_bits(raw, field->bit_size);
    }
    return raw;
}

static int16_t normalize_axis(int64_t value, int32_t logical_min, int32_t logical_max)
{
    if (value < logical_min) {
        value = logical_min;
    } else if (value > logical_max) {
        value = logical_max;
    }
    const int64_t span = (int64_t)logical_max - logical_min;
    const int64_t position = value - logical_min;
    const int64_t scaled = INT16_MIN + (position * UINT16_MAX + span / 2) / span;
    return (int16_t)scaled;
}

static uint16_t normalize_trigger(int64_t value, int32_t logical_min, int32_t logical_max)
{
    if (value < logical_min) {
        value = logical_min;
    } else if (value > logical_max) {
        value = logical_max;
    }
    const int64_t span = (int64_t)logical_max - logical_min;
    const int64_t position = value - logical_min;
    return (uint16_t)((position * UINT16_MAX + span / 2) / span);
}

static gamepad_status_t decode_hat(const gamepad_hid_field_t *field,
                                   int64_t value,
                                   uint8_t *dpad)
{
    if (value < field->logical_min || value > field->logical_max) {
        if ((field->flags & GAMEPAD_HID_FIELD_NULL_STATE) != 0U) {
            *dpad = GAMEPAD_DPAD_CENTERED;
            return GAMEPAD_OK;
        }
        return GAMEPAD_ERR_MALFORMED;
    }

    const int64_t position_count = (int64_t)field->logical_max - field->logical_min + 1;
    const uint8_t position = (uint8_t)(value - field->logical_min);
    static const uint8_t eight_way[8] = {
        GAMEPAD_DPAD_UP,
        GAMEPAD_DPAD_UP | GAMEPAD_DPAD_RIGHT,
        GAMEPAD_DPAD_RIGHT,
        GAMEPAD_DPAD_RIGHT | GAMEPAD_DPAD_DOWN,
        GAMEPAD_DPAD_DOWN,
        GAMEPAD_DPAD_DOWN | GAMEPAD_DPAD_LEFT,
        GAMEPAD_DPAD_LEFT,
        GAMEPAD_DPAD_LEFT | GAMEPAD_DPAD_UP,
    };
    static const uint8_t four_way[4] = {
        GAMEPAD_DPAD_UP,
        GAMEPAD_DPAD_RIGHT,
        GAMEPAD_DPAD_DOWN,
        GAMEPAD_DPAD_LEFT,
    };

    if (position_count == 8) {
        *dpad = eight_way[position];
        return GAMEPAD_OK;
    }
    if (position_count == 4) {
        *dpad = four_way[position];
        return GAMEPAD_OK;
    }
    return GAMEPAD_ERR_UNSUPPORTED;
}

static gamepad_status_t decode_dpad_axis(const gamepad_hid_field_t *field,
                                         int64_t value,
                                         bool horizontal,
                                         uint8_t *dpad)
{
    if (field == NULL || dpad == NULL ||
        field->logical_max <= field->logical_min ||
        value < field->logical_min || value > field->logical_max) {
        return GAMEPAD_ERR_MALFORMED;
    }

    const int64_t span =
        (int64_t)field->logical_max - field->logical_min;
    const int64_t low = field->logical_min + span / 3;
    const int64_t high = field->logical_max - span / 3;
    const uint8_t negative = horizontal
        ? GAMEPAD_DPAD_LEFT : GAMEPAD_DPAD_UP;
    const uint8_t positive = horizontal
        ? GAMEPAD_DPAD_RIGHT : GAMEPAD_DPAD_DOWN;
    *dpad &= (uint8_t)~(negative | positive);
    if (value <= low) {
        *dpad |= negative;
    } else if (value >= high) {
        *dpad |= positive;
    }
    return GAMEPAD_OK;
}

static gamepad_status_t clear_array_buttons(const gamepad_hid_field_t *field,
                                            gamepad_state_t *state)
{
    if (field->usage_page != GAMEPAD_HID_USAGE_PAGE_BUTTON ||
        field->usage > field->usage_max) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    uint16_t usage = field->usage < 1U ? 1U : field->usage;
    const uint16_t maximum = field->usage_max < GAMEPAD_BUTTON_COUNT
                                 ? field->usage_max
                                 : GAMEPAD_BUTTON_COUNT;
    while (usage <= maximum) {
        state->buttons &= ~(UINT64_C(1) << (usage - 1U));
        usage++;
    }
    return GAMEPAD_OK;
}

static gamepad_status_t apply_array_button(const gamepad_hid_field_t *field,
                                           int64_t value,
                                           gamepad_state_t *state)
{
    if (field->usage_page != GAMEPAD_HID_USAGE_PAGE_BUTTON ||
        field->usage > field->usage_max) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    if (value < field->logical_min || value > field->logical_max) {
        return (field->flags & GAMEPAD_HID_FIELD_NULL_STATE) != 0U
                   ? GAMEPAD_OK
                   : GAMEPAD_ERR_MALFORMED;
    }

    if (!array_mapping_supported(field->logical_min, field->logical_max,
                                 field->usage, field->usage_max)) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    const int64_t logical_count =
        (int64_t)field->logical_max - field->logical_min + 1;
    const int64_t usage_count = (int64_t)field->usage_max - field->usage + 1;
    int64_t selected_usage = value;
    if (logical_count == usage_count) {
        selected_usage = (int64_t)field->usage + (value - field->logical_min);
    }
    if (selected_usage < field->usage || selected_usage > field->usage_max) {
        return (field->flags & GAMEPAD_HID_FIELD_NULL_STATE) != 0U
                   ? GAMEPAD_OK
                   : GAMEPAD_ERR_MALFORMED;
    }

    if (selected_usage >= 1 && selected_usage <= GAMEPAD_BUTTON_COUNT) {
        state->buttons |= UINT64_C(1) << ((uint32_t)selected_usage - 1U);
    }
    return GAMEPAD_OK;
}

static gamepad_status_t apply_field(const gamepad_hid_field_t *field,
                                    uint32_t raw,
                                    gamepad_state_t *state)
{
    if (field->bit_size == 0U || field->bit_size > 32U ||
        field->logical_max <= field->logical_min) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    const int64_t value = field_value(field, raw);
    if ((field->flags & GAMEPAD_HID_FIELD_ARRAY) != 0U) {
        if (field->map_target == GAMEPAD_HID_MAP_IGNORE) {
            return GAMEPAD_OK;
        }
        if (field->map_target != GAMEPAD_HID_MAP_BUTTON) {
            return GAMEPAD_ERR_INVALID_STATE;
        }
        return apply_array_button(field, value, state);
    }

    if (value < field->logical_min || value > field->logical_max) {
        if ((field->flags & GAMEPAD_HID_FIELD_NULL_STATE) == 0U) {
            return GAMEPAD_ERR_MALFORMED;
        }

        switch ((gamepad_hid_map_target_t)field->map_target) {
        case GAMEPAD_HID_MAP_LEFT_X:
            state->left_x = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_LEFT_Y:
            state->left_y = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_RIGHT_X:
            state->right_x = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_RIGHT_Y:
            state->right_y = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_LEFT_TRIGGER:
            state->left_trigger = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_RIGHT_TRIGGER:
            state->right_trigger = 0;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_HAT:
            state->dpad = GAMEPAD_DPAD_CENTERED;
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_DPAD_X:
            state->dpad &=
                (uint8_t)~(GAMEPAD_DPAD_LEFT | GAMEPAD_DPAD_RIGHT);
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_DPAD_Y:
            state->dpad &=
                (uint8_t)~(GAMEPAD_DPAD_UP | GAMEPAD_DPAD_DOWN);
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_BUTTON:
            if (field->map_index >= GAMEPAD_BUTTON_COUNT) {
                return GAMEPAD_ERR_INVALID_STATE;
            }
            state->buttons &= ~(UINT64_C(1) << field->map_index);
            return GAMEPAD_OK;
        case GAMEPAD_HID_MAP_IGNORE:
            return GAMEPAD_OK;
        default:
            return GAMEPAD_ERR_INVALID_STATE;
        }
    }

    switch ((gamepad_hid_map_target_t)field->map_target) {
    case GAMEPAD_HID_MAP_IGNORE:
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_LEFT_X:
        state->left_x = normalize_axis(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_LEFT_Y:
        state->left_y = normalize_axis(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_RIGHT_X:
        state->right_x = normalize_axis(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_RIGHT_Y:
        state->right_y = normalize_axis(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_LEFT_TRIGGER:
        state->left_trigger =
            normalize_trigger(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_RIGHT_TRIGGER:
        state->right_trigger =
            normalize_trigger(value, field->logical_min, field->logical_max);
        return GAMEPAD_OK;
    case GAMEPAD_HID_MAP_HAT:
        return decode_hat(field, value, &state->dpad);
    case GAMEPAD_HID_MAP_DPAD_X:
        return decode_dpad_axis(field, value, true, &state->dpad);
    case GAMEPAD_HID_MAP_DPAD_Y:
        return decode_dpad_axis(field, value, false, &state->dpad);
    case GAMEPAD_HID_MAP_BUTTON:
        if (field->map_index >= GAMEPAD_BUTTON_COUNT) {
            return GAMEPAD_ERR_INVALID_STATE;
        }
        if (value != 0) {
            state->buttons |= UINT64_C(1) << field->map_index;
        } else {
            state->buttons &= ~(UINT64_C(1) << field->map_index);
        }
        return GAMEPAD_OK;
    default:
        return GAMEPAD_ERR_INVALID_STATE;
    }
}

gamepad_status_t gamepad_hid_decode_report(const gamepad_hid_layout_t *layout,
                                           const uint8_t *report,
                                           size_t report_size,
                                           uint64_t timestamp_us,
                                           gamepad_state_t *state)
{
    if (report == NULL || state == NULL) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (!layout_valid(layout) || state->version != GAMEPAD_STATE_VERSION ||
        state->size != sizeof(*state) || state->connected > 1U) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    if (!state->connected) {
        return GAMEPAD_ERR_DISCONNECTED;
    }

    const size_t maximum_size =
        GAMEPAD_HID_MAX_REPORT_BYTES + (layout->uses_report_ids != 0U ? 1U : 0U);
    if (report_size > maximum_size) {
        return GAMEPAD_ERR_REPORT_TOO_LARGE;
    }

    uint8_t report_id = 0;
    const uint8_t *payload = report;
    size_t payload_size = report_size;
    if (layout->uses_report_ids != 0U) {
        if (report_size == 0U) {
            return GAMEPAD_ERR_REPORT_SIZE;
        }
        report_id = report[0];
        payload = &report[1];
        payload_size--;
    }

    const gamepad_hid_report_t *expected = find_const_report(layout, report_id);
    if (expected == NULL) {
        return GAMEPAD_ERR_REPORT_ID;
    }
    if (payload_size != expected->payload_bytes) {
        return GAMEPAD_ERR_REPORT_SIZE;
    }

    gamepad_state_t next = *state;
    for (uint16_t index = 0; index < layout->field_count; ++index) {
        const gamepad_hid_field_t *field = &layout->fields[index];
        if (field->report_id == report_id &&
            field->map_target == GAMEPAD_HID_MAP_BUTTON &&
            (field->flags & GAMEPAD_HID_FIELD_ARRAY) != 0U) {
            const gamepad_status_t status = clear_array_buttons(field, &next);
            if (status != GAMEPAD_OK) {
                return status;
            }
        }
    }

    for (uint16_t index = 0; index < layout->field_count; ++index) {
        const gamepad_hid_field_t *field = &layout->fields[index];
        if (field->report_id != report_id ||
            field->map_target == GAMEPAD_HID_MAP_IGNORE) {
            continue;
        }
        uint32_t raw = 0;
        if (!extract_bits(payload, payload_size, field->bit_offset, field->bit_size, &raw)) {
            return GAMEPAD_ERR_REPORT_SIZE;
        }
        const gamepad_status_t status = apply_field(field, raw, &next);
        if (status != GAMEPAD_OK) {
            return status;
        }
    }

    next.timestamp_us = timestamp_us;
    next.sequence++;
    *state = next;
    return GAMEPAD_OK;
}
