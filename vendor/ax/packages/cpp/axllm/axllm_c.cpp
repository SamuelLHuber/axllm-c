#include "axllm_c.h"
#include "axllm.hpp"
#include "mcp.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using axllm::Core;
using axllm::Value;

struct axllm_c_error {
  std::string message;
};

struct axllm_c_ai {
  std::shared_ptr<axllm::AIClient> client;
  std::shared_ptr<axllm::AxAIService> service;
};

struct axllm_c_program {
  axllm_c_program_kind kind;
  std::shared_ptr<axllm::AxProgram> program;
  std::vector<std::shared_ptr<void>> keep;
};

struct axllm_c_tool {
  axllm::Tool tool;
};

struct axllm_c_cancel {
  axllm::AxCancellationToken token;
};

struct axllm_c_run_control {
  axllm::AxRunControl control;
};

struct axllm_c_optimizer {
  std::unique_ptr<axllm::OptimizerEngine> engine;
  std::shared_ptr<axllm::AIClient> reflection;
};

struct axllm_c_playbook {
  std::unique_ptr<axllm::AxPlaybook> owned;
  axllm::AxPlaybook* playbook;
  std::vector<std::shared_ptr<void>> keep;
};

struct axllm_c_mcp_client {
  std::shared_ptr<axllm::AxMCPClient> client;
};

namespace {

char* dup(const std::string& value) {
  char* out = static_cast<char*>(std::malloc(value.size() + 1));
  if (out == nullptr) throw std::bad_alloc();
  std::memcpy(out, value.c_str(), value.size() + 1);
  return out;
}

axllm_c_status fail(const std::string& message, axllm_c_error** out_error) {
  if (out_error != nullptr) {
    try {
      *out_error = new axllm_c_error{message};
    } catch (...) {
      *out_error = nullptr;
    }
  }
  return AXLLM_C_ERR;
}

template <typename F>
axllm_c_status guard(axllm_c_error** out_error, F&& body) {
  if (out_error != nullptr) *out_error = nullptr;
  try {
    body();
    return AXLLM_C_OK;
  } catch (const std::exception& error) {
    return fail(error.what(), out_error);
  } catch (...) {
    return fail("unknown axllm error", out_error);
  }
}

void require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

std::string text(const char* value, const char* name) {
  if (value == nullptr) throw std::invalid_argument(std::string(name) + " must not be null");
  return value;
}

std::string text_or(const char* value, const char* fallback) {
  return (value == nullptr || value[0] == '\0') ? fallback : value;
}

Value json_in(const char* json, Value fallback = Value()) {
  if (json == nullptr || json[0] == '\0') return fallback;
  return axllm::parse_json(json);
}

Value json_object(const char* json) { return json_in(json, Value::object()); }

void json_out(char** out_json, const Value& value) {
  require(out_json != nullptr, "out_json must not be null");
  *out_json = dup(axllm::stringify(value));
}

void text_out(char** out_text, const std::string& value) {
  require(out_text != nullptr, "out_text must not be null");
  *out_text = dup(value);
}

void clear_out(char** out) {
  if (out != nullptr) *out = nullptr;
}

Value call_json(axllm_c_json_fn fn, void* user, const Value& input) {
  if (fn == nullptr) throw std::invalid_argument("callback must not be null");
  std::string in = axllm::stringify(input);
  char* out = nullptr;
  int rc = fn(user, in.c_str(), &out);
  std::string result = out == nullptr ? std::string() : std::string(out);
  std::free(out);
  if (rc != 0) throw std::runtime_error(result.empty() ? "callback failed" : result);
  if (result.empty()) return Value();
  return axllm::parse_json(result);
}

void call_observer(axllm_c_observer_fn fn, void* user, const Value& event) {
  if (fn == nullptr) return;
  std::string in = axllm::stringify(event);
  fn(user, in.c_str());
}

std::function<Value(Value)> json_fn(axllm_c_json_fn fn, void* user) {
  require(fn != nullptr, "callback must not be null");
  return [fn, user](Value input) { return call_json(fn, user, input); };
}

std::function<void(Value)> observer(axllm_c_observer_fn fn, void* user) {
  if (fn == nullptr) return {};
  return [fn, user](Value event) { call_observer(fn, user, event); };
}

Value delta_value(const axllm::AxGenDelta& delta) {
  return axllm::object({
    {"version", Value(static_cast<double>(delta.version))},
    {"index", Value(static_cast<double>(delta.index))},
    {"delta", delta.delta},
  });
}

const axllm::AxCancellationToken* token(axllm_c_cancel* cancel) {
  return cancel == nullptr ? nullptr : &cancel->token;
}

struct CallbackClient final : axllm::AIClient {
  std::string name;
  axllm_c_json_fn fn;
  void* user;
  Value complete(Value request) override { return call_json(fn, user, request); }
};

axllm::AIClient& client_of(axllm_c_ai* ai) {
  require(ai != nullptr && ai->client != nullptr, "ai must not be null");
  return *ai->client;
}

axllm::AIClient* client_or_null(axllm_c_ai* ai) {
  return ai == nullptr ? nullptr : ai->client.get();
}

axllm::AxAIService& service_of(axllm_c_ai* ai) {
  require(ai != nullptr, "ai must not be null");
  if (ai->service == nullptr) throw std::invalid_argument("operation requires a provider service, not a callback client");
  return *ai->service;
}

axllm_c_program* program_of(axllm_c_program* program) {
  require(program != nullptr && program->program != nullptr, "program must not be null");
  return program;
}

const axllm_c_program* program_of(const axllm_c_program* program) {
  require(program != nullptr && program->program != nullptr, "program must not be null");
  return program;
}

template <typename T>
T& as(const axllm_c_program* program, axllm_c_program_kind kind, const char* message) {
  program_of(program);
  if (program->kind != kind) throw std::invalid_argument(message);
  return *static_cast<T*>(program->program.get());
}

axllm::AxGen& gen(const axllm_c_program* p) { return as<axllm::AxGen>(p, AXLLM_C_PROGRAM_GEN, "operation requires a gen program"); }
axllm::AxAgent& agent(const axllm_c_program* p) { return as<axllm::AxAgent>(p, AXLLM_C_PROGRAM_AGENT, "operation requires an agent program"); }
axllm::AxFlow& flow(const axllm_c_program* p) { return as<axllm::AxFlow>(p, AXLLM_C_PROGRAM_FLOW, "operation requires a flow program"); }

std::mutex& ref_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::map<std::string, std::weak_ptr<axllm::AxProgram>>& ref_registry() {
  static std::map<std::string, std::weak_ptr<axllm::AxProgram>> registry;
  return registry;
}

void keep_referenced_programs(axllm_c_program* owner, const Value& value) {
  if (value.is_object()) {
    Value id = Core::get(value, "__agent_stage_id", Value());
    if (!id.is_null()) {
      std::lock_guard<std::mutex> lock(ref_mutex());
      auto found = ref_registry().find(axllm::display(id));
      if (found != ref_registry().end()) {
        if (auto program = found->second.lock()) owner->keep.push_back(program);
      }
    }
  }
  if (auto* object = std::get_if<std::shared_ptr<axllm::Object>>(&value.data)) {
    if (*object) for (const auto& entry : **object) keep_referenced_programs(owner, entry.second);
  } else if (auto* array = std::get_if<std::shared_ptr<axllm::Array>>(&value.data)) {
    if (*array) for (const auto& item : **array) keep_referenced_programs(owner, item);
  }
}

Value catalog_value(const axllm::AxMCPCatalogSnapshot& snapshot) {
  Value subscriptions = Value::array();
  for (const auto& uri : snapshot.subscriptions) Core::append(subscriptions, Value(uri));
  return axllm::object({
    {"namespace", Value(snapshot.namespace_name)},
    {"protocolVersion", Value(snapshot.protocol_version)},
    {"revision", Value(static_cast<double>(snapshot.revision))},
    {"serverInfo", snapshot.server_info},
    {"serverCapabilities", snapshot.server_capabilities},
    {"tools", snapshot.tools},
    {"prompts", snapshot.prompts},
    {"resources", snapshot.resources},
    {"resourceTemplates", snapshot.resource_templates},
    {"subscriptions", subscriptions},
  });
}

std::string arg_text(const Value& args, const char* key, const std::string& fallback = "") {
  Value value = Core::get(args, key, Value());
  return value.is_null() ? fallback : axllm::display(value);
}

Value arg(const Value& args, const char* key, Value fallback = Value::object()) {
  return Core::get(args, key, std::move(fallback));
}

axllm_c_program* new_program(axllm_c_program_kind kind, std::shared_ptr<axllm::AxProgram> program) {
  auto* out = new axllm_c_program{kind, std::move(program), {}};
  return out;
}

}  // namespace

extern "C" {

/* ---- memory, errors, version ---- */

const char* axllm_c_version(void) { return "25.4.10"; }

char* axllm_c_string_dup(const char* value) {
  if (value == nullptr) return nullptr;
  try {
    return dup(value);
  } catch (...) {
    return nullptr;
  }
}

void axllm_c_string_free(char* value) { std::free(value); }

const char* axllm_c_error_message(const axllm_c_error* error) {
  if (error == nullptr) return "";
  return error->message.c_str();
}

void axllm_c_error_free(axllm_c_error* error) { delete error; }

/* ---- signatures and helpers ---- */

axllm_c_status axllm_c_schema_for_signature(const char* signature, const char* section, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    require(out_json != nullptr, "out_json must not be null");
    require(signature != nullptr && signature[0] != '\0', "signature must not be empty");
    Value sig = axllm::s(signature);
    json_out(out_json, axllm::to_json_schema(Core::get(sig, text_or(section, "outputs"))));
  });
}

axllm_c_status axllm_c_signature_parse(const char* signature, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, axllm::s(text(signature, "signature"))); });
}

axllm_c_status axllm_c_signature_to_string(const char* signature_json, char** out_text, axllm_c_error** out_error) {
  clear_out(out_text);
  return guard(out_error, [&] { text_out(out_text, axllm::to_string(json_in(signature_json))); });
}

axllm_c_status axllm_c_to_json_schema(const char* fields_json, const char* title, const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    json_out(out_json, axllm::to_json_schema(json_in(fields_json, Value::array()), text_or(title, "Schema"), json_object(options_json)));
  });
}

axllm_c_status axllm_c_validate_output(const char* fields_json, const char* values_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, axllm::validate_output(json_in(fields_json, Value::array()), json_object(values_json))); });
}

axllm_c_status axllm_c_strip_internal(const char* fields_json, const char* values_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, axllm::strip_internal(json_in(fields_json, Value::array()), json_object(values_json))); });
}

axllm_c_status axllm_c_render_prompt(const char* signature_json, const char* values_json, const char* functions_json, const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    json_out(out_json, axllm::render_prompt(json_in(signature_json), json_object(values_json), json_in(functions_json, Value::array()), json_object(options_json)));
  });
}

axllm_c_status axllm_c_fold_stream(const char* events_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, axllm::fold_stream(json_in(events_json, Value::array()))); });
}

axllm_c_status axllm_c_supported_ai_models(const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, axllm::get_supported_ai_models(json_object(options_json))); });
}

/* ---- process-wide hooks ---- */

void axllm_c_set_usage_observer(axllm_c_observer_fn fn, void* user) {
  axllm::set_usage_observer(observer(fn, user));
}

void axllm_c_set_verbose_log_sink(axllm_c_observer_fn fn, void* user) {
  if (fn == nullptr) {
    axllm::verbose_log_sink() = {};
    return;
  }
  axllm::verbose_log_sink() = [fn, user](const std::string& line) { call_observer(fn, user, Value(line)); };
}

namespace {
axllm::AxCachingFunction caching(axllm_c_json_fn fn, void* user) {
  if (fn == nullptr) return {};
  return [fn, user](const std::string& key, const Value* value) -> std::optional<Value> {
    Value request = axllm::object({{"key", Value(key)}});
    if (value != nullptr) Core::set(request, "value", *value);
    Value result = call_json(fn, user, request);
    if (result.is_null()) return std::nullopt;
    return result;
  };
}

axllm::AxFunctionResultFormatter formatter(axllm_c_json_fn fn, void* user) {
  if (fn == nullptr) return {};
  return [fn, user](const Value& result) -> std::string {
    Value out = call_json(fn, user, result);
    return out.is_string() ? axllm::display(out) : axllm::stringify(out);
  };
}
}  // namespace

void axllm_c_set_caching_function(axllm_c_json_fn fn, void* user) { axllm::set_caching_function(caching(fn, user)); }

void axllm_c_set_function_result_formatter(axllm_c_json_fn fn, void* user) {
  axllm::set_function_result_formatter(formatter(fn, user));
}

axllm_c_status axllm_c_register_agent_observer(axllm_c_observer_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error) {
  clear_out(out_marker_json);
  return guard(out_error, [&] {
    require(fn != nullptr, "callback must not be null");
    json_out(out_marker_json, axllm::register_agent_observer(observer(fn, user)));
  });
}

axllm_c_status axllm_c_register_memories_search(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error) {
  clear_out(out_marker_json);
  return guard(out_error, [&] {
    require(fn != nullptr, "callback must not be null");
    json_out(out_marker_json, axllm::register_memories_search([fn, user](Value searches, Value already_loaded) {
      return call_json(fn, user, axllm::object({{"searches", searches}, {"alreadyLoaded", already_loaded}}));
    }));
  });
}

axllm_c_status axllm_c_register_skills_search(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error) {
  clear_out(out_marker_json);
  return guard(out_error, [&] { json_out(out_marker_json, axllm::register_skills_search(json_fn(fn, user))); });
}

/* ---- AI clients ---- */

axllm_c_status axllm_c_ai_new(const char* provider, const char* options_json, axllm_c_ai** out_ai, axllm_c_error** out_error) {
  if (out_ai != nullptr) *out_ai = nullptr;
  return guard(out_error, [&] {
    require(out_ai != nullptr, "out_ai must not be null");
    auto service = axllm::ai(text(provider, "provider"), json_object(options_json));
    *out_ai = new axllm_c_ai{service, service};
  });
}

axllm_c_status axllm_c_ai_from_callback(const char* name, axllm_c_json_fn complete, void* user, axllm_c_ai** out_ai, axllm_c_error** out_error) {
  if (out_ai != nullptr) *out_ai = nullptr;
  return guard(out_error, [&] {
    require(out_ai != nullptr, "out_ai must not be null");
    require(complete != nullptr, "complete must not be null");
    auto client = std::make_shared<CallbackClient>();
    client->name = text_or(name, "host");
    client->fn = complete;
    client->user = user;
    *out_ai = new axllm_c_ai{client, nullptr};
  });
}

axllm_c_status axllm_c_ai_balancer_new(axllm_c_ai* const* services, size_t count, const char* options_json, axllm_c_ai** out_ai, axllm_c_error** out_error) {
  if (out_ai != nullptr) *out_ai = nullptr;
  return guard(out_error, [&] {
    require(out_ai != nullptr, "out_ai must not be null");
    require(services != nullptr && count > 0, "balancer needs at least one service");
    std::vector<std::shared_ptr<axllm::AxAIService>> list;
    for (size_t i = 0; i < count; ++i) {
      service_of(services[i]);
      list.push_back(services[i]->service);
    }
    auto balancer = std::make_shared<axllm::AxBalancer>(std::move(list), json_object(options_json));
    *out_ai = new axllm_c_ai{balancer, balancer};
  });
}

axllm_c_status axllm_c_ai_router_new(const char* const* keys, axllm_c_ai* const* services, const char* const* descriptions, size_t count, axllm_c_ai** out_ai, axllm_c_error** out_error) {
  if (out_ai != nullptr) *out_ai = nullptr;
  return guard(out_error, [&] {
    require(out_ai != nullptr, "out_ai must not be null");
    require(keys != nullptr && services != nullptr && count > 0, "router needs at least one keyed service");
    auto router = std::make_shared<axllm::MultiServiceRouter>();
    for (size_t i = 0; i < count; ++i) {
      service_of(services[i]);
      std::string description = descriptions == nullptr ? std::string() : text_or(descriptions[i], "");
      router->set_service_entry(text(keys[i], "key"), services[i]->service, description);
    }
    *out_ai = new axllm_c_ai{router, router};
  });
}

void axllm_c_ai_free(axllm_c_ai* ai) { delete ai; }

axllm_c_status axllm_c_ai_complete(axllm_c_ai* ai, const char* request_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, client_of(ai).complete(json_object(request_json))); });
}

axllm_c_status axllm_c_ai_chat(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    json_out(out_json, client_of(ai).chat(json_object(request_json), json_object(options_json), token(cancel)));
  });
}

axllm_c_status axllm_c_ai_chat_stream(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_event_fn on_chunk, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(on_chunk != nullptr, "on_chunk must not be null");
    service_of(ai).stream_each(json_object(request_json), [on_chunk, user](const Value& chunk) {
      std::string json = axllm::stringify(chunk);
      return on_chunk(user, json.c_str()) != 0;
    }, json_object(options_json));
  });
}

axllm_c_status axllm_c_ai_embed(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).embed(json_object(request_json), json_object(options_json), token(cancel))); });
}

axllm_c_status axllm_c_ai_transcribe(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).transcribe(json_object(request_json), json_object(options_json), token(cancel))); });
}

axllm_c_status axllm_c_ai_speak(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).speak(json_object(request_json), json_object(options_json), token(cancel))); });
}

axllm_c_status axllm_c_ai_get_id(axllm_c_ai* ai, char** out_text, axllm_c_error** out_error) {
  clear_out(out_text);
  return guard(out_error, [&] {
    if (ai != nullptr && ai->service == nullptr) {
      text_out(out_text, static_cast<CallbackClient&>(client_of(ai)).name);
      return;
    }
    text_out(out_text, service_of(ai).get_id());
  });
}

axllm_c_status axllm_c_ai_get_name(axllm_c_ai* ai, char** out_text, axllm_c_error** out_error) {
  clear_out(out_text);
  return guard(out_error, [&] {
    if (ai != nullptr && ai->service == nullptr) {
      text_out(out_text, static_cast<CallbackClient&>(client_of(ai)).name);
      return;
    }
    text_out(out_text, service_of(ai).get_name());
  });
}

axllm_c_status axllm_c_ai_get_features(axllm_c_ai* ai, const char* model_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).get_features(json_in(model_json))); });
}

axllm_c_status axllm_c_ai_get_model_list(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).get_model_list()); });
}

axllm_c_status axllm_c_ai_get_metrics(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).get_metrics()); });
}

axllm_c_status axllm_c_ai_get_options(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, service_of(ai).get_options()); });
}

axllm_c_status axllm_c_ai_set_options(axllm_c_ai* ai, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { service_of(ai).set_options(json_object(options_json)); });
}

axllm_c_status axllm_c_ai_get_estimated_cost(axllm_c_ai* ai, const char* model_usage_json, double* out_cost, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(out_cost != nullptr, "out_cost must not be null");
    *out_cost = service_of(ai).get_estimated_cost(json_object(model_usage_json));
  });
}

axllm_c_status axllm_c_ai_get_last_used(axllm_c_ai* ai, const char* which, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    std::string kind = text_or(which, "chat");
    auto& service = service_of(ai);
    if (kind == "chat") json_out(out_json, service.get_last_used_chat_model());
    else if (kind == "embed") json_out(out_json, service.get_last_used_embed_model());
    else if (kind == "config") json_out(out_json, service.get_last_used_model_config());
    else throw std::invalid_argument("which must be chat, embed or config");
  });
}

/* ---- cancellation and run control ---- */

axllm_c_cancel* axllm_c_cancel_new(void) {
  try {
    return new axllm_c_cancel{};
  } catch (...) {
    return nullptr;
  }
}

void axllm_c_cancel_free(axllm_c_cancel* cancel) { delete cancel; }

int axllm_c_cancel_cancel(axllm_c_cancel* cancel, const char* reason) {
  if (cancel == nullptr) return 0;
  try {
    return cancel->token.cancel(text_or(reason, "cancelled")) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int axllm_c_cancel_is_cancelled(const axllm_c_cancel* cancel) {
  return cancel != nullptr && cancel->token.is_cancelled() ? 1 : 0;
}

axllm_c_run_control* axllm_c_run_control_new(void) {
  try {
    return new axllm_c_run_control{axllm::run_control()};
  } catch (...) {
    return nullptr;
  }
}

void axllm_c_run_control_free(axllm_c_run_control* control) { delete control; }

axllm_c_status axllm_c_run_control_steer(axllm_c_run_control* control, const char* text_value, const char* target, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(control != nullptr, "control must not be null");
    control->control.steer(text(text_value, "text"), text_or(target, "root"));
  });
}

axllm_c_status axllm_c_run_control_set_thinking_budget(axllm_c_run_control* control, const char* level, const char* target, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(control != nullptr, "control must not be null");
    control->control.set_thinking_token_budget(text(level, "level"), text_or(target, "root"));
  });
}

axllm_c_status axllm_c_run_control_abort(axllm_c_run_control* control, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(control != nullptr, "control must not be null");
    control->control.abort();
  });
}

axllm_c_status axllm_c_run_control_on_event(axllm_c_run_control* control, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(control != nullptr && fn != nullptr, "control and fn must not be null");
    control->control.on_event(observer(fn, user));
  });
}

axllm_c_status axllm_c_run_control_value(axllm_c_run_control* control, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    require(control != nullptr, "control must not be null");
    json_out(out_json, control->control.value());
  });
}

/* ---- tools ---- */

axllm_c_status axllm_c_tool_new(const char* name, const char* description, const char* parameters_json, axllm_c_json_fn handler, void* user, axllm_c_tool** out_tool, axllm_c_error** out_error) {
  if (out_tool != nullptr) *out_tool = nullptr;
  return guard(out_error, [&] {
    require(out_tool != nullptr, "out_tool must not be null");
    *out_tool = new axllm_c_tool{axllm::Tool(text(name, "name"), text_or(description, ""), json_object(parameters_json), json_fn(handler, user))};
  });
}

axllm_c_status axllm_c_tool_new_with_context(const char* name, const char* description, const char* parameters_json, axllm_c_json_fn handler, void* user, axllm_c_tool** out_tool, axllm_c_error** out_error) {
  if (out_tool != nullptr) *out_tool = nullptr;
  return guard(out_error, [&] {
    require(out_tool != nullptr, "out_tool must not be null");
    require(handler != nullptr, "handler must not be null");
    auto* tool = new axllm_c_tool{axllm::Tool(text(name, "name"), text_or(description, ""), json_object(parameters_json))};
    tool->tool.context_handler([handler, user](Value args, const axllm::AxToolContext& context) {
      Value ctx = axllm::object({
        {"callId", Value(context.call_id)},
        {"sessionId", Value(context.session_id)},
        {"executionPath", Value(context.execution_path)},
        {"eventContext", context.event_context},
        {"cancelled", Value(context.is_cancelled())},
      });
      return call_json(handler, user, axllm::object({{"args", args}, {"context", ctx}}));
    });
    *out_tool = tool;
  });
}

axllm_c_status axllm_c_tool_set_execution(axllm_c_tool* tool, const char* mode, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(tool != nullptr, "tool must not be null");
    tool->tool.execution(text_or(mode, "blocking"));
  });
}

axllm_c_status axllm_c_tool_value(const axllm_c_tool* tool, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    require(tool != nullptr, "tool must not be null");
    json_out(out_json, tool->tool.value());
  });
}

void axllm_c_tool_free(axllm_c_tool* tool) { delete tool; }

/* ---- programs: constructors ---- */

axllm_c_status axllm_c_gen_new(const char* signature, const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error) {
  if (out_program != nullptr) *out_program = nullptr;
  return guard(out_error, [&] {
    require(out_program != nullptr, "out_program must not be null");
    *out_program = new_program(AXLLM_C_PROGRAM_GEN, std::make_shared<axllm::AxGen>(axllm::s(text(signature, "signature")), json_object(options_json)));
  });
}

axllm_c_status axllm_c_agent_new(const char* signature, const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error) {
  if (out_program != nullptr) *out_program = nullptr;
  return guard(out_error, [&] {
    require(out_program != nullptr, "out_program must not be null");
    *out_program = new_program(AXLLM_C_PROGRAM_AGENT, std::make_shared<axllm::AxAgent>(Value(text(signature, "signature")), json_object(options_json)));
  });
}

axllm_c_status axllm_c_flow_new(const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error) {
  if (out_program != nullptr) *out_program = nullptr;
  return guard(out_error, [&] {
    require(out_program != nullptr, "out_program must not be null");
    *out_program = new_program(AXLLM_C_PROGRAM_FLOW, std::make_shared<axllm::AxFlow>(json_object(options_json)));
  });
}

axllm_c_status axllm_c_flow_from_mermaid(const char* mermaid, const char* bindings_json, axllm_c_program** out_program, axllm_c_error** out_error) {
  if (out_program != nullptr) *out_program = nullptr;
  return guard(out_error, [&] {
    require(out_program != nullptr, "out_program must not be null");
    Value bindings = json_object(bindings_json);
    std::unique_ptr<axllm_c_program> program(new_program(AXLLM_C_PROGRAM_FLOW, std::make_shared<axllm::AxFlow>(text(mermaid, "mermaid"), bindings)));
    keep_referenced_programs(program.get(), bindings);
    *out_program = program.release();
  });
}

void axllm_c_program_free(axllm_c_program* program) { delete program; }

axllm_c_program_kind axllm_c_program_get_kind(const axllm_c_program* program) {
  return program == nullptr ? AXLLM_C_PROGRAM_GEN : program->kind;
}

axllm_c_status axllm_c_program_ref(axllm_c_program* program, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    Value ref = Core::agent_stage_ref(*program->program);
    {
      std::lock_guard<std::mutex> lock(ref_mutex());
      ref_registry()[axllm::display(Core::get(ref, "__agent_stage_id"))] = program->program;
    }
    json_out(out_json, ref);
  });
}

/* ---- programs: run ---- */

axllm_c_status axllm_c_program_forward(axllm_c_program* program, axllm_c_ai* ai, const char* values_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    auto& client = client_of(ai);
    Value values = json_object(values_json);
    Value options = json_object(options_json);
    switch (program->kind) {
      case AXLLM_C_PROGRAM_GEN: json_out(out_json, gen(program).forward(client, values, options, token(cancel))); break;
      case AXLLM_C_PROGRAM_AGENT: json_out(out_json, agent(program).forward(client, values, options, token(cancel))); break;
      case AXLLM_C_PROGRAM_FLOW: json_out(out_json, flow(program).forward(client, values, options, token(cancel))); break;
    }
  });
}

axllm_c_status axllm_c_program_streaming_forward(axllm_c_program* program, axllm_c_ai* ai, const char* values_json, const char* options_json, axllm_c_cancel* cancel, axllm_c_event_fn on_delta, void* user, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    auto& client = client_of(ai);
    Value values = json_object(values_json);
    Value options = json_object(options_json);
    axllm::AxGenDeltaHandler handler = [on_delta, user](const axllm::AxGenDelta& delta) {
      if (on_delta == nullptr) return true;
      std::string json = axllm::stringify(delta_value(delta));
      return on_delta(user, json.c_str()) != 0;
    };
    Value out;
    if (program->kind == AXLLM_C_PROGRAM_GEN) {
      out = gen(program).streaming_forward(client, values, options, handler, token(cancel));
    } else if (program->kind == AXLLM_C_PROGRAM_AGENT) {
      axllm::AxCancellationScope scope(token(cancel));
      out = agent(program).streaming_forward(client, values, options, handler);
    } else {
      axllm::AxCancellationScope scope(token(cancel));
      out = flow(program).streaming_forward(client, values, options);
      if (on_delta != nullptr && out.is_array()) {
        for (const auto& event : Core::iter(out)) {
          std::string json = axllm::stringify(event);
          if (on_delta(user, json.c_str()) == 0) break;
        }
      }
    }
    if (out_json != nullptr) json_out(out_json, out);
  });
}

axllm_c_status axllm_c_program_set_cancellation(axllm_c_program* program, axllm_c_cancel* cancel, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(cancel != nullptr, "cancel must not be null");
    gen(program).set_cancellation(cancel->token);
  });
}

axllm_c_status axllm_c_program_get(const axllm_c_program* program, const char* what, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    std::string key = text(what, "what");
    const auto& base = *program->program;
    if (key == "traces") return json_out(out_json, base.get_traces());
    if (key == "chat_log") return json_out(out_json, base.get_chat_log());
    if (key == "usage") return json_out(out_json, program->kind == AXLLM_C_PROGRAM_AGENT ? agent(program).get_usage() : base.get_usage());
    if (key == "optimizable_components") return json_out(out_json, base.get_optimizable_components());
    if (key == "instruction") {
      if (program->kind == AXLLM_C_PROGRAM_GEN) return json_out(out_json, gen(program).get_instruction());
      return json_out(out_json, agent(program).get_instruction());
    }
    if (program->kind == AXLLM_C_PROGRAM_GEN) {
      auto& g = gen(program);
      if (key == "function_call_traces") return json_out(out_json, g.get_function_call_traces());
      if (key == "memory") return json_out(out_json, g.get_memory().value());
      if (key == "value") return json_out(out_json, g.value());
    } else if (program->kind == AXLLM_C_PROGRAM_FLOW) {
      auto& f = flow(program);
      if (key == "value") return json_out(out_json, f.value());
      if (key == "plan") return json_out(out_json, f.get_plan());
      if (key == "mermaid") return json_out(out_json, Value(f.str()));
    } else {
      auto& a = agent(program);
      if (key == "state") return json_out(out_json, a.get_state());
      if (key == "action_log") return json_out(out_json, a.get_action_log());
      if (key == "trace") return json_out(out_json, a.get_trace());
      if (key == "export_trace") return json_out(out_json, a.export_trace());
      if (key == "runtime_contract") return json_out(out_json, a.get_runtime_contract());
      if (key == "policy") return json_out(out_json, a.get_policy());
      if (key == "policy_registry") return json_out(out_json, a.get_policy_registry());
      if (key == "callable_inventory") return json_out(out_json, a.get_callable_inventory());
      if (key == "discovery_catalog") return json_out(out_json, a.get_discovery_catalog());
      if (key == "runtime_state") return json_out(out_json, a.export_runtime_state());
      if (key == "optimizer_metadata") return json_out(out_json, a.get_optimizer_metadata());
    }
    throw std::invalid_argument("unknown or unsupported program property: " + key);
  });
}

/* ---- programs: configuration ---- */

axllm_c_status axllm_c_program_set_instruction(axllm_c_program* program, const char* instruction_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    program_of(program);
    Value instruction = json_in(instruction_json);
    if (program->kind == AXLLM_C_PROGRAM_GEN) gen(program).set_instruction(instruction);
    else agent(program).set_instruction(instruction);
  });
}

axllm_c_status axllm_c_program_set_demos(axllm_c_program* program, const char* demos_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    program_of(program);
    Value demos = json_in(demos_json, Value::array());
    if (program->kind == AXLLM_C_PROGRAM_GEN) gen(program).set_demos(demos);
    else flow(program).set_demos(demos);
  });
}

axllm_c_status axllm_c_program_clear_instruction(axllm_c_program* program, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).clear_instruction(); });
}

axllm_c_status axllm_c_program_set_examples(axllm_c_program* program, const char* examples_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).set_examples(json_in(examples_json, Value::array())); });
}

axllm_c_status axllm_c_program_set_sample_count(axllm_c_program* program, int sample_count, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).set_sample_count(sample_count); });
}

axllm_c_status axllm_c_program_set_result_picker(axllm_c_program* program, axllm_c_json_fn picker, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(picker != nullptr, "picker must not be null");
    gen(program).set_result_picker([picker, user](const Value& samples) {
      return static_cast<int>(Core::number(call_json(picker, user, samples)));
    });
  });
}

axllm_c_status axllm_c_program_add_tool(axllm_c_program* program, const axllm_c_tool* tool, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    program_of(program);
    require(tool != nullptr, "tool must not be null");
    if (program->kind == AXLLM_C_PROGRAM_AGENT) agent(program).add_tool_module(tool->tool.name, {tool->tool});
    else gen(program).add_tool(tool->tool);
  });
}

axllm_c_status axllm_c_program_set_stop_functions(axllm_c_program* program, const char* names_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).set_stop_functions(json_in(names_json, Value::array())); });
}

axllm_c_status axllm_c_program_on_function_call(axllm_c_program* program, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).on_function_call(observer(fn, user)); });
}

axllm_c_status axllm_c_program_add_assert(axllm_c_program* program, const char* assertion_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).add_assert(json_object(assertion_json)); });
}

axllm_c_status axllm_c_program_add_assert_fn(axllm_c_program* program, axllm_c_json_fn fn, void* user, const char* message, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    if (message == nullptr) gen(program).add_assert(json_fn(fn, user));
    else gen(program).add_assert(json_fn(fn, user), message);
  });
}

axllm_c_status axllm_c_program_add_streaming_assert(axllm_c_program* program, const char* assertion_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).add_streaming_assert(json_object(assertion_json)); });
}

axllm_c_status axllm_c_program_add_streaming_assert_fn(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, const char* message, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(fn != nullptr, "callback must not be null");
    gen(program).add_streaming_assert(text(field, "field"), [fn, user](const std::string& value, bool done) {
      return call_json(fn, user, axllm::object({{"text", Value(value)}, {"done", Value(done)}}));
    }, text_or(message, ""));
  });
}

axllm_c_status axllm_c_program_add_field_transform(axllm_c_program* program, const char* field, const char* op, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).add_field_transform(text(field, "field"), text(op, "op")); });
}

axllm_c_status axllm_c_program_add_field_transform_fn(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).add_field_transform(text(field, "field"), json_fn(fn, user)); });
}

namespace {
axllm::AxFieldProcessor field_processor(axllm_c_json_fn fn, void* user) {
  require(fn != nullptr, "callback must not be null");
  return [fn, user](const Value& value, const axllm::AxFieldProcessorContext& context) {
    return call_json(fn, user, axllm::object({{"value", value}, {"values", context.values}, {"done", Value(context.done)}}));
  };
}
}  // namespace

axllm_c_status axllm_c_program_add_field_processor(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_field_processor_mode mode, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    auto native = mode == AXLLM_C_FIELD_TRANSFORM ? axllm::AxFieldProcessorMode::Transform : axllm::AxFieldProcessorMode::Feedback;
    gen(program).add_field_processor(text(field, "field"), field_processor(fn, user), native);
  });
}

axllm_c_status axllm_c_program_add_streaming_field_processor(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).add_streaming_field_processor(text(field, "field"), field_processor(fn, user)); });
}

axllm_c_status axllm_c_program_set_caching_function(axllm_c_program* program, axllm_c_json_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).set_caching_function(caching(fn, user)); });
}

axllm_c_status axllm_c_program_set_function_result_formatter(axllm_c_program* program, axllm_c_json_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { gen(program).set_function_result_formatter(formatter(fn, user)); });
}

/* ---- programs: optimization ---- */

axllm_c_status axllm_c_program_apply_optimized_components(axllm_c_program* program, const char* component_map_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { program_of(program)->program->apply_optimized_components(json_object(component_map_json)); });
}

axllm_c_status axllm_c_program_apply_optimization(axllm_c_program* program, const char* artifact_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    program_of(program);
    Value artifact = json_object(artifact_json);
    switch (program->kind) {
      case AXLLM_C_PROGRAM_GEN: gen(program).apply_optimization(artifact); break;
      case AXLLM_C_PROGRAM_AGENT: agent(program).apply_optimization(artifact); break;
      case AXLLM_C_PROGRAM_FLOW: flow(program).apply_optimization(artifact); break;
    }
  });
}

axllm_c_status axllm_c_program_evaluate_optimization(axllm_c_program* program, axllm_c_ai* ai, const char* dataset_json, const char* candidate_map_json, const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    auto& client = client_of(ai);
    Value dataset = json_in(dataset_json, Value::array());
    Value candidates = json_object(candidate_map_json);
    Value options = json_object(options_json);
    switch (program->kind) {
      case AXLLM_C_PROGRAM_GEN: json_out(out_json, gen(program).evaluate_optimization(client, dataset, candidates, options)); break;
      case AXLLM_C_PROGRAM_AGENT: json_out(out_json, agent(program).evaluate_optimization(client, dataset, candidates, options)); break;
      case AXLLM_C_PROGRAM_FLOW: json_out(out_json, flow(program).evaluate_optimization(client, dataset, candidates, options)); break;
    }
  });
}

axllm_c_status axllm_c_optimize(axllm_c_program* program, axllm_c_ai* student, const char* dataset_json, const char* options_json, axllm_c_ai* teacher, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    auto& client = client_of(student);
    Value dataset = json_in(dataset_json, Value::array());
    Value options = json_object(options_json);
    switch (program->kind) {
      case AXLLM_C_PROGRAM_GEN: json_out(out_json, axllm::optimize(gen(program), client, dataset, options, client_or_null(teacher))); break;
      case AXLLM_C_PROGRAM_AGENT: json_out(out_json, axllm::optimize(agent(program), client, dataset, options, client_or_null(teacher))); break;
      case AXLLM_C_PROGRAM_FLOW: json_out(out_json, axllm::optimize(flow(program), client, dataset, options, client_or_null(teacher))); break;
    }
  });
}

axllm_c_status axllm_c_optimizer_gepa_new(axllm_c_ai* reflection, const char* options_json, axllm_c_optimizer** out_optimizer, axllm_c_error** out_error) {
  if (out_optimizer != nullptr) *out_optimizer = nullptr;
  return guard(out_error, [&] {
    require(out_optimizer != nullptr, "out_optimizer must not be null");
    std::shared_ptr<axllm::AIClient> client = reflection == nullptr ? nullptr : reflection->client;
    *out_optimizer = new axllm_c_optimizer{std::make_unique<axllm::AxGEPA>(client.get(), json_object(options_json)), client};
  });
}

axllm_c_status axllm_c_optimizer_bootstrap_new(const char* options_json, axllm_c_optimizer** out_optimizer, axllm_c_error** out_error) {
  if (out_optimizer != nullptr) *out_optimizer = nullptr;
  return guard(out_error, [&] {
    require(out_optimizer != nullptr, "out_optimizer must not be null");
    *out_optimizer = new axllm_c_optimizer{std::make_unique<axllm::AxBootstrapFewShot>(json_object(options_json)), nullptr};
  });
}

void axllm_c_optimizer_free(axllm_c_optimizer* optimizer) { delete optimizer; }

axllm_c_status axllm_c_program_optimize_with(axllm_c_program* program, axllm_c_optimizer* optimizer, axllm_c_ai* ai, const char* dataset_json, const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    program_of(program);
    require(optimizer != nullptr && optimizer->engine != nullptr, "optimizer must not be null");
    auto& engine = *optimizer->engine;
    Value dataset = json_in(dataset_json, Value::array());
    Value options = json_object(options_json);
    auto run = [&](auto& target) {
      if (ai == nullptr) return target.optimize_with(engine, dataset, options);
      return target.optimize_with(engine, client_of(ai), dataset, options);
    };
    switch (program->kind) {
      case AXLLM_C_PROGRAM_GEN: json_out(out_json, run(gen(program))); break;
      case AXLLM_C_PROGRAM_AGENT: json_out(out_json, run(agent(program))); break;
      case AXLLM_C_PROGRAM_FLOW: json_out(out_json, run(flow(program))); break;
    }
  });
}

/* ---- playbooks ---- */

axllm_c_status axllm_c_playbook_new(axllm_c_program* program, axllm_c_ai* student, const char* options_json, axllm_c_ai* teacher, axllm_c_playbook** out_playbook, axllm_c_error** out_error) {
  if (out_playbook != nullptr) *out_playbook = nullptr;
  return guard(out_error, [&] {
    require(out_playbook != nullptr, "out_playbook must not be null");
    program_of(program);
    auto& client = client_of(student);
    std::unique_ptr<axllm_c_playbook> out(new axllm_c_playbook{nullptr, nullptr, {program->program, student->client}});
    if (teacher != nullptr) out->keep.push_back(teacher->client);
    if (program->kind == AXLLM_C_PROGRAM_AGENT) {
      out->playbook = &agent(program).playbook(client, json_object(options_json), client_or_null(teacher));
    } else {
      out->owned = std::make_unique<axllm::AxPlaybook>(gen(program), client, client_or_null(teacher), json_object(options_json));
      out->playbook = out->owned.get();
    }
    *out_playbook = out.release();
  });
}

void axllm_c_playbook_free(axllm_c_playbook* playbook) { delete playbook; }

namespace {
axllm::AxPlaybook& playbook_of(axllm_c_playbook* playbook) {
  require(playbook != nullptr && playbook->playbook != nullptr, "playbook must not be null");
  return *playbook->playbook;
}
}  // namespace

axllm_c_status axllm_c_playbook_evolve(axllm_c_playbook* playbook, const char* dataset_json, const char* options_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, playbook_of(playbook).evolve(json_in(dataset_json, Value::array()), json_object(options_json))); });
}

axllm_c_status axllm_c_playbook_update(axllm_c_playbook* playbook, const char* args_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, playbook_of(playbook).update(json_object(args_json))); });
}

axllm_c_status axllm_c_playbook_apply(axllm_c_playbook* playbook, axllm_c_error** out_error) {
  return guard(out_error, [&] { playbook_of(playbook).apply_to(nullptr); });
}

axllm_c_status axllm_c_playbook_render(axllm_c_playbook* playbook, char** out_text, axllm_c_error** out_error) {
  clear_out(out_text);
  return guard(out_error, [&] { text_out(out_text, playbook_of(playbook).render()); });
}

axllm_c_status axllm_c_playbook_to_json(axllm_c_playbook* playbook, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] { json_out(out_json, playbook_of(playbook).to_json()); });
}

axllm_c_status axllm_c_playbook_load(axllm_c_playbook* playbook, const char* snapshot_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { playbook_of(playbook).load(json_object(snapshot_json)); });
}

/* ---- agents ---- */

axllm_c_status axllm_c_agent_set_signature(axllm_c_program* program, const char* signature, axllm_c_error** out_error) {
  return guard(out_error, [&] { agent(program).set_signature(Value(text(signature, "signature"))); });
}

axllm_c_status axllm_c_agent_add_actor_instruction(axllm_c_program* program, const char* addendum_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { agent(program).add_actor_instruction(json_in(addendum_json)); });
}

axllm_c_status axllm_c_agent_add_tool_module(axllm_c_program* program, const char* name, const axllm_c_tool* const* tools, size_t count, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(tools != nullptr || count == 0, "tools must not be null");
    std::vector<axllm::Tool> list;
    for (size_t i = 0; i < count; ++i) {
      require(tools[i] != nullptr, "tool must not be null");
      list.push_back(tools[i]->tool);
    }
    agent(program).add_tool_module(text(name, "name"), list);
  });
}

axllm_c_status axllm_c_agent_add_child_agent(axllm_c_program* program, const char* namespace_name, const char* name, axllm_c_program* child, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    agent(child);
    agent(program).add_child_agent(text_or(namespace_name, ""), text(name, "name"), std::static_pointer_cast<axllm::AxAgent>(child->program));
  });
}

axllm_c_status axllm_c_agent_set_state(axllm_c_program* program, const char* state_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { agent(program).set_state(json_object(state_json)); });
}

axllm_c_status axllm_c_agent_set_citations_observer(axllm_c_program* program, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { agent(program).set_citations_observer(observer(fn, user)); });
}

axllm_c_status axllm_c_agent_set_playbook_observer(axllm_c_program* program, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] { agent(program).set_playbook_observer(observer(fn, user)); });
}

axllm_c_status axllm_c_agent_call(axllm_c_program* program, const char* op, const char* args_json, axllm_c_ai* ai, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    auto& a = agent(program);
    std::string name = text(op, "op");
    Value args = json_object(args_json);
    if (name == "discover") return json_out(out_json, a.discover(args));
    if (name == "recall") return json_out(out_json, a.recall(args));
    if (name == "inspect_runtime") return json_out(out_json, a.inspect_runtime(args));
    if (name == "export_session_state") return json_out(out_json, a.export_session_state(args));
    if (name == "restore_session_state") return json_out(out_json, a.restore_session_state(arg(args, "snapshot"), arg(args, "options")));
    if (name == "close_runtime_session") return json_out(out_json, a.close_runtime_session());
    if (name == "restore_runtime_state") return json_out(out_json, a.restore_runtime_state(arg(args, "snapshot")));
    if (name == "replay_trace") return json_out(out_json, a.replay_trace(arg(args, "trace"), arg(args, "fixtures")));
    if (name == "used") return json_out(out_json, a.used(arg(args, "id", Value()), arg(args, "reason", Value("")), arg(args, "stage", Value("executor"))));
    if (name == "invoke_callable") return json_out(out_json, a.invoke_callable(arg(args, "name", Value()), arg(args, "args"), arg(args, "options")));
    if (name == "optimize") return json_out(out_json, a.optimize(arg(args, "dataset", Value::array()), arg(args, "options")));
    if (name == "evaluate_optimization_task") return json_out(out_json, a.evaluate_optimization_task(client_of(ai), arg(args, "task"), arg(args, "options")));
    throw std::invalid_argument("unknown agent operation: " + name);
  });
}

/* ---- flows ---- */

axllm_c_status axllm_c_flow_execute(axllm_c_program* program, const char* name, axllm_c_program* step, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    auto& f = flow(program);
    program_of(step);
    f.execute(text(name, "name"), *step->program, json_object(options_json));
    program->keep.push_back(step->program);
  });
}

axllm_c_status axllm_c_flow_derive(axllm_c_program* program, const char* name, axllm_c_program* step, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    auto& f = flow(program);
    program_of(step);
    f.derive(text(name, "name"), *step->program, json_object(options_json));
    program->keep.push_back(step->program);
  });
}

axllm_c_status axllm_c_flow_map(axllm_c_program* program, const char* name, axllm_c_json_fn mapper, void* user, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { flow(program).map(text(name, "name"), json_fn(mapper, user), json_object(options_json)); });
}

axllm_c_status axllm_c_flow_branch(axllm_c_program* program, const char* name, axllm_c_json_fn predicate, void* user, const char* branches_json, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    Value branches = json_object(branches_json);
    flow(program).branch(text(name, "name"), json_fn(predicate, user), branches, json_object(options_json));
    keep_referenced_programs(program, branches);
  });
}

axllm_c_status axllm_c_flow_while(axllm_c_program* program, const char* name, axllm_c_json_fn condition, void* user, const char* steps_json, int max_iterations, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    Value steps = json_in(steps_json, Value::array());
    flow(program).while_loop(text(name, "name"), json_fn(condition, user), steps, max_iterations > 0 ? max_iterations : 100, json_object(options_json));
    keep_referenced_programs(program, steps);
  });
}

axllm_c_status axllm_c_flow_feedback(axllm_c_program* program, const char* name, axllm_c_json_fn condition, void* user, const char* steps_json, int max_iterations, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    Value steps = json_in(steps_json, Value::array());
    flow(program).feedback(text(name, "name"), json_fn(condition, user), steps, max_iterations > 0 ? max_iterations : 10, json_object(options_json));
    keep_referenced_programs(program, steps);
  });
}

axllm_c_status axllm_c_flow_node_extended(axllm_c_program* program, const char* name, const char* base_signature, const char* extensions_json, const char* options_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    flow(program).node_extended(text(name, "name"), Value(text(base_signature, "base_signature")), json_object(extensions_json), json_object(options_json));
  });
}

axllm_c_status axllm_c_flow_parallel(axllm_c_program* program, const char* steps_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    Value steps = json_in(steps_json, Value::array());
    flow(program).parallel(steps);
    keep_referenced_programs(program, steps);
  });
}

axllm_c_status axllm_c_flow_returns(axllm_c_program* program, const char* spec_json, axllm_c_error** out_error) {
  return guard(out_error, [&] { flow(program).returns(json_object(spec_json)); });
}

axllm_c_status axllm_c_flow_add_raw_step(axllm_c_program* program, const char* step_json, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    Value step = json_object(step_json);
    flow(program).add_raw_step(step);
    keep_referenced_programs(program, step);
  });
}

axllm_c_status axllm_c_flow_callback(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error) {
  clear_out(out_marker_json);
  return guard(out_error, [&] { json_out(out_marker_json, axllm::flow_callback(json_fn(fn, user))); });
}

/* ---- MCP ---- */

axllm_c_status axllm_c_mcp_client_new(const char* transport_json, const char* options_json, axllm_c_mcp_client** out_client, axllm_c_error** out_error) {
  if (out_client != nullptr) *out_client = nullptr;
  return guard(out_error, [&] {
    require(out_client != nullptr, "out_client must not be null");
    Value spec = json_object(transport_json);
    std::string type = arg_text(spec, "type");
    std::shared_ptr<axllm::AxMCPTransport> transport;
    if (type == "http") {
      transport = std::make_shared<axllm::AxMCPStreamableHTTPTransport>(arg_text(spec, "endpoint"), arg(spec, "options"));
    } else if (type == "stdio") {
      std::vector<std::string> args;
      for (const auto& item : Core::iter(arg(spec, "args", Value::array()))) args.push_back(axllm::display(item));
      transport = std::make_shared<axllm::AxMCPStdioTransport>(arg_text(spec, "command"), args);
    } else if (type == "scripted") {
      transport = std::make_shared<axllm::AxMCPScriptedTransport>(arg(spec, "responses", Value::array()));
    } else {
      throw std::invalid_argument("transport type must be http, stdio or scripted");
    }
    *out_client = new axllm_c_mcp_client{std::make_shared<axllm::AxMCPClient>(transport, json_object(options_json))};
  });
}

void axllm_c_mcp_client_free(axllm_c_mcp_client* client) { delete client; }

namespace {
axllm::AxMCPClient& mcp_of(axllm_c_mcp_client* client) {
  require(client != nullptr && client->client != nullptr, "client must not be null");
  return *client->client;
}
}  // namespace

axllm_c_status axllm_c_mcp_client_init(axllm_c_mcp_client* client, axllm_c_error** out_error) {
  return guard(out_error, [&] { mcp_of(client).init(); });
}

axllm_c_status axllm_c_mcp_client_close(axllm_c_mcp_client* client, axllm_c_error** out_error) {
  return guard(out_error, [&] { mcp_of(client).close(); });
}

axllm_c_status axllm_c_mcp_client_call(axllm_c_mcp_client* client, const char* method, const char* args_json, char** out_json, axllm_c_error** out_error) {
  clear_out(out_json);
  return guard(out_error, [&] {
    auto& c = mcp_of(client);
    std::string m = text(method, "method");
    Value a = json_object(args_json);
    Value out;
    if (m == "ping") out = c.ping();
    else if (m == "list_tools") out = c.list_tools(arg_text(a, "cursor"));
    else if (m == "call_tool") out = c.call_tool(arg_text(a, "name"), arg(a, "arguments"), arg(a, "options"));
    else if (m == "call_tool_outcome") out = c.call_tool_outcome(arg_text(a, "name"), arg(a, "arguments"));
    else if (m == "list_prompts") out = c.list_prompts(arg_text(a, "cursor"));
    else if (m == "get_prompt") out = c.get_prompt(arg_text(a, "name"), arg(a, "arguments"));
    else if (m == "list_resources") out = c.list_resources(arg_text(a, "cursor"));
    else if (m == "read_resource") out = c.read_resource(arg_text(a, "uri"));
    else if (m == "subscribe_resource") out = c.subscribe_resource(arg_text(a, "uri"));
    else if (m == "unsubscribe_resource") out = c.unsubscribe_resource(arg_text(a, "uri"));
    else if (m == "list_resource_templates") out = c.list_resource_templates(arg_text(a, "cursor"));
    else if (m == "get_task") out = c.get_task(arg_text(a, "taskId"));
    else if (m == "cancel_task") out = c.cancel_task(arg_text(a, "taskId"));
    else if (m == "list_tasks") out = c.list_tasks(arg_text(a, "cursor"));
    else if (m == "get_task_result") out = c.get_task_result(arg_text(a, "taskId"));
    else if (m == "provide_task_input") c.provide_task_input(arg_text(a, "taskId"), arg(a, "inputResponses"));
    else if (m == "notify") c.notify(arg_text(a, "method"), arg(a, "params", Value()));
    else if (m == "request") out = c.request(arg_text(a, "method"), arg(a, "params"));
    else if (m == "discover") out = c.discover();
    else if (m == "refresh") c.refresh(Core::truthy(arg(a, "force", Value(false))));
    else if (m == "inspect_catalog") out = catalog_value(c.inspect_catalog(Core::truthy(arg(a, "refresh", Value(false)))));
    else if (m == "protocol_version") out = Value(c.protocol_version());
    else if (m == "namespace") out = Value(c.namespace_name());
    else if (m == "prompts") out = c.prompts();
    else if (m == "resources") out = c.resources();
    else if (m == "resource_templates") out = c.resource_templates();
    else if (m == "era") out = Value(c.get_era());
    else throw std::invalid_argument("unknown MCP method: " + m);
    if (out_json != nullptr) json_out(out_json, out);
  });
}

axllm_c_status axllm_c_mcp_client_attach(axllm_c_mcp_client* client, axllm_c_program* program, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    auto& c = mcp_of(client);
    program_of(program);
    auto tools = c.to_function();
    if (program->kind == AXLLM_C_PROGRAM_AGENT) {
      agent(program).add_tool_module(c.namespace_name(), tools);
    } else {
      auto& g = gen(program);
      for (const auto& tool : tools) g.add_tool(tool);
    }
    program->keep.push_back(client->client);
  });
}

axllm_c_status axllm_c_mcp_client_set_elicitation_handler(axllm_c_mcp_client* client, axllm_c_json_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(fn != nullptr, "callback must not be null");
    mcp_of(client).set_elicitation_handler([fn, user](Value params, Value context) {
      return call_json(fn, user, axllm::object({{"params", params}, {"context", context}}));
    });
  });
}

axllm_c_status axllm_c_mcp_client_add_notification_listener(axllm_c_mcp_client* client, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error) {
  return guard(out_error, [&] {
    require(fn != nullptr, "callback must not be null");
    mcp_of(client).add_notification_listener(observer(fn, user));
  });
}

}  // extern "C"
