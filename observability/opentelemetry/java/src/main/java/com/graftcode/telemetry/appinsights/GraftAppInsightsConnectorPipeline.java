package com.graftcode.telemetry.appinsights;

import com.azure.monitor.opentelemetry.autoconfigure.AzureMonitorAutoConfigure;
import com.azure.monitor.opentelemetry.autoconfigure.AzureMonitorAutoConfigureOptions;
import com.graftcode.instrumentation.GraftcodeInstrumentation;
import com.hypertube.utils.logging.LogsLevel;
import io.opentelemetry.api.OpenTelemetry;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.common.Attributes;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.sdk.OpenTelemetrySdk;
import io.opentelemetry.sdk.autoconfigure.AutoConfiguredOpenTelemetrySdk;
import io.opentelemetry.sdk.autoconfigure.AutoConfiguredOpenTelemetrySdkBuilder;
import io.opentelemetry.sdk.common.CompletableResultCode;
import io.opentelemetry.sdk.logs.SdkLoggerProvider;
import io.opentelemetry.sdk.resources.Resource;
import io.opentelemetry.sdk.trace.SdkTracerProvider;
import io.opentelemetry.sdk.trace.samplers.Sampler;

import java.util.Arrays;
import java.util.Collections;
import java.util.concurrent.TimeUnit;

public final class GraftAppInsightsConnectorPipeline implements AutoCloseable {
    private static final int DEFAULT_FLUSH_MILLISECONDS = 5000;
    private static final String MISSING_CONNECTION_STRING =
            "Application Insights telemetry for {ServiceName} is disabled: no connection string was configured. "
                    + "Set " + GraftAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE + " in this process or call "
                    + "GraftAppInsightsConnectorOptions.setConnectionString.";

    private final Object flushGate = new Object();
    private final String serviceName;
    private final OpenTelemetrySdk sdk;
    private final Tracer tracer;
    private final GraftAppInsightsLogger logger;
    private final GraftcodeInstrumentation instrumentation;
    private final boolean exportingToAppInsights;
    private final Thread shutdownHook;
    private final Thread.UncaughtExceptionHandler previousUncaughtHandler;
    private final Thread.UncaughtExceptionHandler uncaughtHandler;
    private boolean closed;

    GraftAppInsightsConnectorPipeline(GraftAppInsightsConnectorOptions options) {
        this(options, null);
    }

    GraftAppInsightsConnectorPipeline(GraftAppInsightsConnectorOptions options, OpenTelemetrySdk providedSdk) {
        options.validate();

        serviceName = options.getServiceName();
        String connectionString = providedSdk == null ? options.resolveConnectionString() : null;
        exportingToAppInsights = connectionString != null;

        if (providedSdk != null) {
            sdk = providedSdk;
        } else if (connectionString != null) {
            sdk = buildExportingSdk(serviceName, connectionString, options.getTracesPerSecond());
        } else {
            sdk = buildLocalSdk(serviceName);
        }

        tracer = sdk.getTracer(serviceName);
        logger = new GraftAppInsightsLogger(
                serviceName,
                sdk.getLogsBridge().get(serviceName),
                options.getMinimumLogLevel(),
                options.isEnableConsoleLogging() ? System.out : null);
        instrumentation = GraftcodeInstrumentation.attach(sdk, options.getMinimumLogLevel());

        if (providedSdk == null && connectionString == null) {
            warnAboutMissingConnectionString(options);
        }

        shutdownHook = new Thread(this::close, "graft-appinsights-shutdown");
        Runtime.getRuntime().addShutdownHook(shutdownHook);

        previousUncaughtHandler = Thread.getDefaultUncaughtExceptionHandler();
        uncaughtHandler = this::onUncaughtException;
        Thread.setDefaultUncaughtExceptionHandler(uncaughtHandler);
    }

    public String getServiceName() {
        return serviceName;
    }

    public Tracer getTracer() {
        return tracer;
    }

    public OpenTelemetry getOpenTelemetry() {
        return sdk;
    }

    public GraftAppInsightsLogger getLogger() {
        return logger;
    }

    public boolean isExportingToAppInsights() {
        return exportingToAppInsights;
    }

    public Span startSpan(String name) {
        return startSpan(name, SpanKind.INTERNAL);
    }

    public Span startSpan(String name, SpanKind kind) {
        return tracer.spanBuilder(name).setSpanKind(kind).startSpan();
    }

    public boolean forceFlush() {
        return forceFlush(DEFAULT_FLUSH_MILLISECONDS);
    }

    public boolean forceFlush(int millisecondsTimeout) {
        synchronized (flushGate) {
            if (closed) {
                return true;
            }

            CompletableResultCode traces = sdk.getSdkTracerProvider().forceFlush();
            CompletableResultCode logs = sdk.getSdkLoggerProvider().forceFlush();
            return CompletableResultCode.ofAll(Arrays.asList(traces, logs))
                    .join(millisecondsTimeout, TimeUnit.MILLISECONDS)
                    .isSuccess();
        }
    }

    @Override
    public void close() {
        synchronized (flushGate) {
            if (closed) {
                return;
            }

            sdk.getSdkTracerProvider().forceFlush().join(DEFAULT_FLUSH_MILLISECONDS, TimeUnit.MILLISECONDS);
            sdk.getSdkLoggerProvider().forceFlush().join(DEFAULT_FLUSH_MILLISECONDS, TimeUnit.MILLISECONDS);
            closed = true;
        }

        instrumentation.close();

        try {
            Runtime.getRuntime().removeShutdownHook(shutdownHook);
        } catch (IllegalStateException alreadyShuttingDown) {
        }

        if (Thread.getDefaultUncaughtExceptionHandler() == uncaughtHandler) {
            Thread.setDefaultUncaughtExceptionHandler(previousUncaughtHandler);
        }

        sdk.shutdown().join(DEFAULT_FLUSH_MILLISECONDS, TimeUnit.MILLISECONDS);
    }

    private static OpenTelemetrySdk buildExportingSdk(String serviceName, String connectionString, Double tracesPerSecond) {
        Sampler sampler = tracesPerSecond == null ? Sampler.alwaysOn() : new RateLimitedSampler(tracesPerSecond);

        AutoConfiguredOpenTelemetrySdkBuilder builder = AutoConfiguredOpenTelemetrySdk.builder()
                .disableShutdownHook()
                .addPropertiesSupplier(() -> Collections.singletonMap("otel.service.name", serviceName))
                .addSamplerCustomizer((configured, properties) -> sampler);

        AzureMonitorAutoConfigure.customize(builder, new AzureMonitorAutoConfigureOptions().connectionString(connectionString));
        return builder.build().getOpenTelemetrySdk();
    }

    private static OpenTelemetrySdk buildLocalSdk(String serviceName) {
        Resource resource = Resource.getDefault().merge(
                Resource.create(Attributes.of(AttributeKey.stringKey("service.name"), serviceName)));

        return OpenTelemetrySdk.builder()
                .setTracerProvider(SdkTracerProvider.builder().setResource(resource).build())
                .setLoggerProvider(SdkLoggerProvider.builder().setResource(resource).build())
                .build();
    }

    private void warnAboutMissingConnectionString(GraftAppInsightsConnectorOptions options) {
        logger.log(LogsLevel.Warning, MISSING_CONNECTION_STRING, serviceName);

        if (!options.isEnableConsoleLogging()) {
            System.err.println(MISSING_CONNECTION_STRING.replace("{ServiceName}", serviceName));
        }
    }

    private void onUncaughtException(Thread thread, Throwable exception) {
        forceFlush();

        if (previousUncaughtHandler != null) {
            previousUncaughtHandler.uncaughtException(thread, exception);
            return;
        }

        System.err.print("Exception in thread \"" + thread.getName() + "\" ");
        exception.printStackTrace(System.err);
    }
}
