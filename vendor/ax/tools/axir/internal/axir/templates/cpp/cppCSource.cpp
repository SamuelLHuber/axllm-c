#include "axllm_c.h"
#include "axllm.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>

struct axllm_c_error {
  std::string message;
};

namespace {

char* axllm_c_dup(const std::string& value) {
  char* out = static_cast<char*>(std::malloc(value.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, value.c_str(), value.size() + 1);
  return out;
}

axllm_c_status axllm_c_fail(const std::string& message, axllm_c_error** out_error) {
  if (out_error != nullptr) {
    try {
      *out_error = new axllm_c_error{message};
    } catch (...) {
      *out_error = nullptr;
    }
  }
  return AXLLM_C_ERR;
}

}  // namespace

const char* axllm_c_version(void) {
  return "__VERSION__";
}

axllm_c_status axllm_c_schema_for_signature(
  const char* signature,
  const char* section,
  char** out_json,
  axllm_c_error** out_error
) {
  if (out_json == nullptr) return axllm_c_fail("out_json must not be null", out_error);
  *out_json = nullptr;
  if (out_error != nullptr) *out_error = nullptr;
  if (signature == nullptr || signature[0] == '\0') {
    return axllm_c_fail("signature must not be empty", out_error);
  }

  try {
    const char* schema_section = (section == nullptr || section[0] == '\0') ? "outputs" : section;
    axllm::Value sig = axllm::s(signature);
    axllm::Value fields = axllm::Core::get(sig, schema_section);
    axllm::Value schema = axllm::to_json_schema(fields);
    axllm::Value json_value = axllm::Core::json_stringify(schema);
    std::string json = std::get<std::string>(json_value.data);
    *out_json = axllm_c_dup(json);
    if (*out_json == nullptr) return axllm_c_fail("failed to allocate output JSON", out_error);
    return AXLLM_C_OK;
  } catch (const std::exception& error) {
    return axllm_c_fail(error.what(), out_error);
  } catch (...) {
    return axllm_c_fail("unknown axllm error", out_error);
  }
}

void axllm_c_string_free(char* value) {
  std::free(value);
}

const char* axllm_c_error_message(const axllm_c_error* error) {
  if (error == nullptr) return "";
  return error->message.c_str();
}

void axllm_c_error_free(axllm_c_error* error) {
  delete error;
}
