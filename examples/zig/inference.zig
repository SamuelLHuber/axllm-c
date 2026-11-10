//! Runs an AxGen program through the C ABI against an OpenAI-compatible
//! endpoint. AXLLM_C_BASE_URL selects the endpoint (e.g. a local mock),
//! OPENAI_API_KEY the key and AXLLM_C_MODEL the model.
const std = @import("std");

const c = @cImport({
    @cInclude("stdlib.h");
    @cInclude("axllm/axllm_c.h");
});

const AxError = error{AxllmError};

fn check(status: c.axllm_c_status, err: ?*c.axllm_c_error) AxError!void {
    if (status == c.AXLLM_C_OK) return;
    const message = if (err) |e| std.mem.span(c.axllm_c_error_message(e)) else "unknown axllm error";
    std.debug.print("axllm error: {s}\n", .{message});
    if (err) |e| c.axllm_c_error_free(e);
    return error.AxllmError;
}

fn env(name: [*:0]const u8, fallback: []const u8) []const u8 {
    const value = c.getenv(name);
    if (value == null) return fallback;
    return std.mem.span(value);
}

fn onDelta(user: ?*anyopaque, event: [*c]const u8) callconv(.c) c_int {
    const count: *u32 = @ptrCast(@alignCast(user.?));
    count.* += 1;
    _ = event;
    return 1;
}

pub fn main() !void {
    var buffer: [1024]u8 = undefined;
    const options = try std.fmt.bufPrintZ(&buffer, "{{\"api_key\":\"{s}\",\"model\":\"{s}\",\"base_url\":\"{s}\"}}", .{
        env("OPENAI_API_KEY", "test"),
        env("AXLLM_C_MODEL", "gpt-5-mini"),
        env("AXLLM_C_BASE_URL", "https://api.openai.com/v1"),
    });

    var err: ?*c.axllm_c_error = null;
    var ai: ?*c.axllm_c_ai = null;
    try check(c.axllm_c_ai_new("openai", options.ptr, &ai, &err), err);
    defer c.axllm_c_ai_free(ai);

    var program: ?*c.axllm_c_program = null;
    try check(c.axllm_c_gen_new("question:string -> answer:string", null, &program, &err), err);
    defer c.axllm_c_program_free(program);

    var out: [*c]u8 = null;
    try check(c.axllm_c_program_forward(program, ai, "{\"question\":\"What is the capital of France?\"}", null, null, &out, &err), err);
    defer c.axllm_c_string_free(out);
    const answer = std.mem.span(out);
    if (std.mem.indexOf(u8, answer, "Paris") == null) return error.UnexpectedAnswer;

    var deltas: u32 = 0;
    var streamed: [*c]u8 = null;
    try check(c.axllm_c_program_streaming_forward(program, ai, "{\"question\":\"What is the capital of France?\"}", null, null, onDelta, &deltas, &streamed, &err), err);
    defer c.axllm_c_string_free(streamed);
    if (deltas == 0) return error.NoStreamDeltas;

    std.debug.print("zig inference via axllm-c: {s} (streamed {d} deltas)\n", .{ answer, deltas });
}
