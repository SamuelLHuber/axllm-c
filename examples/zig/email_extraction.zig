//! Email information extraction with Ax, driven from Zig through the axllm-c ABI.
//!
//! An Ax port of the DSPy tutorial https://dspy.ai/tutorials/email_extraction/:
//! four typed Ax programs (classify -> extract -> summarize -> act) run against
//! a real model and are composed here, like the tutorial's EmailProcessor.
//!
//!   OPENAI_API_KEY=... zig run examples/zig/email_extraction.zig -I dist \
//!     -DAXLLM_ENABLE_CURL=1 dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp \
//!     dist/axllm/mcp.cpp -lc++ -lcurl
//!
//! AXLLM_C_MODEL picks the model; AXLLM_C_BASE_URL any OpenAI-compatible endpoint.
const std = @import("std");

const c = @cImport({
    @cInclude("stdlib.h");
    @cInclude("axllm/axllm_c.h");
});

const json = std.json;
const print = std.debug.print;

const classify_signature =
    \\"Classify the type and urgency of an email based on its content."
    \\emailSubject:string "The subject line of the email",
    \\emailBody:string "The main content of the email",
    \\sender:string "Email sender information"
    \\->
    \\emailType:class "order_confirmation, support_request, meeting_invitation, newsletter, promotional, invoice, shipping_notification, other" "The classified type of email",
    \\urgency:class "low, medium, high, critical" "The urgency level of the email",
    \\reasoning:string "Brief explanation of the classification"
;

const extract_signature =
    \\"Extract key entities and information from email content."
    \\emailContent:string "The full email content including subject and body",
    \\emailType:string "The classified type of email"
    \\->
    \\keyEntities:json "List of extracted entities, each an object with entityType, value and confidence (0-1)",
    \\financialAmount?:number "Any monetary amount found, as a number (e.g. 99.99)",
    \\importantDates:string[] "List of important dates found in the email",
    \\contactInfo:string[] "Relevant contact information extracted"
;

const summarize_signature =
    \\"Create a concise summary of the email content."
    \\emailSubject:string,
    \\emailBody:string,
    \\keyEntities:json "Key entities found in the email"
    \\->
    \\summary:string "A 2-3 sentence summary of the email's main points"
;

const actions_signature =
    \\"Determine what actions are needed based on the email content and extracted information."
    \\emailType:string,
    \\urgency:string,
    \\emailSummary:string "Brief summary of the email content",
    \\extractedEntities:json "Key entities found in the email"
    \\->
    \\actionRequired:boolean "Whether any action is required",
    \\actionItems:string[] "List of specific actions needed",
    \\deadline?:string "Deadline for action if applicable",
    \\priorityScore:number "Priority score from 1-10"
;

const Email = struct { subject: []const u8, body: []const u8, sender: []const u8 };

const sample_emails = [_]Email{
    .{
        .subject = "Order Confirmation #12345 - Your MacBook Pro is on the way!",
        .body =
        \\Dear John Smith,
        \\
        \\Thank you for your order! We're excited to confirm that your order #12345 has been processed.
        \\
        \\Order Details:
        \\- MacBook Pro 14-inch (Space Gray)
        \\- Order Total: $2,399.00
        \\- Estimated Delivery: December 15, 2024
        \\- Tracking Number: 1Z999AA1234567890
        \\
        \\If you have any questions, please contact our support team at support@techstore.com.
        \\
        \\Best regards,
        \\TechStore Team
        ,
        .sender = "orders@techstore.com",
    },
    .{
        .subject = "URGENT: Server Outage - Immediate Action Required",
        .body =
        \\Hi DevOps Team,
        \\
        \\We're experiencing a critical server outage affecting our production environment.
        \\
        \\Impact: All users unable to access the platform
        \\Started: 2:30 PM EST
        \\
        \\Please join the emergency call immediately: +1-555-123-4567
        \\
        \\This is our highest priority.
        \\
        \\Thanks,
        \\Site Reliability Team
        ,
        .sender = "alerts@company.com",
    },
    .{
        .subject = "Meeting Invitation: Q4 Planning Session",
        .body =
        \\Hello team,
        \\
        \\You're invited to our Q4 planning session.
        \\
        \\When: Friday, December 20, 2024 at 2:00 PM - 4:00 PM EST
        \\Where: Conference Room A
        \\
        \\Please confirm your attendance by December 18th.
        \\
        \\Best,
        \\Sarah Johnson
        ,
        .sender = "sarah.johnson@company.com",
    },
};

// ANSI styling.
const reset = "\x1b[0m";
const bold = "\x1b[1m";
const dim = "\x1b[2m";
const cyan = "\x1b[36m";
const green = "\x1b[32m";
const yellow = "\x1b[33m";
const red = "\x1b[31m";
const magenta = "\x1b[35m";
const white_on_red = "\x1b[1;97;41m";

fn check(status: c.axllm_c_status, err: ?*c.axllm_c_error) !void {
    if (status == c.AXLLM_C_OK) return;
    const message = if (err) |e| std.mem.span(c.axllm_c_error_message(e)) else "unknown axllm error";
    print("{s}axllm error:{s} {s}\n", .{ red, reset, message });
    if (err) |e| c.axllm_c_error_free(e);
    return error.AxllmError;
}

fn env(name: [*:0]const u8, fallback: []const u8) []const u8 {
    const value = c.getenv(name);
    if (value == null) return fallback;
    const span = std.mem.span(value);
    return if (span.len == 0) fallback else span;
}

const Program = struct {
    handle: *c.axllm_c_program,

    fn init(signature: [:0]const u8) !Program {
        var err: ?*c.axllm_c_error = null;
        var handle: ?*c.axllm_c_program = null;
        try check(c.axllm_c_gen_new(signature.ptr, null, &handle, &err), err);
        return .{ .handle = handle.? };
    }

    fn deinit(self: Program) void {
        c.axllm_c_program_free(self.handle);
    }

    /// Serializes `input`, runs the program and parses its JSON output into `arena`.
    fn forward(self: Program, arena: std.mem.Allocator, ai: *c.axllm_c_ai, input: anytype) !json.ObjectMap {
        const input_json = try json.Stringify.valueAlloc(arena, input, .{});
        const input_z = try arena.dupeZ(u8, input_json);
        var err: ?*c.axllm_c_error = null;
        var out: [*c]u8 = null;
        try check(c.axllm_c_program_forward(self.handle, ai, input_z.ptr, null, null, &out, &err), err);
        defer c.axllm_c_string_free(out);
        const parsed = try json.parseFromSliceLeaky(json.Value, arena, std.mem.span(out), .{ .allocate = .alloc_always });
        if (parsed != .object) return error.UnexpectedOutput;
        return parsed.object;
    }
};

const EmailProcessor = struct {
    classifier: Program,
    entity_extractor: Program,
    summarizer: Program,
    action_generator: Program,

    fn init() !EmailProcessor {
        return .{
            .classifier = try Program.init(classify_signature),
            .entity_extractor = try Program.init(extract_signature),
            .summarizer = try Program.init(summarize_signature),
            .action_generator = try Program.init(actions_signature),
        };
    }

    fn deinit(self: EmailProcessor) void {
        self.classifier.deinit();
        self.entity_extractor.deinit();
        self.summarizer.deinit();
        self.action_generator.deinit();
    }

    const Result = struct { classification: json.ObjectMap, entities: json.ObjectMap, summary: json.ObjectMap, actions: json.ObjectMap };

    fn forward(self: EmailProcessor, arena: std.mem.Allocator, ai: *c.axllm_c_ai, email: Email) !Result {
        const classification = try self.classifier.forward(arena, ai, .{
            .emailSubject = email.subject,
            .emailBody = email.body,
            .sender = email.sender,
        });
        const email_type = text(classification, "emailType");
        const full_content = try std.fmt.allocPrint(arena, "Subject: {s}\n\nFrom: {s}\n\n{s}", .{ email.subject, email.sender, email.body });
        const entities = try self.entity_extractor.forward(arena, ai, .{
            .emailContent = full_content,
            .emailType = email_type,
        });
        const key_entities = entities.get("keyEntities") orelse json.Value{ .array = .init(arena) };
        const summary = try self.summarizer.forward(arena, ai, .{
            .emailSubject = email.subject,
            .emailBody = email.body,
            .keyEntities = key_entities,
        });
        const actions = try self.action_generator.forward(arena, ai, .{
            .emailType = email_type,
            .urgency = text(classification, "urgency"),
            .emailSummary = text(summary, "summary"),
            .extractedEntities = key_entities,
        });
        return .{ .classification = classification, .entities = entities, .summary = summary, .actions = actions };
    }
};

fn text(object: json.ObjectMap, key: []const u8) []const u8 {
    const value = object.get(key) orelse return "";
    return switch (value) {
        .string => |s| std.mem.trim(u8, s, "\" "),
        else => "",
    };
}

fn number(object: json.ObjectMap, key: []const u8) ?f64 {
    const value = object.get(key) orelse return null;
    return switch (value) {
        .float => |f| f,
        .integer => |i| @floatFromInt(i),
        .number_string => |s| std.fmt.parseFloat(f64, s) catch null,
        else => null,
    };
}

fn urgencyStyle(urgency: []const u8) []const u8 {
    if (std.mem.eql(u8, urgency, "critical")) return white_on_red;
    if (std.mem.eql(u8, urgency, "high")) return red ++ bold;
    if (std.mem.eql(u8, urgency, "medium")) return yellow ++ bold;
    return green ++ bold;
}

fn money(amount: f64) void {
    const cents: u64 = @intFromFloat(@round(@abs(amount) * 100));
    const whole = cents / 100;
    var groups: [8]u64 = undefined;
    var count: usize = 0;
    var rest = whole;
    while (true) {
        groups[count] = rest % 1000;
        count += 1;
        rest /= 1000;
        if (rest == 0 or count == groups.len) break;
    }
    print("{s}${d}", .{ if (amount < 0) "-" else "", groups[count - 1] });
    var i = count - 1;
    while (i > 0) {
        i -= 1;
        print(",{d:0>3}", .{groups[i]});
    }
    print(".{d:0>2}", .{cents % 100});
}

fn rule() void {
    print("{s}  ──────────────────────────────────────────────────────────────────────{s}\n", .{ dim, reset });
}

/// Prints `body` word-wrapped at `width` columns, each line prefixed by `indent`.
fn wrapped(indent: []const u8, body: []const u8, width: usize) void {
    var column: usize = 0;
    var words = std.mem.tokenizeAny(u8, body, " \n\t");
    print("{s}", .{indent});
    while (words.next()) |word| {
        if (column > 0 and column + 1 + word.len > width) {
            print("{s}\n{s}", .{ reset, indent });
            column = 0;
        }
        if (column > 0) {
            print(" ", .{});
            column += 1;
        }
        print("{s}", .{word});
        column += word.len;
    }
    print("{s}\n", .{reset});
}

fn strings(object: json.ObjectMap, key: []const u8) []const json.Value {
    const value = object.get(key) orelse return &.{};
    return switch (value) {
        .array => |a| a.items,
        else => &.{},
    };
}

fn render(index: usize, email: Email, result: EmailProcessor.Result) void {
    const urgency = text(result.classification, "urgency");
    print("\n{s}{s}  EMAIL {d}{s}  {s}{s}{s}\n", .{ bold, cyan, index + 1, reset, bold, email.subject, reset });
    print("{s}  from {s}{s}\n", .{ dim, email.sender, reset });
    rule();
    print("  {s}type{s}       {s}{s}{s}\n", .{ dim, reset, magenta, text(result.classification, "emailType"), reset });
    print("  {s}urgency{s}    {s} {s} {s}\n", .{ dim, reset, urgencyStyle(urgency), urgency, reset });
    if (number(result.actions, "priorityScore")) |score| {
        const filled: usize = @intFromFloat(std.math.clamp(@round(score), 0, 10));
        print("  {s}priority{s}   {s}", .{ dim, reset, urgencyStyle(urgency) });
        for (0..10) |i| print("{s}", .{if (i < filled) "█" else "░"});
        print("{s} {d}/10\n", .{ reset, filled });
    }
    if (number(result.entities, "financialAmount")) |amount| {
        print("  {s}amount{s}     {s}", .{ dim, reset, bold });
        money(amount);
        print("{s}\n", .{reset});
    }
    for (strings(result.entities, "importantDates"), 0..) |date, i| {
        if (date == .string) print("  {s}{s}{s}       {s}\n", .{ dim, if (i == 0) "dates" else "     ", reset, date.string });
    }
    for (strings(result.entities, "contactInfo"), 0..) |contact, i| {
        if (contact == .string) print("  {s}{s}{s}    {s}\n", .{ dim, if (i == 0) "contact" else "       ", reset, contact.string });
    }
    print("\n  {s}summary{s}\n", .{ bold, reset });
    wrapped("    ", text(result.summary, "summary"), 66);
    print("  {s}why{s}\n", .{ bold, reset });
    wrapped("    " ++ dim, text(result.classification, "reasoning"), 66);

    const action_required = if (result.actions.get("actionRequired")) |v| v == .bool and v.bool else false;
    if (action_required) {
        print("  {s}{s}action required{s}", .{ bold, yellow, reset });
        const deadline = text(result.actions, "deadline");
        if (deadline.len > 0) print("  {s}deadline:{s} {s}", .{ dim, reset, deadline });
        print("\n", .{});
        for (strings(result.actions, "actionItems")) |item| {
            if (item == .string) print("    {s}▸{s} {s}\n", .{ yellow, reset, item.string });
        }
    } else {
        print("  {s}no action required{s}\n", .{ green, reset });
    }
}

pub fn main() !void {
    const key = env("OPENAI_API_KEY", "");
    if (key.len == 0) {
        print("Set OPENAI_API_KEY to run this example.\n", .{});
        return error.MissingApiKey;
    }
    const model = env("AXLLM_C_MODEL", "gpt-5.4-mini");

    var arena_state = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const Options = struct { api_key: []const u8, model: []const u8, base_url: ?[]const u8 };
    const base_url = env("AXLLM_C_BASE_URL", "");
    const options = try json.Stringify.valueAlloc(arena, Options{
        .api_key = key,
        .model = model,
        .base_url = if (base_url.len == 0) null else base_url,
    }, .{ .emit_null_optional_fields = false });

    var err: ?*c.axllm_c_error = null;
    var ai: ?*c.axllm_c_ai = null;
    try check(c.axllm_c_ai_new("openai", (try arena.dupeZ(u8, options)).ptr, &ai, &err), err);
    defer c.axllm_c_ai_free(ai);

    const processor = try EmailProcessor.init();
    defer processor.deinit();

    print("\n{s}{s}  Email Processing Demo{s}  {s}Ax {s} · {s} · Zig via axllm-c{s}\n", .{ bold, cyan, reset, dim, c.axllm_c_version(), model, reset });
    print("{s}  ══════════════════════════════════════════════════════════════════════{s}\n", .{ cyan, reset });

    for (sample_emails, 0..) |email, i| {
        const result = try processor.forward(arena, ai.?, email);
        render(i, email, result);
    }
    print("\n", .{});
}
