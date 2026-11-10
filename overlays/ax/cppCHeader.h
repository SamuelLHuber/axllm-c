#ifndef AXLLM_C_H
#define AXLLM_C_H

/*
 * C ABI over the generated Ax C++ package.
 *
 * Conventions:
 * - Every Ax value crosses the boundary as a UTF-8 JSON string. Inputs are
 *   `const char*` (NULL means JSON null / default); outputs are `char**` that
 *   the caller frees with axllm_c_string_free.
 * - Every fallible call returns axllm_c_status and, on AXLLM_C_ERR, sets
 *   *out_error (when out_error is not NULL); free it with axllm_c_error_free.
 * - Handles are opaque and owned by the caller; release them with their
 *   *_free function. Handles are not thread-safe.
 * - A program, flow or agent keeps every handle added to it alive, so the
 *   caller may free its own reference right after adding it.
 * - Callbacks receive the `user` pointer they were registered with; the
 *   caller keeps whatever it points to alive for the handle's lifetime.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum axllm_c_status {
  AXLLM_C_OK = 0,
  AXLLM_C_ERR = 1
} axllm_c_status;

typedef struct axllm_c_error axllm_c_error;
typedef struct axllm_c_ai axllm_c_ai;
typedef struct axllm_c_program axllm_c_program;
typedef struct axllm_c_tool axllm_c_tool;
typedef struct axllm_c_cancel axllm_c_cancel;
typedef struct axllm_c_run_control axllm_c_run_control;
typedef struct axllm_c_optimizer axllm_c_optimizer;
typedef struct axllm_c_playbook axllm_c_playbook;
typedef struct axllm_c_mcp_client axllm_c_mcp_client;

typedef enum axllm_c_program_kind {
  AXLLM_C_PROGRAM_GEN = 0,
  AXLLM_C_PROGRAM_AGENT = 1,
  AXLLM_C_PROGRAM_FLOW = 2
} axllm_c_program_kind;

typedef enum axllm_c_field_processor_mode {
  AXLLM_C_FIELD_FEEDBACK = 0,
  AXLLM_C_FIELD_TRANSFORM = 1
} axllm_c_field_processor_mode;

/*
 * A JSON function: reads in_json, writes a JSON result to *out_json (a string
 * from axllm_c_string_dup or malloc; NULL means JSON null) and returns 0. To
 * fail, return non-zero and optionally set *out_json to an error message.
 */
typedef int (*axllm_c_json_fn)(void* user, const char* in_json, char** out_json);
/* A streaming handler: return non-zero to continue, 0 to stop the run. */
typedef int (*axllm_c_event_fn)(void* user, const char* event_json);
/* A one-way observer. */
typedef void (*axllm_c_observer_fn)(void* user, const char* event_json);

/* ---- memory, errors, version ---- */
const char* axllm_c_version(void);
char* axllm_c_string_dup(const char* value);
void axllm_c_string_free(char* value);
const char* axllm_c_error_message(const axllm_c_error* error);
void axllm_c_error_free(axllm_c_error* error);

/* ---- signatures and helpers ---- */
axllm_c_status axllm_c_schema_for_signature(const char* signature, const char* section, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_signature_parse(const char* signature, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_signature_to_string(const char* signature_json, char** out_text, axllm_c_error** out_error);
axllm_c_status axllm_c_to_json_schema(const char* fields_json, const char* title, const char* options_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_validate_output(const char* fields_json, const char* values_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_strip_internal(const char* fields_json, const char* values_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_render_prompt(const char* signature_json, const char* values_json, const char* functions_json, const char* options_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_fold_stream(const char* events_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_supported_ai_models(const char* options_json, char** out_json, axllm_c_error** out_error);

/* ---- process-wide hooks (pass NULL fn to clear) ---- */
void axllm_c_set_usage_observer(axllm_c_observer_fn fn, void* user);
void axllm_c_set_verbose_log_sink(axllm_c_observer_fn fn, void* user);
/* fn(user, {"key":..., "value": <output>|absent}) -> stored output JSON or null on miss. */
void axllm_c_set_caching_function(axllm_c_json_fn fn, void* user);
/* fn(user, <tool result JSON>) -> a JSON string used as the text the model sees (other values are stringified). */
void axllm_c_set_function_result_formatter(axllm_c_json_fn fn, void* user);
/* Markers to place in program options; the callback stays registered for the process. */
axllm_c_status axllm_c_register_agent_observer(axllm_c_observer_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error);
/* fn(user, {"searches":..., "alreadyLoaded":...}) -> results. */
axllm_c_status axllm_c_register_memories_search(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error);
/* fn(user, <searches>) -> results. */
axllm_c_status axllm_c_register_skills_search(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error);

/* ---- AI clients ---- */
/* provider: "openai", "anthropic", "google-gemini", ... as Ax's ai(provider, options). */
axllm_c_status axllm_c_ai_new(const char* provider, const char* options_json, axllm_c_ai** out_ai, axllm_c_error** out_error);
/* A host-implemented client: complete(user, request) -> response. */
axllm_c_status axllm_c_ai_from_callback(const char* name, axllm_c_json_fn complete, void* user, axllm_c_ai** out_ai, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_balancer_new(axllm_c_ai* const* services, size_t count, const char* options_json, axllm_c_ai** out_ai, axllm_c_error** out_error);
/* keys[i] names services[i]; descriptions may be NULL. */
axllm_c_status axllm_c_ai_router_new(const char* const* keys, axllm_c_ai* const* services, const char* const* descriptions, size_t count, axllm_c_ai** out_ai, axllm_c_error** out_error);
void axllm_c_ai_free(axllm_c_ai* ai);
axllm_c_status axllm_c_ai_complete(axllm_c_ai* ai, const char* request_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_chat(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_chat_stream(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_event_fn on_chunk, void* user, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_embed(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_transcribe(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_speak(axllm_c_ai* ai, const char* request_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_id(axllm_c_ai* ai, char** out_text, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_name(axllm_c_ai* ai, char** out_text, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_features(axllm_c_ai* ai, const char* model_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_model_list(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_metrics(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_options(axllm_c_ai* ai, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_set_options(axllm_c_ai* ai, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_ai_get_estimated_cost(axllm_c_ai* ai, const char* model_usage_json, double* out_cost, axllm_c_error** out_error);
/* which: "chat", "embed" or "config". */
axllm_c_status axllm_c_ai_get_last_used(axllm_c_ai* ai, const char* which, char** out_json, axllm_c_error** out_error);

/* ---- cancellation and run control ---- */
axllm_c_cancel* axllm_c_cancel_new(void);
void axllm_c_cancel_free(axllm_c_cancel* cancel);
int axllm_c_cancel_cancel(axllm_c_cancel* cancel, const char* reason);
int axllm_c_cancel_is_cancelled(const axllm_c_cancel* cancel);

axllm_c_run_control* axllm_c_run_control_new(void);
void axllm_c_run_control_free(axllm_c_run_control* control);
/* target NULL means "root". */
axllm_c_status axllm_c_run_control_steer(axllm_c_run_control* control, const char* text, const char* target, axllm_c_error** out_error);
axllm_c_status axllm_c_run_control_set_thinking_budget(axllm_c_run_control* control, const char* level, const char* target, axllm_c_error** out_error);
axllm_c_status axllm_c_run_control_abort(axllm_c_run_control* control, axllm_c_error** out_error);
axllm_c_status axllm_c_run_control_on_event(axllm_c_run_control* control, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error);
/* JSON marker to put in forward options under "runControl". */
axllm_c_status axllm_c_run_control_value(axllm_c_run_control* control, char** out_json, axllm_c_error** out_error);

/* ---- tools ---- */
/* handler(user, args_json) -> result. */
axllm_c_status axllm_c_tool_new(const char* name, const char* description, const char* parameters_json, axllm_c_json_fn handler, void* user, axllm_c_tool** out_tool, axllm_c_error** out_error);
/* Context-aware handler: in_json is {"args":..., "context":{"callId","sessionId","executionPath","eventContext","cancelled"}}. */
axllm_c_status axllm_c_tool_new_with_context(const char* name, const char* description, const char* parameters_json, axllm_c_json_fn handler, void* user, axllm_c_tool** out_tool, axllm_c_error** out_error);
/* mode: "blocking" (default) or "async". */
axllm_c_status axllm_c_tool_set_execution(axllm_c_tool* tool, const char* mode, axllm_c_error** out_error);
axllm_c_status axllm_c_tool_value(const axllm_c_tool* tool, char** out_json, axllm_c_error** out_error);
void axllm_c_tool_free(axllm_c_tool* tool);

/* ---- programs: constructors ---- */
axllm_c_status axllm_c_gen_new(const char* signature, const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_new(const char* signature, const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_new(const char* options_json, axllm_c_program** out_program, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_from_mermaid(const char* mermaid, const char* bindings_json, axllm_c_program** out_program, axllm_c_error** out_error);
void axllm_c_program_free(axllm_c_program* program);
axllm_c_program_kind axllm_c_program_get_kind(const axllm_c_program* program);
/* A JSON reference ({"__agent_stage_id":...}) to use for a program inside flow
 * steps_json / branches_json / mermaid bindings; a flow keeps referenced programs alive. */
axllm_c_status axllm_c_program_ref(axllm_c_program* program, char** out_json, axllm_c_error** out_error);

/* ---- programs: run (any kind) ---- */
axllm_c_status axllm_c_program_forward(axllm_c_program* program, axllm_c_ai* ai, const char* values_json, const char* options_json, axllm_c_cancel* cancel, char** out_json, axllm_c_error** out_error);
/* on_delta gets {"version","index","delta"} (gen/agent) or flow stream events (flow). */
axllm_c_status axllm_c_program_streaming_forward(axllm_c_program* program, axllm_c_ai* ai, const char* values_json, const char* options_json, axllm_c_cancel* cancel, axllm_c_event_fn on_delta, void* user, char** out_json, axllm_c_error** out_error);
/* Gen only: the program's own cancellation token for every run. */
axllm_c_status axllm_c_program_set_cancellation(axllm_c_program* program, axllm_c_cancel* cancel, axllm_c_error** out_error);
/* what: "traces", "chat_log", "usage", "function_call_traces" (gen), "memory" (gen),
 * "value" (gen/flow), "plan" (flow), "mermaid" (flow), "state", "action_log", "trace",
 * "export_trace", "runtime_contract", "policy", "policy_registry", "callable_inventory",
 * "discovery_catalog", "runtime_state", "optimizer_metadata" (agent),
 * "optimizable_components", "instruction". */
axllm_c_status axllm_c_program_get(const axllm_c_program* program, const char* what, char** out_json, axllm_c_error** out_error);

/* ---- programs: configuration ---- */
axllm_c_status axllm_c_program_set_instruction(axllm_c_program* program, const char* instruction_json, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_demos(axllm_c_program* program, const char* demos_json, axllm_c_error** out_error);
/* Gen only below unless stated. */
axllm_c_status axllm_c_program_clear_instruction(axllm_c_program* program, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_examples(axllm_c_program* program, const char* examples_json, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_sample_count(axllm_c_program* program, int sample_count, axllm_c_error** out_error);
/* picker(user, samples_json) -> index as JSON number. */
axllm_c_status axllm_c_program_set_result_picker(axllm_c_program* program, axllm_c_json_fn picker, void* user, axllm_c_error** out_error);
/* Gen: AxGen::add_tool. Agent: also accepted (added as a tool module named by the tool). */
axllm_c_status axllm_c_program_add_tool(axllm_c_program* program, const axllm_c_tool* tool, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_stop_functions(axllm_c_program* program, const char* names_json, axllm_c_error** out_error);
axllm_c_status axllm_c_program_on_function_call(axllm_c_program* program, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error);
/* Declarative assert, e.g. {"field":"answer","contains":"Ax","message":"..."}. */
axllm_c_status axllm_c_program_add_assert(axllm_c_program* program, const char* assertion_json, axllm_c_error** out_error);
/* fn(user, values_json) -> null/true pass, false or message string fail. */
axllm_c_status axllm_c_program_add_assert_fn(axllm_c_program* program, axllm_c_json_fn fn, void* user, const char* message, axllm_c_error** out_error);
axllm_c_status axllm_c_program_add_streaming_assert(axllm_c_program* program, const char* assertion_json, axllm_c_error** out_error);
/* fn(user, {"text":..., "done":bool}) -> null/true pass, false or message fail. */
axllm_c_status axllm_c_program_add_streaming_assert_fn(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, const char* message, axllm_c_error** out_error);
/* op: "uppercase", "lowercase", "trim", "prefix:<text>", "suffix:<text>". */
axllm_c_status axllm_c_program_add_field_transform(axllm_c_program* program, const char* field, const char* op, axllm_c_error** out_error);
axllm_c_status axllm_c_program_add_field_transform_fn(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_error** out_error);
/* fn(user, {"value":..., "values":..., "done":bool}) -> feedback message / new value / null. */
axllm_c_status axllm_c_program_add_field_processor(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_field_processor_mode mode, axllm_c_error** out_error);
axllm_c_status axllm_c_program_add_streaming_field_processor(axllm_c_program* program, const char* field, axllm_c_json_fn fn, void* user, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_caching_function(axllm_c_program* program, axllm_c_json_fn fn, void* user, axllm_c_error** out_error);
axllm_c_status axllm_c_program_set_function_result_formatter(axllm_c_program* program, axllm_c_json_fn fn, void* user, axllm_c_error** out_error);

/* ---- programs: optimization (any kind) ---- */
axllm_c_status axllm_c_program_apply_optimized_components(axllm_c_program* program, const char* component_map_json, axllm_c_error** out_error);
axllm_c_status axllm_c_program_apply_optimization(axllm_c_program* program, const char* artifact_json, axllm_c_error** out_error);
axllm_c_status axllm_c_program_evaluate_optimization(axllm_c_program* program, axllm_c_ai* ai, const char* dataset_json, const char* candidate_map_json, const char* options_json, char** out_json, axllm_c_error** out_error);
/* teacher may be NULL. */
axllm_c_status axllm_c_optimize(axllm_c_program* program, axllm_c_ai* student, const char* dataset_json, const char* options_json, axllm_c_ai* teacher, char** out_json, axllm_c_error** out_error);
/* reflection may be NULL. */
axllm_c_status axllm_c_optimizer_gepa_new(axllm_c_ai* reflection, const char* options_json, axllm_c_optimizer** out_optimizer, axllm_c_error** out_error);
axllm_c_status axllm_c_optimizer_bootstrap_new(const char* options_json, axllm_c_optimizer** out_optimizer, axllm_c_error** out_error);
void axllm_c_optimizer_free(axllm_c_optimizer* optimizer);
/* ai may be NULL. */
axllm_c_status axllm_c_program_optimize_with(axllm_c_program* program, axllm_c_optimizer* optimizer, axllm_c_ai* ai, const char* dataset_json, const char* options_json, char** out_json, axllm_c_error** out_error);

/* ---- playbooks (gen or agent) ---- */
axllm_c_status axllm_c_playbook_new(axllm_c_program* program, axllm_c_ai* student, const char* options_json, axllm_c_ai* teacher, axllm_c_playbook** out_playbook, axllm_c_error** out_error);
void axllm_c_playbook_free(axllm_c_playbook* playbook);
axllm_c_status axllm_c_playbook_evolve(axllm_c_playbook* playbook, const char* dataset_json, const char* options_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_playbook_update(axllm_c_playbook* playbook, const char* args_json, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_playbook_apply(axllm_c_playbook* playbook, axllm_c_error** out_error);
axllm_c_status axllm_c_playbook_render(axllm_c_playbook* playbook, char** out_text, axllm_c_error** out_error);
axllm_c_status axllm_c_playbook_to_json(axllm_c_playbook* playbook, char** out_json, axllm_c_error** out_error);
axllm_c_status axllm_c_playbook_load(axllm_c_playbook* playbook, const char* snapshot_json, axllm_c_error** out_error);

/* ---- agents ---- */
axllm_c_status axllm_c_agent_set_signature(axllm_c_program* agent, const char* signature, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_add_actor_instruction(axllm_c_program* agent, const char* addendum_json, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_add_tool_module(axllm_c_program* agent, const char* name, const axllm_c_tool* const* tools, size_t count, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_add_child_agent(axllm_c_program* agent, const char* namespace_name, const char* name, axllm_c_program* child, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_set_state(axllm_c_program* agent, const char* state_json, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_set_citations_observer(axllm_c_program* agent, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error);
axllm_c_status axllm_c_agent_set_playbook_observer(axllm_c_program* agent, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error);
/* op: "discover", "recall", "inspect_runtime", "export_session_state", "restore_session_state",
 * "close_runtime_session", "restore_runtime_state", "replay_trace" ({"trace","fixtures"}),
 * "used" ({"id","reason","stage"}), "invoke_callable" ({"name","args","options"}),
 * "optimize" ({"dataset","options"}), "evaluate_optimization_task" (needs ai: {"task","options"}). */
axllm_c_status axllm_c_agent_call(axllm_c_program* agent, const char* op, const char* args_json, axllm_c_ai* ai, char** out_json, axllm_c_error** out_error);

/* ---- flows ---- */
/* step: the program to run; options may be NULL. */
axllm_c_status axllm_c_flow_execute(axllm_c_program* flow, const char* name, axllm_c_program* step, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_derive(axllm_c_program* flow, const char* name, axllm_c_program* step, const char* options_json, axllm_c_error** out_error);
/* mapper(user, state_json) -> new state fields. */
axllm_c_status axllm_c_flow_map(axllm_c_program* flow, const char* name, axllm_c_json_fn mapper, void* user, const char* options_json, axllm_c_error** out_error);
/* predicate(user, state) -> branch key; branches_json maps keys to step arrays. */
axllm_c_status axllm_c_flow_branch(axllm_c_program* flow, const char* name, axllm_c_json_fn predicate, void* user, const char* branches_json, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_while(axllm_c_program* flow, const char* name, axllm_c_json_fn condition, void* user, const char* steps_json, int max_iterations, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_feedback(axllm_c_program* flow, const char* name, axllm_c_json_fn condition, void* user, const char* steps_json, int max_iterations, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_node_extended(axllm_c_program* flow, const char* name, const char* base_signature, const char* extensions_json, const char* options_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_parallel(axllm_c_program* flow, const char* steps_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_returns(axllm_c_program* flow, const char* spec_json, axllm_c_error** out_error);
axllm_c_status axllm_c_flow_add_raw_step(axllm_c_program* flow, const char* step_json, axllm_c_error** out_error);
/* JSON marker for a callback usable inside steps_json / branches_json / mermaid bindings. */
axllm_c_status axllm_c_flow_callback(axllm_c_json_fn fn, void* user, char** out_marker_json, axllm_c_error** out_error);

/* ---- MCP ---- */
/* transport_json: {"type":"http","endpoint":...,"options":{...}} | {"type":"stdio","command":...,"args":[...]}
 *                 | {"type":"scripted","responses":[...]}. */
axllm_c_status axllm_c_mcp_client_new(const char* transport_json, const char* options_json, axllm_c_mcp_client** out_client, axllm_c_error** out_error);
void axllm_c_mcp_client_free(axllm_c_mcp_client* client);
axllm_c_status axllm_c_mcp_client_init(axllm_c_mcp_client* client, axllm_c_error** out_error);
axllm_c_status axllm_c_mcp_client_close(axllm_c_mcp_client* client, axllm_c_error** out_error);
/* method: "ping", "list_tools", "call_tool", "call_tool_outcome", "list_prompts", "get_prompt",
 * "list_resources", "read_resource", "subscribe_resource", "unsubscribe_resource",
 * "list_resource_templates", "get_task", "cancel_task", "list_tasks", "get_task_result",
 * "provide_task_input", "notify", "request", "discover", "refresh", "inspect_catalog",
 * "protocol_version", "namespace", "prompts", "resources", "resource_templates", "era".
 * args_json carries the named arguments, e.g. {"name":..., "arguments":{...}, "cursor":..., "uri":...}. */
axllm_c_status axllm_c_mcp_client_call(axllm_c_mcp_client* client, const char* method, const char* args_json, char** out_json, axllm_c_error** out_error);
/* Adds the client's tools (prompts/resources as functions) to a gen or agent program. */
axllm_c_status axllm_c_mcp_client_attach(axllm_c_mcp_client* client, axllm_c_program* program, axllm_c_error** out_error);
/* fn(user, {"params":..., "context":{"client","namespace"}}) -> elicitation result. */
axllm_c_status axllm_c_mcp_client_set_elicitation_handler(axllm_c_mcp_client* client, axllm_c_json_fn fn, void* user, axllm_c_error** out_error);
axllm_c_status axllm_c_mcp_client_add_notification_listener(axllm_c_mcp_client* client, axllm_c_observer_fn fn, void* user, axllm_c_error** out_error);

#ifdef __cplusplus
}
#endif

#endif
