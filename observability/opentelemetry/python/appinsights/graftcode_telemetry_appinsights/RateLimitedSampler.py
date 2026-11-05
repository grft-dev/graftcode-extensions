import threading
import time

from opentelemetry.sdk.trace.sampling import Decision, Sampler, SamplingResult


class RateLimitedSampler(Sampler):

    def __init__(self, traces_per_second):
        self._traces_per_second = traces_per_second
        self._capacity = max(1.0, traces_per_second)
        self._tokens = self._capacity
        self._last_refill = time.monotonic()
        self._lock = threading.Lock()

    def should_sample(self, parent_context, trace_id, name, kind=None, attributes=None, links=None,
                      trace_state=None):
        decision = Decision.RECORD_AND_SAMPLE if self._try_acquire() else Decision.DROP
        return SamplingResult(decision, attributes if decision == Decision.RECORD_AND_SAMPLE else None, trace_state)

    def get_description(self):
        return "RateLimitedSampler{" + str(self._traces_per_second) + "}"

    def _try_acquire(self):
        with self._lock:
            now = time.monotonic()
            self._tokens = min(self._capacity, self._tokens + (now - self._last_refill) * self._traces_per_second)
            self._last_refill = now

            if self._tokens < 1.0:
                return False

            self._tokens -= 1.0
            return True
