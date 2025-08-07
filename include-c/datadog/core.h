#ifndef __DATADOG_CORE_H__
#define __DATADOG_CORE_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DD_TRACKING_CONSENT_GRANTED,
    DD_TRACKING_CONSENT_NOT_GRANTED,
    DD_TRACKING_CONSENT_PENDING,
} dd_tracking_consent_t;

typedef enum
{
    DD_SITE_US1,
    DD_SITE_US3,
    DD_SITE_US5,
    DD_SITE_EU1,
    DD_SITE_AP1,
    DD_SITE_AP2,
    DD_SITE_US1_FED,
} dd_site_t;

typedef enum
{
    DD_BATCH_SIZE_SMALL,
    DD_BATCH_SIZE_MEDIUM,
    DD_BATCH_SIZE_LARGE,
} dd_batch_size_t;

typedef enum
{
    DD_UPLOAD_FREQUENCY_FREQUENT,
    DD_UPLOAD_FREQUENCY_AVERAGE,
    DD_UPLOAD_FREQUENCY_RARE,
} dd_upload_frequency_t;

typedef enum
{
    DD_BATCH_PROCESSING_LEVEL_LOW,
    DD_BATCH_PROCESSING_LEVEL_MEDIUM,
    DD_BATCH_PROCESSING_LEVEL_HIGH,
} dd_batch_processing_level_t;

typedef struct dd_core_config
{
    dd_tracking_consent_t tracking_consent;
    dd_site_t datadog_site;
    const char* client_token;
    const char* service;
    const char* env;
    const char* application_version;
    dd_batch_size_t batch_size;
    dd_upload_frequency_t upload_frequency;
    dd_batch_processing_level_t batch_processing_level;
} dd_core_config_t;

typedef struct dd_core dd_core_t;

dd_core_t* dd_core_create(dd_core_config_t* config);
void dd_core_destroy(dd_core_t* core);

bool dd_core_start(dd_core_t* core);
void dd_core_shutdown(dd_core_t* core);

#ifdef __cplusplus
}
#endif

#endif // __DATADOG_CORE_H__
