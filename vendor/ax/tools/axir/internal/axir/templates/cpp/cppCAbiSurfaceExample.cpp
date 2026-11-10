/* Exercises the C ABI end to end with a host callback client. Written in the C
 * subset so scripts/verify.sh also compiles it as C11. */
#include "axllm/axllm_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static int ok(axllm_c_status status, axllm_c_error* error, int line) {
  if (status == AXLLM_C_OK) return 1;
  fprintf(stderr, "axllm error at line %d: %s\n", line, axllm_c_error_message(error));
  axllm_c_error_free(error);
  failures++;
  return 0;
}

#define OK(call) ok((call), err, __LINE__)

static int contains(const char* haystack, const char* needle) {
  return haystack != NULL && strstr(haystack, needle) != NULL;
}

struct client_state {
  int calls;
};

/* Scripted model: first turn calls the search tool, then answers. */
static int complete(void* user, const char* request, char** out) {
  struct client_state* state = (struct client_state*)user;
  state->calls++;
  if (contains(request, "\"name\":\"search\"") && !contains(request, "Ax docs found")) {
    *out = axllm_c_string_dup(
      "{\"content\":\"\",\"function_calls\":[{\"id\":\"call_1\",\"name\":\"search\",\"params\":{\"query\":\"ax\"}}]}");
    return 0;
  }
  *out = axllm_c_string_dup("{\"content\":\"Answer:  Ax docs found  \"}");
  return 0;
}

static int search_calls = 0;

static int search(void* user, const char* args, char** out) {
  (void)user;
  search_calls++;
  if (!contains(args, "\"query\"")) {
    *out = axllm_c_string_dup("missing query");
    return 1;
  }
  *out = axllm_c_string_dup("{\"title\":\"Ax docs found\"}");
  return 0;
}

static int deltas = 0;

static int on_delta(void* user, const char* event) {
  (void)user;
  if (contains(event, "\"delta\"")) deltas++;
  return 1;
}

static int mapper(void* user, const char* state, char** out) {
  (void)user;
  (void)state;
  *out = axllm_c_string_dup("{\"shout\":\"MAPPED\"}");
  return 0;
}

static int events = 0;

static void on_event(void* user, const char* event) {
  (void)user;
  (void)event;
  events++;
}

int main(void) {
  axllm_c_error* err = NULL;
  char* json = NULL;

  CHECK(strlen(axllm_c_version()) > 0);
  CHECK(strstr(axllm_c_version(), "AX_VERSION") == NULL);

  /* Signatures and helpers. */
  if (OK(axllm_c_signature_parse("question:string -> answer:string", &json, &err))) {
    CHECK(contains(json, "answer"));
    axllm_c_string_free(json);
  }
  if (OK(axllm_c_schema_for_signature("question:string -> answer:string", NULL, &json, &err))) {
    CHECK(contains(json, "answer"));
    axllm_c_string_free(json);
  }
  CHECK(axllm_c_signature_parse("", &json, &err) == AXLLM_C_ERR || (axllm_c_string_free(json), 1));
  if (err) axllm_c_error_free(err), err = NULL;
  if (OK(axllm_c_supported_ai_models(NULL, &json, &err))) {
    CHECK(contains(json, "openai"));
    axllm_c_string_free(json);
  }

  /* A built-in provider can be configured without network access. */
  axllm_c_ai* openai = NULL;
  if (OK(axllm_c_ai_new("openai", "{\"api_key\":\"test\",\"model\":\"gpt-5-mini\"}", &openai, &err))) {
    char* name = NULL;
    if (OK(axllm_c_ai_get_name(openai, &name, &err))) {
      CHECK(strlen(name) > 0);
      axllm_c_string_free(name);
    }
    if (OK(axllm_c_ai_get_features(openai, NULL, &json, &err))) {
      CHECK(contains(json, "functions"));
      axllm_c_string_free(json);
    }
    if (OK(axllm_c_ai_set_options(openai, "{\"debug\":false}", &err))) {
      if (OK(axllm_c_ai_get_options(openai, &json, &err))) axllm_c_string_free(json);
    }
    axllm_c_ai* members[1] = {openai};
    axllm_c_ai* balancer = NULL;
    if (OK(axllm_c_ai_balancer_new(members, 1, NULL, &balancer, &err))) axllm_c_ai_free(balancer);
    const char* keys[1] = {"fast"};
    axllm_c_ai* router = NULL;
    if (OK(axllm_c_ai_router_new(keys, members, NULL, 1, &router, &err))) axllm_c_ai_free(router);
    axllm_c_ai_free(openai);
  }

  /* Inference through a host client: gen with a tool, assert and transform. */
  struct client_state state = {0};
  axllm_c_ai* ai = NULL;
  if (!OK(axllm_c_ai_from_callback("scripted", complete, &state, &ai, &err))) return 1;

  axllm_c_tool* tool = NULL;
  OK(axllm_c_tool_new("search", "Search docs",
                      "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}",
                      search, NULL, &tool, &err));
  if (OK(axllm_c_tool_value(tool, &json, &err))) {
    CHECK(contains(json, "search"));
    axllm_c_string_free(json);
  }

  axllm_c_program* qa = NULL;
  if (OK(axllm_c_gen_new("query:string -> answer:string", NULL, &qa, &err))) {
    CHECK(axllm_c_program_get_kind(qa) == AXLLM_C_PROGRAM_GEN);
    OK(axllm_c_program_add_tool(qa, tool, &err));
    OK(axllm_c_program_add_assert(qa, "{\"field\":\"answer\",\"contains\":\"Ax\",\"message\":\"mention Ax\"}", &err));
    OK(axllm_c_program_add_field_transform(qa, "answer", "trim", &err));
    OK(axllm_c_program_set_instruction(qa, "\"Answer briefly.\"", &err));
    if (OK(axllm_c_program_forward(qa, ai, "{\"query\":\"ax\"}", NULL, NULL, &json, &err))) {
      CHECK(contains(json, "\"answer\":\"Ax docs found\""));
      axllm_c_string_free(json);
    }
    CHECK(search_calls == 1);
    if (OK(axllm_c_program_get(qa, "traces", &json, &err))) axllm_c_string_free(json);
    if (OK(axllm_c_program_get(qa, "instruction", &json, &err))) {
      CHECK(contains(json, "Answer briefly."));
      axllm_c_string_free(json);
    }
    if (OK(axllm_c_program_get(qa, "optimizable_components", &json, &err))) axllm_c_string_free(json);
  }

  /* Streaming and cancellation. */
  axllm_c_program* plain = NULL;
  if (OK(axllm_c_gen_new("question:string -> answer:string", NULL, &plain, &err))) {
    if (OK(axllm_c_program_streaming_forward(plain, ai, "{\"question\":\"q\"}", NULL, NULL, on_delta, NULL, &json, &err))) {
      CHECK(contains(json, "Ax docs found"));
      axllm_c_string_free(json);
    }
    CHECK(deltas >= 1);

    axllm_c_cancel* cancel = axllm_c_cancel_new();
    CHECK(axllm_c_cancel_cancel(cancel, "stop") == 1);
    CHECK(axllm_c_cancel_is_cancelled(cancel) == 1);
    int before = state.calls;
    CHECK(axllm_c_program_forward(plain, ai, "{\"question\":\"q\"}", NULL, cancel, &json, &err) == AXLLM_C_ERR);
    CHECK(err != NULL && strlen(axllm_c_error_message(err)) > 0);
    axllm_c_error_free(err);
    err = NULL;
    CHECK(state.calls == before);
    axllm_c_cancel_free(cancel);
  }

  /* Kind checks surface as errors, not crashes. */
  CHECK(axllm_c_agent_set_signature(plain, "a:string -> b:string", &err) == AXLLM_C_ERR);
  axllm_c_error_free(err);
  err = NULL;

  /* Flows: a gen step, a host mapper, and declared outputs. */
  axllm_c_program* flow = NULL;
  if (OK(axllm_c_flow_new("{\"id\":\"c.flow\"}", &flow, &err))) {
    OK(axllm_c_flow_execute(flow, "qa", plain, NULL, &err));
    OK(axllm_c_flow_map(flow, "shout", mapper, NULL, NULL, &err));
    OK(axllm_c_flow_returns(flow, "{\"answer\":\"answer\",\"shout\":\"shout\"}", &err));
    axllm_c_program_free(plain);
    plain = NULL;
    if (OK(axllm_c_program_forward(flow, ai, "{\"question\":\"q\"}", NULL, NULL, &json, &err))) {
      CHECK(contains(json, "MAPPED"));
      CHECK(contains(json, "Ax docs found"));
      axllm_c_string_free(json);
    }
    if (OK(axllm_c_program_get(flow, "plan", &json, &err))) {
      CHECK(contains(json, "qa"));
      axllm_c_string_free(json);
    }
    axllm_c_program_free(flow);
  }

  /* Agents: construction, configuration and introspection. */
  axllm_c_program* agent = NULL;
  if (OK(axllm_c_agent_new("question:string -> answer:string", "{\"contextFields\":[]}", &agent, &err))) {
    CHECK(axllm_c_program_get_kind(agent) == AXLLM_C_PROGRAM_AGENT);
    OK(axllm_c_program_add_tool(agent, tool, &err));
    if (OK(axllm_c_program_get(agent, "callable_inventory", &json, &err))) {
      CHECK(contains(json, "search"));
      axllm_c_string_free(json);
    }
    if (OK(axllm_c_program_get(agent, "runtime_contract", &json, &err))) axllm_c_string_free(json);
    axllm_c_program_free(agent);
  }

  /* Run control. */
  axllm_c_run_control* control = axllm_c_run_control_new();
  OK(axllm_c_run_control_on_event(control, on_event, NULL, &err));
  OK(axllm_c_run_control_steer(control, "be brief", NULL, &err));
  if (OK(axllm_c_run_control_value(control, &json, &err))) {
    CHECK(strlen(json) > 2);
    axllm_c_string_free(json);
  }
  axllm_c_run_control_free(control);

  /* MCP over a scripted transport, attached to a program. */
  axllm_c_mcp_client* mcp = NULL;
  const char* transport =
    "{\"type\":\"scripted\",\"responses\":["
    "{\"method\":\"initialize\",\"result\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{\"tools\":{}},"
    "\"serverInfo\":{\"name\":\"scripted-mcp\",\"version\":\"1.0.0\"}}},"
    "{\"method\":\"tools/list\",\"result\":{\"tools\":[{\"name\":\"echo\",\"description\":\"Echo text\",\"inputSchema\":{\"type\":\"object\"}}]}},"
    "{\"method\":\"tools/call\",\"result\":{\"structuredContent\":{\"echo\":\"hello\"}}}]}";
  if (OK(axllm_c_mcp_client_new(transport, "{\"era\":\"legacy\"}", &mcp, &err))) {
    if (OK(axllm_c_mcp_client_init(mcp, &err))) {
      if (OK(axllm_c_mcp_client_call(mcp, "inspect_catalog", NULL, &json, &err))) {
        CHECK(contains(json, "echo"));
        axllm_c_string_free(json);
      }
      if (OK(axllm_c_mcp_client_call(mcp, "call_tool", "{\"name\":\"echo\",\"arguments\":{\"text\":\"hello\"}}", &json, &err))) {
        CHECK(contains(json, "hello"));
        axllm_c_string_free(json);
      }
      if (qa != NULL) OK(axllm_c_mcp_client_attach(mcp, qa, &err));
    }
    CHECK(axllm_c_mcp_client_call(mcp, "no_such_method", NULL, &json, &err) == AXLLM_C_ERR);
    axllm_c_error_free(err);
    err = NULL;
    axllm_c_mcp_client_free(mcp);
  }

  axllm_c_program_free(qa);
  axllm_c_tool_free(tool);
  axllm_c_ai_free(ai);

  if (failures != 0) {
    fprintf(stderr, "%d C ABI checks failed\n", failures);
    return 1;
  }
  printf("cpp-c-abi-surface-ok\n");
  return 0;
}
