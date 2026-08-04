import { describe, expect, it } from 'vitest';

import { AxMockAIService } from '../ai/mock/api.js';
import type { AxChatRequest, AxChatResponse } from '../ai/types.js';
import { AxGen } from './generate.js';
import type { AxStepHooks } from './types.js';

/**
 * Helper: create a non-streaming AxChatResponse with content.
 */
function textResponse(content: string): AxChatResponse {
  return {
    results: [{ index: 0, content, finishReason: 'stop' as const }],
    modelUsage: {
      ai: 'mock',
      model: 'mock-model',
      tokens: { promptTokens: 10, completionTokens: 5, totalTokens: 15 },
    },
  };
}

/**
 * Helper: create a non-streaming AxChatResponse with a function call.
 */
function functionCallResponse(
  funcName: string,
  params: Record<string, unknown> = {}
): AxChatResponse {
  return {
    results: [
      {
        index: 0,
        functionCalls: [
          {
            id: '1',
            type: 'function' as const,
            function: { name: funcName, params: JSON.stringify(params) },
          },
        ],
        finishReason: 'stop' as const,
      },
    ],
    modelUsage: {
      ai: 'mock',
      model: 'mock-model',
      tokens: { promptTokens: 10, completionTokens: 5, totalTokens: 15 },
    },
  };
}

/**
 * Extract the system message content from a chatPrompt array.
 */
function getSystemContent(
  chatPrompt: AxChatRequest['chatPrompt']
): string | undefined {
  const sys = chatPrompt.find((m) => m.role === 'system');
  if (!sys) return undefined;
  return typeof sys.content === 'string'
    ? sys.content
    : Array.isArray(sys.content)
      ? sys.content.map((c) => ('text' in c ? c.text : '')).join('')
      : undefined;
}

describe('System prompt refresh after ctx.addFunctions()', () => {
  it('system prompt includes dynamically added function on the next step', async () => {
    let callCount = 0;
    const capturedSystemContents: (string | undefined)[] = [];

    const ai = new AxMockAIService({
      features: { functions: true },
    });

    // Override chat to capture the system prompt on each API call
    ai.chat = async (req: Readonly<AxChatRequest<unknown>>) => {
      callCount++;
      capturedSystemContents.push(getSystemContent(req.chatPrompt));

      if (callCount === 1) {
        // Step 0: LLM calls search_tools
        return functionCallResponse('search_tools', { query: 'database' });
      }
      // Step 1: LLM produces final answer
      return textResponse('Answer: done');
    };

    const gen = new AxGen('question:string -> answer:string', {
      functions: [
        {
          name: 'search_tools',
          description: 'Search for tools',
          func: () => 'Found: new_database_tool',
        },
      ],
    });

    const stepHooks: AxStepHooks = {
      afterFunctionExecution: (ctx) => {
        if (ctx.functionsExecuted.has('search_tools')) {
          ctx.addFunctions([
            {
              name: 'new_database_tool',
              description: 'A dynamically discovered database tool',
              parameters: {
                type: 'object' as const,
                properties: {
                  query: { type: 'string', description: 'The query' },
                },
              },
              func: async () => 'query result',
            },
          ]);
        }
      },
    };

    await gen.forward(ai as any, { question: 'test' }, { stepHooks });

    // Step 0: system prompt should list only search_tools
    expect(capturedSystemContents[0]).toBeDefined();
    expect(capturedSystemContents[0]).toContain('search_tools');
    expect(capturedSystemContents[0]).not.toContain('new_database_tool');

    // Step 1: system prompt should now also list new_database_tool
    expect(capturedSystemContents[1]).toBeDefined();
    expect(capturedSystemContents[1]).toContain('new_database_tool');
    expect(capturedSystemContents[1]).toContain('search_tools');
  });

  it('system prompt reflects function removal on the next step', async () => {
    let callCount = 0;
    const capturedSystemContents: (string | undefined)[] = [];

    const ai = new AxMockAIService({
      features: { functions: true },
    });

    ai.chat = async (req: Readonly<AxChatRequest<unknown>>) => {
      callCount++;
      capturedSystemContents.push(getSystemContent(req.chatPrompt));

      if (callCount === 1) {
        return functionCallResponse('toolA', {});
      }
      return textResponse('Answer: done');
    };

    const gen = new AxGen('question:string -> answer:string', {
      functions: [
        { name: 'toolA', description: 'Tool Alpha', func: () => 'A' },
        { name: 'toolB', description: 'Tool Beta', func: () => 'B' },
      ],
    });

    const stepHooks: AxStepHooks = {
      afterFunctionExecution: (ctx) => {
        // Remove toolB after step 0
        ctx.removeFunctions('toolB');
      },
    };

    await gen.forward(ai as any, { question: 'test' }, { stepHooks });

    // Step 0: both tools in system prompt
    expect(capturedSystemContents[0]).toContain('toolA');
    expect(capturedSystemContents[0]).toContain('toolB');

    // Step 1: toolB should be gone from system prompt
    expect(capturedSystemContents[1]).toContain('toolA');
    expect(capturedSystemContents[1]).not.toContain('toolB');
  });

  it('system prompt is unchanged when no function mutations occur', async () => {
    let callCount = 0;
    const capturedSystemContents: (string | undefined)[] = [];

    const ai = new AxMockAIService({
      features: { functions: true },
    });

    ai.chat = async (req: Readonly<AxChatRequest<unknown>>) => {
      callCount++;
      capturedSystemContents.push(getSystemContent(req.chatPrompt));

      if (callCount === 1) {
        return functionCallResponse('myTool', {});
      }
      return textResponse('Answer: done');
    };

    const gen = new AxGen('question:string -> answer:string', {
      functions: [
        { name: 'myTool', description: 'My tool', func: () => 'result' },
      ],
    });

    await gen.forward(ai as any, { question: 'test' });

    // Both steps should have the same system prompt
    expect(capturedSystemContents[0]).toBeDefined();
    expect(capturedSystemContents[1]).toBeDefined();
    expect(capturedSystemContents[0]).toEqual(capturedSystemContents[1]);
  });

  it('multiple addFunctions calls across steps accumulate in system prompt', async () => {
    let callCount = 0;
    const capturedSystemContents: (string | undefined)[] = [];

    const ai = new AxMockAIService({
      features: { functions: true },
    });

    ai.chat = async (req: Readonly<AxChatRequest<unknown>>) => {
      callCount++;
      capturedSystemContents.push(getSystemContent(req.chatPrompt));

      if (callCount <= 2) {
        // Steps 0 and 1: call discover to trigger more function additions
        return functionCallResponse('discover', { step: callCount });
      }
      return textResponse('Answer: all done');
    };

    const gen = new AxGen('question:string -> answer:string', {
      functions: [
        { name: 'discover', description: 'Discover tools', func: () => 'ok' },
      ],
    });

    const stepHooks: AxStepHooks = {
      afterFunctionExecution: (ctx) => {
        if (ctx.stepIndex === 0) {
          ctx.addFunctions([
            {
              name: 'toolA',
              description: 'Tool A added in step 0',
              func: async () => 'A',
            },
          ]);
        } else if (ctx.stepIndex === 1) {
          ctx.addFunctions([
            {
              name: 'toolB',
              description: 'Tool B added in step 1',
              func: async () => 'B',
            },
          ]);
        }
      },
    };

    await gen.forward(ai as any, { question: 'test' }, { stepHooks });

    // Step 0: only discover
    expect(capturedSystemContents[0]).toContain('discover');
    expect(capturedSystemContents[0]).not.toContain('toolA');
    expect(capturedSystemContents[0]).not.toContain('toolB');

    // Step 1: discover + toolA
    expect(capturedSystemContents[1]).toContain('discover');
    expect(capturedSystemContents[1]).toContain('toolA');
    expect(capturedSystemContents[1]).not.toContain('toolB');

    // Step 2: discover + toolA + toolB
    expect(capturedSystemContents[2]).toContain('discover');
    expect(capturedSystemContents[2]).toContain('toolA');
    expect(capturedSystemContents[2]).toContain('toolB');
  });

  it('dynamically added functions appear in API tool definitions AND system prompt', async () => {
    let callCount = 0;
    const capturedFunctions: (string[] | undefined)[] = [];
    const capturedSystemContents: (string | undefined)[] = [];

    const ai = new AxMockAIService({
      features: { functions: true },
    });

    ai.chat = async (req: Readonly<AxChatRequest<unknown>>) => {
      callCount++;
      capturedSystemContents.push(getSystemContent(req.chatPrompt));
      capturedFunctions.push(req.functions?.map((f) => f.name));

      if (callCount === 1) {
        return functionCallResponse('search', {});
      }
      return textResponse('Answer: done');
    };

    const gen = new AxGen('question:string -> answer:string', {
      functions: [
        { name: 'search', description: 'Search', func: () => 'found' },
      ],
    });

    const stepHooks: AxStepHooks = {
      afterFunctionExecution: (ctx) => {
        ctx.addFunctions([
          {
            name: 'execute_query',
            description: 'Execute a database query',
            func: async () => 'results',
          },
        ]);
      },
    };

    await gen.forward(ai as any, { question: 'test' }, { stepHooks });

    // Step 1: both API tool definitions and system prompt should include execute_query
    expect(capturedFunctions[1]).toContain('execute_query');
    expect(capturedSystemContents[1]).toContain('execute_query');
  });
});
