export type AxAgentRecursiveTargetId =
  | 'root.actor.shared'
  | 'root.actor.root'
  | 'root.actor.recursive'
  | 'root.actor.terminal'
  | 'root.responder';

export type AxAgentRecursiveNodeRole = 'root' | 'recursive' | 'terminal';

export type AxAgentRecursiveUsage = {
  promptTokens: number;
  completionTokens: number;
  totalTokens: number;
};

export type AxAgentRecursiveTurn = {
  turn: number;
  code: string;
  output: string;
  isError: boolean;
  thought?: string;
};

export type AxAgentRecursiveFunctionCall = {
  qualifiedName: string;
  name?: string;
  error?: string;
};

export type AxAgentRecursiveTraceNode = {
  nodeId: string;
  parentId?: string;
  depth: number;
  role: AxAgentRecursiveNodeRole;
  taskDigest?: string;
  contextDigest?: string;
  completionType?: 'final' | 'askClarification';
  turnCount: number;
  childCount: number;
  actorTurns: AxAgentRecursiveTurn[];
  functionCalls: AxAgentRecursiveFunctionCall[];
  toolErrors: string[];
  localUsage: AxAgentRecursiveUsage;
  cumulativeUsage: AxAgentRecursiveUsage;
  children: AxAgentRecursiveTraceNode[];
};

export type AxAgentRecursiveExpensiveNode = {
  nodeId: string;
  role: AxAgentRecursiveNodeRole;
  depth: number;
  taskDigest?: string;
  totalTokens: number;
};

export type AxAgentRecursiveStats = {
  nodeCount: number;
  leafCount: number;
  maxDepth: number;
  recursiveCallCount: number;
  batchedFanOutCount: number;
  clarificationCount: number;
  errorCount: number;
  directAnswerCount: number;
  delegatedAnswerCount: number;
  rootLocalUsage: AxAgentRecursiveUsage;
  rootCumulativeUsage: AxAgentRecursiveUsage;
  topExpensiveNodes: AxAgentRecursiveExpensiveNode[];
};
