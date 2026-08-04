/**
 * Generic component-optimization surface that any AxProgrammable can implement.
 *
 * GEPA (and any future reflective optimizer) discovers what can be optimized by
 * calling `getOptimizableComponents()` on a program tree, mutates the returned
 * string values, and broadcasts the updates back via `applyOptimizedComponents()`.
 * The optimizer learns nothing about specific artifact kinds — each program owns
 * its own dispatch and tree traversal.
 */

export type AxOptimizableValidator = (value: string) => true | string;

export interface AxOptimizableComponent {
  /**
   * Globally-unique key within the program tree. Stable across calls.
   * Convention: `${programId}::${kind}` or `${programId}::${kind}:${subKey}`.
   */
  key: string;

  /**
   * Free-form hint for the reflection prompt. Not interpreted by the optimizer.
   * Examples: "instruction", "description", "fn-desc", "fn-name", "actor-tpl",
   * "primitive", or any user-defined kind.
   */
  kind: string;

  /** Current value snapshot. */
  current: string;

  /** Human-readable context shown to the reflection LLM. */
  description?: string;

  /** Free-form invariants the proposed value must respect. */
  constraints?: string;

  /** Stable identifier used to correlate runtime traces with this component. */
  traceId?: string;

  /** Other component keys that should be proposed and evaluated with this one. */
  dependsOn?: readonly string[];

  /** Literal tokens/placeholders that proposed values must preserve. */
  preserve?: readonly string[];

  /** Optional generic length hint for reflection and validation. */
  maxLength?: number;

  /** Optional generic format hint, e.g. "snake_case" or "handlebars-template". */
  format?: string;

  /**
   * Optional validator: returns `true` when the value is acceptable, or an
   * error message string the optimizer can show the LLM on re-roll.
   */
  validate?: AxOptimizableValidator;
}

/**
 * Parse a component key into its parts. Returns `null` if the key is malformed.
 *
 * Grammar: `${programId}::${kind}` or `${programId}::${kind}:${subKey}`.
 * `subKey` may itself contain colons (e.g. `fn:foo:desc` → kind=`fn`, subKey=`foo:desc`).
 */
export function parseComponentKey(
  key: string
): { programId: string; kind: string; subKey?: string } | null {
  const sep = key.indexOf('::');
  if (sep < 0) return null;
  const programId = key.slice(0, sep);
  const rest = key.slice(sep + 2);
  if (!programId || !rest) return null;
  const colon = rest.indexOf(':');
  if (colon < 0) return { programId, kind: rest };
  return {
    programId,
    kind: rest.slice(0, colon),
    subKey: rest.slice(colon + 1),
  };
}

export function formatComponentKey(
  programId: string,
  kind: string,
  subKey?: string
): string {
  return subKey ? `${programId}::${kind}:${subKey}` : `${programId}::${kind}`;
}

/** Common validators reused by program-side component declarations. */
export const axOptimizableValidators = {
  /** snake_case identifier, ≤ maxLen chars. */
  snakeCaseIdentifier:
    (maxLen = 32): AxOptimizableValidator =>
    (value) => {
      const v = value.trim();
      if (v.length === 0) return 'identifier must not be empty';
      if (v.length > maxLen) return `identifier must be ≤ ${maxLen} chars`;
      if (!/^[a-z][a-z0-9_]*$/.test(v))
        return 'identifier must be snake_case (a-z, 0-9, _; starting with a letter)';
      return true;
    },

  /** Must contain every placeholder in `required` (e.g. `['{{primitivesList}}']`). */
  preservesPlaceholders:
    (required: readonly string[]): AxOptimizableValidator =>
    (value) => {
      for (const p of required) {
        if (!value.includes(p)) return `must preserve placeholder ${p}`;
      }
      return true;
    },

  /** Non-empty after trim. */
  nonEmpty: (): AxOptimizableValidator => (value) =>
    value.trim().length > 0 ? true : 'value must not be empty',
} as const;
