# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Quick-and-dirty code for tracking the overall token usage of CI scripts.

If each LLM call is recorded via record_llm_call_costs(), calling print_llm_costs() at
the end of a script will print aggregated usage statistics, including estimated cost at
advertised rates.
"""
import sys
from dataclasses import dataclass

import anthropic


__estimated_prices_per_million_tokens__ = {
    'claude-sonnet-4-6': (3.00, 15.00),
}


@dataclass
class LlmCallCosts:
    label: str
    model: str
    input_tokens: int
    output_tokens: int


__llm_call_costs__: list[LlmCallCosts] = []


def record_llm_call_costs(response: anthropic.types.Message, label: str = ''):
    __llm_call_costs__.append(LlmCallCosts(
        label=label,
        model=response.model,
        input_tokens=response.usage.input_tokens,
        output_tokens=response.usage.output_tokens,
    ))


def print_llm_costs() -> None:
    total_input_tokens = 0
    total_output_tokens = 0
    total_cost = 0.0
    for c in __llm_call_costs__:
        prices = __estimated_prices_per_million_tokens__.get(c.model)
        if not prices:
            print(f'WARNING: Usage costs for model {c.model} not known; falling back to claude-sonnet-4-6 pricing', file=sys.stderr)
            prices = __estimated_prices_per_million_tokens__['claude-sonnet-4-6']
        input_price, output_price = prices
        cost = (c.input_tokens / 1_000_000 * input_price + c.output_tokens / 1_000_000 * output_price)
        total_input_tokens += c.input_tokens
        total_output_tokens += c.output_tokens
        total_cost += cost
        label = f' ({c.label})' if c.label else ''
        print(f'  {c.model}{label}: {c.input_tokens}in + {c.output_tokens}out = ${cost:.4f}', file=sys.stderr)
    print(f'  Estimated cost: ${total_cost:.4f} ({len(__llm_call_costs__)} LLM calls with {total_input_tokens} input tokens, {total_output_tokens} output tokens)', file=sys.stderr)
