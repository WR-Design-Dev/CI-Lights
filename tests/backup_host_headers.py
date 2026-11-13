"""SDK boundary stubs; production backup/storage code remains unchanged in tests."""
HEADERS = {
'esp_err.h': r'''
#pragma once
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_ERR_NVS_NOT_FOUND 7
#define ESP_ERR_NVS_NO_FREE_PAGES 8
#define ESP_ERR_NVS_NEW_VERSION_FOUND 9
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
''',
'sdkconfig.h': '#pragma once\n#define CONFIG_APP_SECURITY_SECURE 0\n',
'esp_http_server.h': '#pragma once\ntypedef struct httpd_req httpd_req_t;\n',
'esp_log.h': '#pragma once\n#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n',
'esp_random.h': '#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void*,size_t);\n',
'esp_timer.h': r'''
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct {void (*callback)(void*); void *arg; const char *name;} esp_timer_create_args_t;
typedef void *esp_timer_handle_t;
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t,uint64_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
''',
'esp_app_desc.h': '#pragma once\ntypedef struct {char version[32];} esp_app_desc_t;\nconst esp_app_desc_t *esp_app_get_description(void);\n',
'nvs.h': r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
esp_err_t nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
esp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
esp_err_t nvs_get_u32(nvs_handle_t,const char*,uint32_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
esp_err_t nvs_set_str(nvs_handle_t,const char*,const char*);
esp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t);
esp_err_t nvs_set_u32(nvs_handle_t,const char*,uint32_t);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_commit(nvs_handle_t);
''',
'nvs_flash.h': '#pragma once\n#include "nvs.h"\nesp_err_t nvs_flash_init(void);\nesp_err_t nvs_flash_erase(void);\n',
'esp_partition.h': r'''
#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef int esp_partition_subtype_t;
typedef struct {size_t size;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
const esp_partition_t *esp_partition_find_first(int,int,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
''',
'freertos/FreeRTOS.h': '#pragma once\n#define pdPASS 1\n#define portMAX_DELAY -1\n#define tskIDLE_PRIORITY 0\ntypedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(m) ((void)(m))\n#define portEXIT_CRITICAL(m) ((void)(m))\n',
'freertos/task.h': '#pragma once\nint xTaskCreate(void(*)(void*),const char*,int,void*,int,void*);\nvoid vTaskDelete(void*);\n',
'freertos/semphr.h': '#pragma once\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateBinary(void);\nvoid xSemaphoreGive(SemaphoreHandle_t);\nvoid xSemaphoreTake(SemaphoreHandle_t,int);\nvoid vSemaphoreDelete(SemaphoreHandle_t);\n',
'mbedtls/platform_util.h': '#pragma once\n#include <stddef.h>\nvoid mbedtls_platform_zeroize(void*,size_t);\n',
'mbedtls/md.h': '#pragma once\n#include <stddef.h>\ntypedef int mbedtls_md_info_t;\n#define MBEDTLS_MD_SHA256 1\nconst mbedtls_md_info_t *mbedtls_md_info_from_type(int);\nint mbedtls_md(const mbedtls_md_info_t*,const unsigned char*,size_t,unsigned char*);\n',
'mbedtls/base64.h': '#pragma once\n#include <stddef.h>\nint mbedtls_base64_encode(unsigned char*,size_t,size_t*,const unsigned char*,size_t);\nint mbedtls_base64_decode(unsigned char*,size_t,size_t*,const unsigned char*,size_t);\n',
'psa/crypto.h': r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int psa_status_t;
typedef unsigned mbedtls_svc_key_id_t;
typedef struct {unsigned char salt[16]; char password[129]; unsigned cost;} psa_key_derivation_operation_t;
typedef struct {int type,bits,usage,algorithm;} psa_key_attributes_t;
#define PSA_KEY_DERIVATION_OPERATION_INIT {0}
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define MBEDTLS_SVC_KEY_ID_INIT 0
#define PSA_SUCCESS 0
#define PSA_ERROR_INVALID_SIGNATURE -149
#define PSA_ALG_SHA_256 1
#define PSA_ALG_PBKDF2_HMAC(x) (100+(x))
#define PSA_KEY_DERIVATION_INPUT_COST 1
#define PSA_KEY_DERIVATION_INPUT_SALT 2
#define PSA_KEY_DERIVATION_INPUT_PASSWORD 3
#define PSA_KEY_TYPE_AES 4
#define PSA_KEY_USAGE_ENCRYPT 1
#define PSA_KEY_USAGE_DECRYPT 2
#define PSA_ALG_GCM 5
static inline void psa_set_key_type(psa_key_attributes_t *a,int v){a->type=v;}
static inline void psa_set_key_bits(psa_key_attributes_t *a,int v){a->bits=v;}
static inline void psa_set_key_usage_flags(psa_key_attributes_t *a,int v){a->usage=v;}
static inline void psa_set_key_algorithm(psa_key_attributes_t *a,int v){a->algorithm=v;}
static inline void psa_reset_key_attributes(psa_key_attributes_t *a){(void)a;}
int psa_crypto_init(void);
int psa_key_derivation_setup(psa_key_derivation_operation_t*,int);
int psa_key_derivation_input_integer(psa_key_derivation_operation_t*,int,uint64_t);
int psa_key_derivation_input_bytes(psa_key_derivation_operation_t*,int,const unsigned char*,size_t);
int psa_key_derivation_output_bytes(psa_key_derivation_operation_t*,unsigned char*,size_t);
int psa_key_derivation_abort(psa_key_derivation_operation_t*);
int psa_import_key(const psa_key_attributes_t*,const unsigned char*,size_t,mbedtls_svc_key_id_t*);
int psa_destroy_key(mbedtls_svc_key_id_t);
int psa_aead_encrypt(mbedtls_svc_key_id_t,int,const unsigned char*,size_t,const unsigned char*,size_t,const unsigned char*,size_t,unsigned char*,size_t,size_t*);
int psa_aead_decrypt(mbedtls_svc_key_id_t,int,const unsigned char*,size_t,const unsigned char*,size_t,const unsigned char*,size_t,unsigned char*,size_t,size_t*);
''',
}
