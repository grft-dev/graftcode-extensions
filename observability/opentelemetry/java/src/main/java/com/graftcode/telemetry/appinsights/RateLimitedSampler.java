package com.graftcode.telemetry.appinsights;

import io.opentelemetry.api.common.Attributes;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.context.Context;
import io.opentelemetry.sdk.trace.data.LinkData;
import io.opentelemetry.sdk.trace.samplers.Sampler;
import io.opentelemetry.sdk.trace.samplers.SamplingDecision;
import io.opentelemetry.sdk.trace.samplers.SamplingResult;

import java.util.List;

final class RateLimitedSampler implements Sampler {
    private final double tracesPerSecond;
    private final double capacity;
    private double tokens;
    private long lastRefillNanos;

    RateLimitedSampler(double tracesPerSecond) {
        this.tracesPerSecond = tracesPerSecond;
        this.capacity = Math.max(1.0, tracesPerSecond);
        this.tokens = capacity;
        this.lastRefillNanos = System.nanoTime();
    }

    @Override
    public SamplingResult shouldSample(
            Context parentContext,
            String traceId,
            String name,
            SpanKind spanKind,
            Attributes attributes,
            List<LinkData> parentLinks) {
        return SamplingResult.create(tryAcquire() ? SamplingDecision.RECORD_AND_SAMPLE : SamplingDecision.DROP);
    }

    @Override
    public String getDescription() {
        return "RateLimitedSampler{" + tracesPerSecond + "}";
    }

    private synchronized boolean tryAcquire() {
        long now = System.nanoTime();
        tokens = Math.min(capacity, tokens + ((now - lastRefillNanos) / 1_000_000_000.0) * tracesPerSecond);
        lastRefillNanos = now;

        if (tokens < 1.0) {
            return false;
        }

        tokens -= 1.0;
        return true;
    }
}
