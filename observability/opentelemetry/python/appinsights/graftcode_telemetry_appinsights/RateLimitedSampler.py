from __future__ import annotations

import threading
import time
from typing import Optional

from opentelemetry.context import Context
from opentelemetry.sdk.trace.sampling import Decision, Sampler, SamplingResult
from opentelemetry.trace import Link, SpanKind
from opentelemetry.trace.span import TraceState


class RateLimitedSampler(Sampler):

    def __init__(self, traces_per_second: float) -> None:
        self._traces_per_second = traces_per_second
        self._capacity = max(1.0, traces_per_second)
        self._tokens = self._capacity
        self._last_refill = time.monotonic()
        self._lock = threading.Lock()

    def should_sample(
            self,
            parent_context: Optional[Context],
            trace_id: int,
            name: str,
            kind: Optional[SpanKind] = None,
            attributes: Optional[dict] = None,
            links: Optional[tuple[Link, ...]] = None,
            trace_state: Optional[TraceState] = None,
    ) -> SamplingResult:
        decision = Decision.RECORD_AND_SAMPLE if self._try_acquire() else Decision.DROP
        return SamplingResult(decision, attributes if decision == Decision.RECORD_AND_SAMPLE else None, trace_state)

    def get_description(self) -> str:
        return "RateLimitedSampler{" + str(self._traces_per_second) + "}"

    def _try_acquire(self) -> bool:
        with self._lock:
            now = time.monotonic()
            self._tokens = min(self._capacity, self._tokens + (now - self._last_refill) * self._traces_per_second)
            self._last_refill = now

            if self._tokens < 1.0:
                return False

            self._tokens -= 1.0
            return True
