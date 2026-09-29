#include "app_branding.h"

#include <string.h>

#include "esp_partition.h"

#define BRANDING_PARTITION_SIZE 0x20000
#define BRANDING_SLOT_SIZE 0x8000
#define BRANDING_MAGIC 0x424E4431u
#define BRANDING_SUBTYPE 0x40

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t length;
    uint32_t crc32;
    uint32_t format;
} branding_header_t;

_Static_assert(sizeof(branding_header_t) == 20, "Branding header size changed");

typedef struct {
    branding_header_t header;
    size_t offset;
    bool found;
} selected_slot_t;

static const esp_partition_t *branding_partition(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t) BRANDING_SUBTYPE, "branding");
    return partition != NULL && partition->size == BRANDING_PARTITION_SIZE ? partition : NULL;
}

static uint32_t update_crc(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return crc;
}

static bool allowed_format(app_branding_kind_t kind, uint32_t format)
{
    return kind == APP_BRANDING_LOGO ?
        (format == APP_BRANDING_SVG || format == APP_BRANDING_PNG) :
        kind == APP_BRANDING_FAVICON &&
        (format == APP_BRANDING_ICO || format == APP_BRANDING_PNG);
}

static bool read_valid_slot(const esp_partition_t *partition, size_t offset,
                            app_branding_kind_t kind, branding_header_t *header)
{
    if (esp_partition_read(partition, offset, header, sizeof(*header)) != ESP_OK ||
        header->magic != BRANDING_MAGIC || header->generation == 0 ||
        header->length == 0 || header->length > APP_BRANDING_MAX_BYTES ||
        !allowed_format(kind, header->format)) {
        return false;
    }
    uint8_t chunk[512];
    size_t remaining = header->length;
    size_t position = offset + sizeof(*header);
    uint32_t crc = 0xffffffffu;
    while (remaining > 0) {
        size_t count = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (esp_partition_read(partition, position, chunk, count) != ESP_OK) {
            return false;
        }
        crc = update_crc(crc, chunk, count);
        remaining -= count;
        position += count;
    }
    return (crc ^ 0xffffffffu) == header->crc32;
}

static selected_slot_t select_slot(const esp_partition_t *partition,
                                    app_branding_kind_t kind)
{
    selected_slot_t selected = {0};
    if (partition == NULL || (kind != APP_BRANDING_LOGO && kind != APP_BRANDING_FAVICON)) {
        return selected;
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        size_t offset = ((size_t) kind * 2 + slot) * BRANDING_SLOT_SIZE;
        branding_header_t header;
        if (read_valid_slot(partition, offset, kind, &header) &&
            (!selected.found || (int32_t) (header.generation - selected.header.generation) > 0)) {
            selected.header = header;
            selected.offset = offset;
            selected.found = true;
        }
    }
    return selected;
}

bool app_branding_get_info(app_branding_kind_t kind, app_branding_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    selected_slot_t slot = select_slot(branding_partition(), kind);
    if (!slot.found) {
        return false;
    }
    info->length = slot.header.length;
    info->format = (app_branding_format_t) slot.header.format;
    return true;
}

esp_err_t app_branding_read(app_branding_kind_t kind, void *buffer, size_t length)
{
    const esp_partition_t *partition = branding_partition();
    selected_slot_t slot = select_slot(partition, kind);
    if (!slot.found) {
        return ESP_ERR_NOT_FOUND;
    }
    if (buffer == NULL || length != slot.header.length) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_partition_read(partition, slot.offset + sizeof(slot.header), buffer, length);
}

esp_err_t app_branding_save(app_branding_kind_t kind, app_branding_format_t format,
                            const void *data, size_t length)
{
    if (data == NULL || length == 0 || length > APP_BRANDING_MAX_BYTES ||
        !allowed_format(kind, format)) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *partition = branding_partition();
    if (partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    selected_slot_t previous = select_slot(partition, kind);
    size_t first_offset = (size_t) kind * 2 * BRANDING_SLOT_SIZE;
    size_t target = previous.found && previous.offset == first_offset ?
                    first_offset + BRANDING_SLOT_SIZE : first_offset;
    esp_err_t err = esp_partition_erase_range(partition, target, BRANDING_SLOT_SIZE);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_partition_write(partition, target + sizeof(branding_header_t), data, length);
    if (err != ESP_OK) {
        return err;
    }
    branding_header_t header = {
        .magic = BRANDING_MAGIC,
        .generation = previous.found ? previous.header.generation + 1u : 1u,
        .length = length,
        .crc32 = update_crc(0xffffffffu, data, length) ^ 0xffffffffu,
        .format = format,
    };
    if (header.generation == 0) {
        header.generation = 1;
    }
    err = esp_partition_write(partition, target, &header, sizeof(header));
    if (err != ESP_OK) {
        return err;
    }
    branding_header_t check;
    return read_valid_slot(partition, target, kind, &check) ? ESP_OK : ESP_FAIL;
}
