package com.graftcode.samples.crosstech.onetoone;

import com.google.gson.Gson;
import com.graftcode.telemetry.appinsights.GraftCodeAppInsightsConnector;
import com.graftcode.telemetry.appinsights.GraftCodeAppInsightsConnectorPipeline;
import com.hypertube.sdk.RuntimeBridge;
import com.hypertube.sdk.RuntimeContext;
import com.hypertube.sdk.RuntimeFactory;
import com.hypertube.utils.connectiondata.WsConnectionData;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.context.Scope;

import java.nio.file.Path;
import java.util.Map;
import java.util.Objects;

public final class CrossTechOneToOneRunner {
    private CrossTechOneToOneRunner() {
    }

    public static void main(String[] args) throws Exception {
        var configPath = args.length > 0
                ? Path.of(args[0])
                : Path.of("..", "matrix-config.json").toAbsolutePath().normalize();
        var connectionString = System.getenv("APPLICATIONINSIGHTS_CONNECTION_STRING");
        if (connectionString == null || connectionString.isBlank()) {
            System.err.println("APPLICATIONINSIGHTS_CONNECTION_STRING is not set");
            System.exit(2);
        }

        var gson = new Gson();
        var root = gson.fromJson(java.nio.file.Files.readString(configPath), MatrixRoot.class);
        if (root == null || root.hosts == null) {
            throw new IllegalStateException("matrix-config.json invalid");
        }

        try (GraftCodeAppInsightsConnectorPipeline pipeline = GraftCodeAppInsightsConnector.start(options -> {
            options.setServiceName("CrossTech.OneToOne.JavaCaller");
            options.setConnectionString(connectionString);
        })) {

            for (var entry : root.hosts.entrySet()) {
                var calleeKey = entry.getKey();
                var callee = entry.getValue();
                var input = "matrix-java-" + calleeKey.toLowerCase();
                try {
                    var traceId = "";
                    var result = "";
                    Span span = pipeline.startSpan("CrossMatrix.Java->" + calleeKey, SpanKind.SERVER);
                    try (Scope ignored = span.makeCurrent()) {
                        var spanContext = Span.current().getSpanContext();
                        traceId = spanContext.getTraceId();
                        pipeline.getLogger().info(
                                "CrossMatrix Caller=Java Callee={Callee} Input={Input} TraceId={TraceId}",
                                calleeKey,
                                input,
                                traceId);

                        RuntimeFactory factory = RuntimeBridge.webSocket(new WsConnectionData(callee.ws));
                        RuntimeContext runtime = switch (Objects.requireNonNullElse(callee.runtimeArg, "")) {
                            case "netcore" -> factory.netcore();
                            case "python" -> factory.python();
                            case "nodejs" -> factory.nodejs();
                            default -> factory.jvm();
                        };

                        Object value = runtime
                                .getType(callee.typeName)
                                .invokeStaticMethod(callee.methodName, input)
                                .execute()
                                .getValue();

                        result = String.valueOf(value);
                        pipeline.getLogger().info("CrossMatrix after call Result={Result}", result);
                    } finally {
                        span.end();
                        pipeline.forceFlush(15000);
                    }

                    System.out.printf("OK|Java|%s|%s|%s%n", calleeKey, traceId, result.replace('|', '/'));
                } catch (Exception ex) {
                    System.out.printf("FAIL|Java|%s||%s: %s%n", calleeKey, ex.getClass().getSimpleName(),
                            ex.getMessage().replace('|', '/'));
                }
            }
        }
    }

    private static final class MatrixRoot {
        Map<String, CalleeConfig> hosts;
    }

    private static final class CalleeConfig {
        String ws;
        String runtimeArg;
        String typeName;
        String methodName;
    }
}