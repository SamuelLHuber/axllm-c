import { describe, expect, it } from 'vitest';
import type { AxAIService } from '../../ai/types.js';
import { createAxGenAdapter } from './axGenAdapter.js';

describe('createAxGenAdapter', () => {
  it('slices fn components to relevant function-call traces plus zero-score rows', async () => {
    const program = {
      applyOptimizedComponents: () => {},
      forward: async (_ai: AxAIService, input: any, options: any) => {
        if (input.tool === 'lookup_user') {
          await options.onFunctionCall?.({
            fn: 'lookup_user',
            componentId: 'lookup_user',
            args: { q: input.q },
            result: 'lookup-result',
            ok: true,
            ms: 1,
          });
          return { answer: 'lookup' };
        }
        await options.onFunctionCall?.({
          fn: 'send_email',
          componentId: 'send_email',
          args: { q: input.q },
          result: 'email-result',
          ok: true,
          ms: 1,
        });
        return { answer: 'email' };
      },
    };

    const adapter = createAxGenAdapter({
      program: program as any,
      ai: {} as AxAIService,
      sampleCount: 1,
      metricFn: async ({ example }) => (example as any).score as number,
    });

    const evalBatch = await adapter.evaluate(
      [
        { tool: 'lookup_user', q: 'a', score: 1 },
        { tool: 'send_email', q: 'b', score: 1 },
        { tool: 'send_email', q: 'c', score: 0 },
      ],
      {},
      true
    );
    const ds = adapter.make_reflective_dataset({}, evalBatch, [
      'root::fn:lookup_user:desc',
    ]);

    expect(ds['root::fn:lookup_user:desc']).toHaveLength(2);
    expect(ds['root::fn:lookup_user:desc']?.[0].calls[0].fn).toBe(
      'lookup_user'
    );
    expect(ds['root::fn:lookup_user:desc']?.[1].score).toBe(0);
  });
});
