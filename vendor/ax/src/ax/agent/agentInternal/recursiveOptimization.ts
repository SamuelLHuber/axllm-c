import type { AxAgentRecursiveNodeRole } from '../agentRecursiveOptimize.js';
import type { AxAgentOptimizationTargetDescriptor } from './types.js';

/** Advanced/recursive llmQuery mode has been removed. Always returns false. */
export function supportsRecursiveActorSlotOptimization(_self: any): boolean {
  return false;
}

/** Advanced/recursive llmQuery mode has been removed. Always returns undefined. */
export function getRecursiveActorRole(
  _self: any
): AxAgentRecursiveNodeRole | undefined {
  return undefined;
}

/** Always returns the flat list of named programs — recursive slot targets are gone. */
export function listOptimizationTargetDescriptors(
  self: any
): AxAgentOptimizationTargetDescriptor[] {
  const s = self as any;
  return s.namedProgramInstances().map((entry: any) => ({
    id: entry.id,
    signature: entry.signature,
    program: entry.program as AxAgentOptimizationTargetDescriptor['program'],
  }));
}
