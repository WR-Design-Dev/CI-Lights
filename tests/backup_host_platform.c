/* In-memory hardware boundaries and OpenSSL-backed PSA operations for host tests. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "app_backup.h"
#include "app_config_store.h"
#include "app_branding.h"
#include "app_ota_storage.h"
#include "traffic_light.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "mbedtls/md.h"
#include "psa/crypto.h"

extern int RAND_bytes(unsigned char*,int);
extern unsigned char *SHA256(const unsigned char*,size_t,unsigned char*);
extern int EVP_EncodeBlock(unsigned char*,const unsigned char*,int);
extern int EVP_DecodeBlock(unsigned char*,const unsigned char*,int);
extern const void *EVP_sha256(void);
extern int PKCS5_PBKDF2_HMAC(const char*,int,const unsigned char*,int,int,const void*,int,unsigned char*);
extern void *EVP_CIPHER_CTX_new(void);
extern void EVP_CIPHER_CTX_free(void*);
extern const void *EVP_aes_256_gcm(void);
extern int EVP_CIPHER_CTX_ctrl(void*,int,int,void*);
extern int EVP_EncryptInit_ex(void*,const void*,void*,const unsigned char*,const unsigned char*);
extern int EVP_EncryptUpdate(void*,unsigned char*,int*,const unsigned char*,int);
extern int EVP_EncryptFinal_ex(void*,unsigned char*,int*);
extern int EVP_DecryptInit_ex(void*,const void*,void*,const unsigned char*,const unsigned char*);
extern int EVP_DecryptUpdate(void*,unsigned char*,int*,const unsigned char*,int);
extern int EVP_DecryptFinal_ex(void*,unsigned char*,int*);

typedef struct {int bank; char name[16]; void *data; size_t size;} entry_t;
static entry_t entries[256];
static unsigned char flash[0x20000], key_bytes[32];
static int fail_after, writes;
static app_light_settings_t runtime;
static int mutation(void) {++writes; return fail_after > 0 && --fail_after == 0 ? ESP_FAIL : ESP_OK;}
void test_fail_after(int value) {fail_after=value;}
int test_writes(void) {return writes;}
static entry_t *entry(int bank,const char *name,int create) {
    for(unsigned i=0;i<256;++i) if(entries[i].data && entries[i].bank==bank && !strcmp(entries[i].name,name))return &entries[i];
    if(create)for(unsigned i=0;i<256;++i)if(!entries[i].data){entries[i].bank=bank;strcpy(entries[i].name,name);return &entries[i];}
    return NULL;
}
int nvs_open(const char *name,int mode,nvs_handle_t *handle) {
    (void)mode; *handle=!strcmp(name,"settings")?1:!strcmp(name,"settings_b")?2:!strcmp(name,"cfg_meta")?3:0;
    assert(*handle); return ESP_OK;
}
void nvs_close(nvs_handle_t handle){assert(handle>0&&handle<4);}
int nvs_get_blob(nvs_handle_t bank,const char *name,void *data,size_t *size) {
    entry_t *item=entry(bank,name,0);if(!item)return ESP_ERR_NVS_NOT_FOUND;
    if(data && *size<item->size)return ESP_ERR_INVALID_SIZE;
    if(data)memcpy(data,item->data,item->size);*size=item->size;return ESP_OK;
}
int nvs_get_str(nvs_handle_t bank,const char *name,char *data,size_t *size){return nvs_get_blob(bank,name,data,size);}
int nvs_get_u8(nvs_handle_t b,const char *n,uint8_t *v){size_t size=1;return nvs_get_blob(b,n,v,&size);}
int nvs_get_u32(nvs_handle_t b,const char *n,uint32_t *v){size_t size=4;return nvs_get_blob(b,n,v,&size);}
int nvs_set_blob(nvs_handle_t bank,const char *name,const void *data,size_t size){
    if(mutation())return ESP_FAIL;entry_t *item=entry(bank,name,1);assert(item);
    free(item->data);item->data=malloc(size);assert(item->data);memcpy(item->data,data,size);item->size=size;return ESP_OK;
}
int nvs_set_str(nvs_handle_t b,const char *n,const char *v){return nvs_set_blob(b,n,v,strlen(v)+1);}
int nvs_set_u8(nvs_handle_t b,const char *n,uint8_t v){return nvs_set_blob(b,n,&v,1);}
int nvs_set_u32(nvs_handle_t b,const char *n,uint32_t v){return nvs_set_blob(b,n,&v,4);}
int nvs_erase_key(nvs_handle_t b,const char *n){if(mutation())return ESP_FAIL;entry_t *e=entry(b,n,0);if(!e)return ESP_ERR_NVS_NOT_FOUND;free(e->data);memset(e,0,sizeof(*e));return ESP_OK;}
int nvs_erase_all(nvs_handle_t b){if(mutation())return ESP_FAIL;for(unsigned i=0;i<256;++i)if(entries[i].data&&entries[i].bank==b){free(entries[i].data);memset(&entries[i],0,sizeof(entries[i]));}return ESP_OK;}
int nvs_commit(nvs_handle_t b){(void)b;return mutation();}
int nvs_flash_init(void){return ESP_OK;} int nvs_flash_erase(void){return ESP_OK;}
static const esp_partition_t partition={.size=sizeof(flash)};
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label){assert(type==1&&subtype==0x40&&!strcmp(label,"branding"));return &partition;}
int esp_partition_read(const esp_partition_t *p,size_t at,void *out,size_t size){assert(p==&partition&&at+size<=p->size);memcpy(out,flash+at,size);return ESP_OK;}
int esp_partition_write(const esp_partition_t *p,size_t at,const void *data,size_t size){assert(p==&partition&&at+size<=p->size&&at%16==0&&size%16==0);if(mutation())return ESP_FAIL;memcpy(flash+at,data,size);return ESP_OK;}
int esp_partition_erase_range(const esp_partition_t *p,size_t at,size_t size){assert(p==&partition&&at+size<=p->size);if(mutation())return ESP_FAIL;memset(flash+at,255,size);return ESP_OK;}
static int64_t now_us;
static unsigned derivations;
static esp_timer_create_args_t expiry_timer;
int64_t esp_timer_get_time(void){return now_us;}
int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *timer){expiry_timer=*args;*timer=&expiry_timer;return ESP_OK;}
int esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t period){assert(timer==&expiry_timer&&period==1000000);return ESP_OK;}
int esp_timer_delete(esp_timer_handle_t timer){assert(timer==&expiry_timer);memset(&expiry_timer,0,sizeof(expiry_timer));return ESP_OK;}
void test_advance_time(int seconds,int fire_timer){now_us+=(int64_t)seconds*1000000;if(fire_timer&&expiry_timer.callback)expiry_timer.callback(expiry_timer.arg);}
unsigned test_derivations(void){return derivations;}
void esp_fill_random(void *out,size_t size){assert(RAND_bytes(out,size)==1);}
void mbedtls_platform_zeroize(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
const mbedtls_md_info_t *mbedtls_md_info_from_type(int type){static const int info=1;assert(type==1);return &info;}
int mbedtls_md(const mbedtls_md_info_t *info,const unsigned char *in,size_t n,unsigned char *out){assert(info);return SHA256(in,n,out)?0:-1;}
int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *written,const unsigned char *in,size_t n){if(cap<4*((n+2)/3)+1)return -1;int size=EVP_EncodeBlock(out,in,n);*written=size;return size<0?-1:0;}
int mbedtls_base64_decode(unsigned char *out,size_t cap,size_t *written,const unsigned char *in,size_t n){
    if(n%4)return -1;unsigned char *tmp=malloc(n+1);int size=EVP_DecodeBlock(tmp,in,n);
    if(n&&in[n-1]=='=')--size;if(n>1&&in[n-2]=='=')--size;
    int err=size<0||(size_t)size>cap?-1:0;if(!err){memcpy(out,tmp,size);*written=size;}free(tmp);return err;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return calloc(1,sizeof(int));}
void xSemaphoreGive(SemaphoreHandle_t s){*(int*)s=1;}
void xSemaphoreTake(SemaphoreHandle_t s,int ticks){(void)ticks;assert(*(int*)s==1);}
void vSemaphoreDelete(SemaphoreHandle_t s){free(s);}
int xTaskCreate(void(*fn)(void*),const char *name,int stack,void *arg,int priority,void *task){(void)name;(void)stack;(void)task;assert(priority==0);fn(arg);return 1;}
void vTaskDelete(void *task){assert(!task);}
int psa_crypto_init(void){return 0;}
int psa_key_derivation_setup(psa_key_derivation_operation_t *o,int alg){assert(alg==101);memset(o,0,sizeof(*o));return 0;}
int psa_key_derivation_input_integer(psa_key_derivation_operation_t *o,int step,uint64_t cost){assert(step==1&&cost==600000);o->cost=cost;return 0;}
int psa_key_derivation_input_bytes(psa_key_derivation_operation_t *o,int step,const unsigned char *in,size_t n){if(step==2){assert(n==16);memcpy(o->salt,in,n);}else{assert(step==3&&n<=128);memcpy(o->password,in,n);o->password[n]=0;}return 0;}
int psa_key_derivation_output_bytes(psa_key_derivation_operation_t *o,unsigned char *out,size_t n){assert(n==32);++derivations;return PKCS5_PBKDF2_HMAC(o->password,strlen(o->password),o->salt,16,o->cost,EVP_sha256(),n,out)==1?0:-1;}
int psa_key_derivation_abort(psa_key_derivation_operation_t *o){mbedtls_platform_zeroize(o,sizeof(*o));return 0;}
int psa_import_key(const psa_key_attributes_t *a,const unsigned char *in,size_t n,mbedtls_svc_key_id_t *key){assert(a->type==4&&a->bits==256&&a->usage==3&&a->algorithm==5&&n==32);memcpy(key_bytes,in,n);*key=1;return 0;}
int psa_destroy_key(mbedtls_svc_key_id_t key){(void)key;memset(key_bytes,0,32);return 0;}
int psa_aead_encrypt(mbedtls_svc_key_id_t key,int alg,const unsigned char *nonce,size_t nonce_n,const unsigned char *aad,size_t aad_n,const unsigned char *in,size_t n,unsigned char *out,size_t cap,size_t *written){
    assert(key==1&&alg==5&&nonce_n==12&&cap>=n+16);void *ctx=EVP_CIPHER_CTX_new();int got,total;
    assert(EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key_bytes,nonce)==1);
    assert(EVP_EncryptUpdate(ctx,NULL,&got,aad,aad_n)==1);assert(EVP_EncryptUpdate(ctx,out,&got,in,n)==1);total=got;
    assert(EVP_EncryptFinal_ex(ctx,out+total,&got)==1);total+=got;
    assert(EVP_CIPHER_CTX_ctrl(ctx,0x10,16,out+total)==1);*written=total+16;EVP_CIPHER_CTX_free(ctx);return 0;
}
int psa_aead_decrypt(mbedtls_svc_key_id_t key,int alg,const unsigned char *nonce,size_t nonce_n,const unsigned char *aad,size_t aad_n,const unsigned char *in,size_t n,unsigned char *out,size_t cap,size_t *written){
    assert(key==1&&alg==5&&nonce_n==12&&n>=16&&cap>=n-16);void *ctx=EVP_CIPHER_CTX_new();int got,total;
    assert(EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,key_bytes,nonce)==1);
    assert(EVP_DecryptUpdate(ctx,NULL,&got,aad,aad_n)==1);assert(EVP_DecryptUpdate(ctx,out,&got,in,n-16)==1);total=got;
    assert(EVP_CIPHER_CTX_ctrl(ctx,0x11,16,(void*)(in+n-16))==1);int valid=EVP_DecryptFinal_ex(ctx,out+total,&got);*written=total+got;
    EVP_CIPHER_CTX_free(ctx);return valid==1?0:PSA_ERROR_INVALID_SIGNATURE;
}
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t d={.version="1.0.1019"};return &d;}
app_control_mode_t traffic_light_control_mode(void){return runtime.mode;}
app_disco_effect_t traffic_light_disco_effect(void){return runtime.disco_effect;}
bool traffic_light_manual_state(app_manual_light_t i){return runtime.enabled[i];}
void traffic_light_manual_color(app_manual_light_t i,uint8_t *r,uint8_t *g,uint8_t *b){*r=runtime.colors[i][0];*g=runtime.colors[i][1];*b=runtime.colors[i][2];}

void test_seed(int variant){
    app_backup_discard_prepared();now_us=0;derivations=0;
    fail_after=0;for(unsigned i=0;i<256;++i){free(entries[i].data);memset(&entries[i],0,sizeof(entries[i]));}memset(flash,255,sizeof(flash));app_storage_init();
    assert(app_admin_credentials_save(variant?"source-admin":"target-admin",variant?"source-password":"target-password")==0);
    app_config_t config={.wifi_auth=APP_WIFI_AUTH_PERSONAL,.jenkins_poll_interval_minutes=7,.jenkins_job_list_timeout_seconds=120,.jenkins_status_timeout_seconds=90,.light_brightness_percent=83,.build_effect=APP_BUILD_EFFECT_BLINK,.cpu_mode=APP_CPU_MODE_AUTO_240};
    strcpy(config.wifi_ssid,variant?"source-wifi":"target-wifi");strcpy(config.wifi_password,"wifi-password");assert(!app_config_save_wifi(&config));
    strcpy(config.jenkins_url,"https://jenkins.example");strcpy(config.jenkins_user,"jenkins-admin");strcpy(config.jenkins_token,"private-test-token");strcpy(config.jenkins_job_path,"/job/folder/job/build/");assert(!app_config_save_jenkins(&config));
    assert(!app_config_save_jenkins_poll_settings(7,120,90));assert(!app_config_save_light_brightness(83));assert(!app_config_save_build_effect(config.build_effect));assert(!app_config_save_cpu_mode(config.cpu_mode));
    assert(!app_config_save_site_title(variant?"Source title":"Target title"));assert(!app_config_save_hostname(variant?"source-device":"target-device"));assert(!app_config_save_language(variant?"en":"de"));
    app_light_settings_defaults(&runtime);runtime.mode=APP_CONTROL_MODE_MANUAL;runtime.disco_effect=APP_DISCO_EFFECT_FIREWORKS;runtime.colors[0][2]=117;runtime.enabled[0]=true;assert(!app_light_settings_save(&runtime));
    app_ota_settings_t ota;app_ota_history_t history;assert(!app_ota_storage_load(&ota,&history));ota.mode=APP_OTA_AUTO_INSTALL;strcpy(ota.timezone,"Europe/Berlin");ota.install_minute=185;strcpy(history.blocked_version,variant?"1.0.1005":"1.0.1006");assert(!app_ota_storage_save_settings(&ota,&history));
    const char *banner=variant?"<svg>source</svg>":"<svg>target</svg>";assert(!app_branding_save(APP_BRANDING_LOGO,APP_BRANDING_SVG,banner,strlen(banner)));
    unsigned char icon[]={0,0,1,0,1,0,7,8};assert(!app_branding_save(APP_BRANDING_FAVICON,APP_BRANDING_ICO,icon,sizeof(icon)));writes=0;
}
void test_boot(void){app_backup_discard_prepared();app_storage_init();assert(!app_light_settings_load(&runtime));}
void test_free(void *p){free(p);}
char *test_export(const char *password){cJSON *out=NULL;assert(!app_backup_export(password,&out));char *text=cJSON_PrintUnformatted(out);cJSON_Delete(out);return text;}
int test_import(const char *password,const char *file){cJSON *json=app_backup_json_parse(file);if(!json)return ESP_ERR_INVALID_ARG;int err=app_backup_import(password,json);cJSON_Delete(json);return err;}
int test_import_prepared(const char *import_id){return app_backup_import_prepared(import_id);}
char *test_validate(const char *password,const char *file){cJSON *json=app_backup_json_parse(file),*summary=NULL;int err=app_backup_validate(password,json,&summary);cJSON_Delete(json);if(err)return NULL;char *text=cJSON_PrintUnformatted(summary);cJSON_Delete(summary);return text;}
char *test_state(void){app_settings_snapshot_t s;assert(!app_settings_snapshot_load(&s));cJSON *json=app_backup_snapshot_json(&s);assert(json);cJSON_AddStringToObject(json,"blocked",s.ota_history.blocked_version);char banner[128]={0};app_branding_info_t info;if(app_branding_get_info(0,&info)&&info.length<sizeof(banner))assert(!app_branding_read(0,banner,info.length));cJSON_AddStringToObject(json,"banner",banner);char *text=cJSON_PrintUnformatted(json);app_backup_json_delete(json);return text;}
int test_reset(void){return app_config_factory_reset();}
int test_config_present(void){app_config_t config;return app_config_load(&config);}
char *test_open_file(const char *password,const char *file){cJSON *json=app_backup_json_parse(file);char *plain=NULL;int err=app_backup_open(password,json,&plain);cJSON_Delete(json);return err?NULL:plain;}
char *test_seal_file(const char *password,const char *plain){cJSON *json=NULL;assert(!app_backup_seal(password,plain,&json));char *text=cJSON_PrintUnformatted(json);cJSON_Delete(json);return text;}
void test_clear(void){fail_after=0;for(unsigned i=0;i<256;++i){free(entries[i].data);memset(&entries[i],0,sizeof(entries[i]));}memset(flash,255,sizeof(flash));app_storage_init();writes=0;}
void test_select_bank(unsigned char bank){assert(!nvs_set_u8(3,"active",bank));test_boot();}
char *test_banner(void){app_branding_info_t info;size_t size=app_branding_get_info(0,&info)?info.length:0;char *text=calloc(1,size+1);if(size)assert(!app_branding_read(0,text,size));return text;}
int test_valid_json(const char *text){cJSON *json=app_backup_json_parse(text);int valid=json!=NULL;cJSON_Delete(json);return valid;}
void test_expand(void){
    app_settings_snapshot_t s;assert(!app_settings_snapshot_load(&s));s.wifi.count=8;s.wifi.last_index=3;
    for(unsigned i=0;i<8;++i){snprintf(s.wifi.entries[i].ssid,sizeof(s.wifi.entries[i].ssid),"network-%u",i);memset(s.wifi.entries[i].username,'u',127);s.wifi.entries[i].username[127]=0;memset(s.wifi.entries[i].password,'p',63);s.wifi.entries[i].password[63]=0;s.wifi.entries[i].auth=APP_WIFI_AUTH_ENTERPRISE;}
    unsigned char *asset=malloc(APP_BRANDING_MAX_BYTES);assert(asset);memset(asset,'a',APP_BRANDING_MAX_BYTES);memcpy(asset,"<svg>",5);memcpy(asset+APP_BRANDING_MAX_BYTES-6,"</svg>",6);
    assert(!app_branding_pin_active());assert(!app_branding_stage(0,APP_BRANDING_SVG,asset,APP_BRANDING_MAX_BYTES,&s.branding_slots[0]));
    const unsigned char ico[]={0,0,1,0,1,0};memcpy(asset,ico,6);assert(!app_branding_stage(1,APP_BRANDING_ICO,asset,APP_BRANDING_MAX_BYTES,&s.branding_slots[1]));free(asset);
    assert(!app_settings_snapshot_stage(&s));assert(!app_config_store_activate());test_boot();
}
