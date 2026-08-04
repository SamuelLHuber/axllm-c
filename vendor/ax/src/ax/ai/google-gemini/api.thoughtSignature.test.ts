import { describe, expect, it, vi } from 'vitest';

import { AxAIGoogleGemini } from './api.js';
import { AxAIGoogleGeminiModel } from './types.js';

// Utility to create a fake fetch that returns a canned response and captures the request body
function createMockFetch(body: unknown, capture: { lastBody?: any }) {
  return vi
    .fn()
    .mockImplementation(async (_url: RequestInfo | URL, init?: RequestInit) => {
      try {
        if (init?.body && typeof init.body === 'string') {
          capture.lastBody = JSON.parse(init.body);
        }
      } catch {}
      return new Response(JSON.stringify(body), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      });
    });
}

describe('thoughtSignature round-tripping for Gemini 3 models', () => {
  // ---------------------------------------------------------------------------
  // 1. Parse thoughtSignature from a function call response
  // ---------------------------------------------------------------------------
  it('parses thoughtSignature from a functionCall response part into thoughtBlocks', async () => {
    const ai = new AxAIGoogleGemini({
      apiKey: 'test-key',
      config: { model: AxAIGoogleGeminiModel.Gemini3Pro },
      models: [],
    });

    const capture: { lastBody?: any } = {};
    const fetch = createMockFetch(
      {
        candidates: [
          {
            content: {
              parts: [
                {
                  functionCall: { name: 'getWeather', args: { city: 'NYC' } },
                  thoughtSignature: 'abc-sig-xyz',
                },
              ],
            },
            finishReason: 'STOP',
          },
        ],
        usageMetadata: {
          promptTokenCount: 10,
          candidatesTokenCount: 5,
          totalTokenCount: 15,
          thoughtsTokenCount: 0,
        },
      },
      capture
    );

    ai.setOptions({ fetch });

    const res = await ai.chat(
      {
        chatPrompt: [{ role: 'user', content: 'What is the weather in NYC?' }],
      },
      { stream: false }
    );

    // The parsed result must surface the function call
    expect(res.results[0]?.functionCalls).toHaveLength(1);
    expect(res.results[0]?.functionCalls?.[0].function.name).toBe('getWeather');

    // The parsed result must have thoughtBlocks carrying the signature
    expect(res.results[0]?.thoughtBlocks).toBeDefined();
    expect(res.results[0]?.thoughtBlocks).toHaveLength(1);
    expect(res.results[0]?.thoughtBlocks?.[0]?.signature).toBe('abc-sig-xyz');
  });

  // ---------------------------------------------------------------------------
  // 2. Round-trip: thoughtSignature flows back into the next request
  // ---------------------------------------------------------------------------
  it('sends thought_signature on the functionCall part in a follow-up request', async () => {
    const ai = new AxAIGoogleGemini({
      apiKey: 'test-key',
      config: { model: AxAIGoogleGeminiModel.Gemini3Pro },
      models: [],
    });

    // First response: model calls a function with a signature
    const firstResponse = {
      candidates: [
        {
          content: {
            parts: [
              {
                functionCall: { name: 'getWeather', args: { city: 'NYC' } },
                thoughtSignature: 'round-trip-sig-001',
              },
            ],
          },
          finishReason: 'STOP',
        },
      ],
      usageMetadata: {
        promptTokenCount: 10,
        candidatesTokenCount: 5,
        totalTokenCount: 15,
        thoughtsTokenCount: 0,
      },
    };

    const capture: { lastBody?: any } = {};
    const fetch = createMockFetch(firstResponse, capture);
    ai.setOptions({ fetch });

    // Step 1: get the function call + signature
    const res = await ai.chat(
      { chatPrompt: [{ role: 'user', content: 'weather?' }] },
      { stream: false }
    );

    const functionCalls = res.results[0]?.functionCalls;
    const thoughtBlocks = res.results[0]?.thoughtBlocks;

    expect(functionCalls).toHaveLength(1);
    expect(thoughtBlocks?.[0]?.signature).toBe('round-trip-sig-001');

    // Step 2: build history including assistant message with thoughtBlocks, then send function result
    // Replace the mock to return a plain text response for the second call
    const secondResponse = {
      candidates: [
        {
          content: { parts: [{ text: 'It is sunny in NYC.' }] },
          finishReason: 'STOP',
        },
      ],
      usageMetadata: {
        promptTokenCount: 20,
        candidatesTokenCount: 8,
        totalTokenCount: 28,
        thoughtsTokenCount: 0,
      },
    };

    const capture2: { lastBody?: any } = {};
    const fetch2 = createMockFetch(secondResponse, capture2);
    ai.setOptions({ fetch: fetch2 });

    const history: any[] = [
      { role: 'user', content: 'weather?' },
      {
        role: 'assistant',
        functionCalls,
        thoughtBlocks,
      },
      {
        role: 'function',
        functionId: 'getWeather',
        result: JSON.stringify({ temp: 72, condition: 'sunny' }),
      },
    ];

    await ai.chat({ chatPrompt: history }, { stream: false });

    const reqBody = capture2.lastBody;

    // The second content entry should be the assistant (model) message
    const assistantMsg = reqBody.contents[1];
    expect(assistantMsg.role).toBe('model');

    // Find the functionCall part
    const fcPart = assistantMsg.parts.find(
      (p: any) => p.functionCall?.name === 'getWeather'
    );
    expect(fcPart).toBeDefined();
    expect(fcPart.thought_signature).toBe('round-trip-sig-001');
  });

  // ---------------------------------------------------------------------------
  // 3. Multiple parallel function calls: signature only on the first
  // ---------------------------------------------------------------------------
  it('attaches thought_signature only to the first functionCall when multiple are present', async () => {
    const ai = new AxAIGoogleGemini({
      apiKey: 'test-key',
      config: { model: AxAIGoogleGeminiModel.Gemini3Pro },
      models: [],
    });

    const capture: { lastBody?: any } = {};
    const fetch = createMockFetch(
      {
        candidates: [
          {
            content: { parts: [{ text: 'done' }] },
            finishReason: 'STOP',
          },
        ],
        usageMetadata: {
          promptTokenCount: 30,
          candidatesTokenCount: 3,
          totalTokenCount: 33,
          thoughtsTokenCount: 0,
        },
      },
      capture
    );
    ai.setOptions({ fetch });

    // Simulate a history where the assistant made two parallel function calls
    // with a thought block carrying a signature
    const history: any[] = [
      { role: 'user', content: 'get weather and news' },
      {
        role: 'assistant',
        functionCalls: [
          {
            id: 'id1',
            type: 'function',
            function: { name: 'getWeather', params: '{"city":"NYC"}' },
          },
          {
            id: 'id2',
            type: 'function',
            function: { name: 'getNews', params: '{"topic":"tech"}' },
          },
        ],
        thoughtBlocks: [
          { data: '', encrypted: false, signature: 'multi-fc-sig' },
        ],
      },
      { role: 'function', functionId: 'getWeather', result: '{"temp":72}' },
      {
        role: 'function',
        functionId: 'getNews',
        result: '{"headline":"AI news"}',
      },
    ];

    await ai.chat({ chatPrompt: history }, { stream: false });

    const reqBody = capture.lastBody;
    const assistantMsg = reqBody.contents[1];
    expect(assistantMsg.role).toBe('model');

    // Find all functionCall parts
    const fcParts = assistantMsg.parts.filter((p: any) => p.functionCall);
    expect(fcParts).toHaveLength(2);

    // First function call part should carry the signature
    expect(fcParts[0].functionCall.name).toBe('getWeather');
    expect(fcParts[0].thought_signature).toBe('multi-fc-sig');

    // Second function call part must NOT carry a signature
    expect(fcParts[1].functionCall.name).toBe('getNews');
    expect(fcParts[1].thought_signature).toBeUndefined();
  });

  // ---------------------------------------------------------------------------
  // 4. buildCacheCreateOp (extractCacheableContent path): thought_signature
  //    preserved in the cache creation payload
  // ---------------------------------------------------------------------------
  it('buildCacheCreateOp preserves thought_signature on functionCall in cacheable contents', () => {
    const ai = new AxAIGoogleGemini({
      apiKey: 'test-key',
      config: { model: AxAIGoogleGeminiModel.Gemini3Pro },
      models: [],
    });

    // The assistant message (with thoughtBlocks + functionCalls) sits before the
    // cache breakpoint (the function result has cache: true), so it lands inside
    // extractCacheableContent and therefore inside buildCacheCreateOp's payload.
    const history: any[] = [
      { role: 'user', content: 'What is the weather in NYC?' },
      {
        role: 'assistant',
        functionCalls: [
          {
            id: 'id1',
            type: 'function',
            function: { name: 'getWeather', params: '{"city":"NYC"}' },
          },
        ],
        thoughtBlocks: [
          {
            data: 'thinking about the weather',
            encrypted: false,
            signature: 'cache-path-sig-001',
          },
        ],
      },
      {
        role: 'function',
        functionId: 'getWeather',
        result: '{"temp":72,"condition":"sunny"}',
        cache: true, // <-- breakpoint: all previous messages become cacheable
      },
    ];

    const op = (ai as any).aiImpl.buildCacheCreateOp(
      { chatPrompt: history, model: AxAIGoogleGeminiModel.Gemini3Pro } as any,
      { contextCache: { ttlSeconds: 3600 } } as any
    );

    expect(op).toBeDefined();
    const cacheReq = op!.request as any;
    expect(cacheReq.contents).toBeDefined();

    // The model (assistant) message must be present in the cacheable contents
    const modelMsg = cacheReq.contents.find((c: any) => c.role === 'model');
    expect(modelMsg).toBeDefined();

    // The functionCall part must carry the thought_signature
    const fcPart = modelMsg.parts.find(
      (p: any) => p.functionCall?.name === 'getWeather'
    );
    expect(fcPart).toBeDefined();
    expect(fcPart.thought_signature).toBe('cache-path-sig-001');
  });

  // ---------------------------------------------------------------------------
  // 5. prepareCachedChatReq (extractDynamicContent path): thought_signature
  //    preserved in the dynamic contents sent alongside the cache reference
  // ---------------------------------------------------------------------------
  it('prepareCachedChatReq preserves thought_signature on functionCall in dynamic contents', async () => {
    const ai = new AxAIGoogleGemini({
      apiKey: 'test-key',
      config: { model: AxAIGoogleGeminiModel.Gemini3Pro },
      models: [],
    });

    // The user message is the cache breakpoint; the assistant message (with
    // thoughtBlocks + functionCalls) is AFTER the breakpoint, so it lands in
    // extractDynamicContent and therefore inside prepareCachedChatReq's payload.
    const history: any[] = [
      {
        role: 'user',
        content: 'What is the weather in NYC?',
        cache: true, // <-- breakpoint: everything after this is dynamic
      },
      {
        role: 'assistant',
        functionCalls: [
          {
            id: 'id1',
            type: 'function',
            function: { name: 'getWeather', params: '{"city":"NYC"}' },
          },
        ],
        thoughtBlocks: [
          {
            data: 'thinking about the weather',
            encrypted: false,
            signature: 'dynamic-path-sig-001',
          },
        ],
      },
      {
        role: 'function',
        functionId: 'getWeather',
        result: '{"temp":72,"condition":"sunny"}',
      },
    ];

    const prepared = await (ai as any).aiImpl.prepareCachedChatReq(
      { chatPrompt: history, model: AxAIGoogleGeminiModel.Gemini3Pro } as any,
      {} as any,
      'cachedContents/test-cache-name-123'
    );

    expect(prepared.request.cachedContent).toBe(
      'cachedContents/test-cache-name-123'
    );

    // The model (assistant) message must be present in the dynamic contents
    const modelMsg = (prepared.request.contents as any[]).find(
      (c: any) => c.role === 'model'
    );
    expect(modelMsg).toBeDefined();

    // The functionCall part must carry the thought_signature
    const fcPart = modelMsg.parts.find(
      (p: any) => p.functionCall?.name === 'getWeather'
    );
    expect(fcPart).toBeDefined();
    expect(fcPart.thought_signature).toBe('dynamic-path-sig-001');
  });
});
