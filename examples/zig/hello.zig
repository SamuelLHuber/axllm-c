const std = @import("std");

const ax = @cImport({
    @cInclude("axllm/axllm_c.h");
});

pub fn main() !void {
    var json: [*c]u8 = null;
    var err: ?*ax.axllm_c_error = null;

    const status = ax.axllm_c_schema_for_signature(
        "question:string -> answer:string",
        "outputs",
        &json,
        &err,
    );
    defer if (err) |e| ax.axllm_c_error_free(e);

    if (status != ax.AXLLM_C_OK) {
        const message = if (err) |e| std.mem.span(ax.axllm_c_error_message(e)) else "unknown axllm error";
        std.debug.print("axllm error: {s}\n", .{message});
        return error.AxllmError;
    }
    defer ax.axllm_c_string_free(json);

    const schema = std.mem.span(json);
    if (std.mem.indexOf(u8, schema, "answer") == null) return error.MissingAnswerField;

    std.debug.print("hello from Zig + axllm-c: {s}\n", .{schema});
}
