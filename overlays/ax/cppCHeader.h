#ifndef AXLLM_C_H
#define AXLLM_C_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum axllm_c_status {
  AXLLM_C_OK = 0,
  AXLLM_C_ERR = 1
} axllm_c_status;

typedef struct axllm_c_error axllm_c_error;

const char* axllm_c_version(void);

axllm_c_status axllm_c_schema_for_signature(
  const char* signature,
  const char* section,
  char** out_json,
  axllm_c_error** out_error
);

void axllm_c_string_free(char* value);
const char* axllm_c_error_message(const axllm_c_error* error);
void axllm_c_error_free(axllm_c_error* error);

#ifdef __cplusplus
}
#endif

#endif
