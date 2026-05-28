#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include "snapshot.h"
#include "config.h"
#include "yyjson.h"

/* Convert a yyjson mutable doc to a C string (caller must free).
   root is the top-level JSON value and will be set as the doc root. */
char *json_doc_to_string(yyjson_mut_doc *doc, yyjson_mut_val *root);

/* Build a complete success response: {"ok":true,"data":<data>,"meta":{...}}
   The `data` val must be created with yyjson_mut_obj() or yyjson_mut_arr(). */
yyjson_mut_val *json_build_success(yyjson_mut_doc *doc, yyjson_mut_val *data,
                                   uint64_t sequence, int64_t sampled_at_ms,
                                   uint32_t sample_interval_ms);

/* Build a complete error response: {"ok":false,"error":{...}} */
yyjson_mut_val *json_build_error(yyjson_mut_doc *doc, const char *code, const char *message);

/* Data serializers - each returns a yyjson_mut_val* (obj or arr) */
yyjson_mut_val *json_write_config(yyjson_mut_doc *doc, const config_t *cfg);
yyjson_mut_val *json_write_system_summary(yyjson_mut_doc *doc, const system_summary_t *s);
yyjson_mut_val *json_write_process_list(yyjson_mut_doc *doc, const process_list_t *pl);
yyjson_mut_val *json_write_process_info(yyjson_mut_doc *doc, const process_info_t *p);
yyjson_mut_val *json_write_gpu_list(yyjson_mut_doc *doc, const gpu_list_t *gl);
yyjson_mut_val *json_write_gpu_info(yyjson_mut_doc *doc, const gpu_info_t *gi);
yyjson_mut_val *json_write_gpu_process_list(yyjson_mut_doc *doc, const gpu_process_list_t *gpl);
yyjson_mut_val *json_write_probe_status(yyjson_mut_doc *doc, const probe_status_info_t *ps);

#endif
