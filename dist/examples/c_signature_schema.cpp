#include "axllm/axllm_c.h"

#include <cstring>
#include <iostream>

int main() {
  char* json = nullptr;
  axllm_c_error* error = nullptr;
  axllm_c_status status = axllm_c_schema_for_signature(
    "question:string -> answer:string",
    "outputs",
    &json,
    &error
  );
  if (status != AXLLM_C_OK) {
    std::cerr << axllm_c_error_message(error) << "\n";
    axllm_c_error_free(error);
    return 1;
  }
  bool ok = std::strstr(json, "answer") != nullptr;
  axllm_c_string_free(json);
  if (!ok) return 1;
  std::cout << "cpp-c-abi-signature-schema-ok\n";
  return 0;
}
