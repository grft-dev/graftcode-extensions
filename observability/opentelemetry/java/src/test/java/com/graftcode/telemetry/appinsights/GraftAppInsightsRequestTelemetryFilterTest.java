package com.graftcode.telemetry.appinsights;

import com.hypertube.utils.logging.LogsLevel;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.api.trace.StatusCode;
import io.opentelemetry.sdk.trace.data.SpanData;
import jakarta.servlet.FilterChain;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Proxy;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;

class GraftAppInsightsRequestTelemetryFilterTest {
    private static final String TRACE_ID = "0af7651916cd43dd8448eb211c80319c";
    private static final String PARENT_SPAN_ID = "b7ad6b7169203331";

    @Test
    void requestOperationName_UsesControllerWhenAvailable() {
        Assertions.assertEquals(
                "TestGgJavaWebApi.Fetch",
                GraftAppInsightsRequestTelemetryFilter.resolveOperationName("TestGgJavaWebApi", "GET", "/api/fetch/Pawel6", "Fetch"));
    }

    @Test
    void requestOperationName_FallsBackToMethodAndPath() {
        Assertions.assertEquals(
                "TestGgJavaWebApi GET /health",
                GraftAppInsightsRequestTelemetryFilter.resolveOperationName("TestGgJavaWebApi", "GET", "/health", null));
    }

    @Test
    void springHandler_NamesTheControllerWithoutItsSuffix() {
        Map<String, Object> attributes = new HashMap<>();
        attributes.put("org.springframework.web.servlet.HandlerMapping.bestMatchingHandler", new FakeHandlerMethod(FetchController.class));

        Assertions.assertEquals("Fetch", GraftAppInsightsRequestTelemetryFilter.resolveControllerName(request("/api/fetch/x", attributes, Collections.<String, String>emptyMap())));
    }

    @Test
    void request_OpensAServerSpan_ParentedOnTheIncomingTraceParent_AndCurrentDuringTheChain() throws Exception {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            Map<String, Object> attributes = new HashMap<>();
            HttpServletRequest request = request(
                    "/api/fetch/Pawel6",
                    attributes,
                    Collections.singletonMap("traceparent", "00-" + TRACE_ID + "-" + PARENT_SPAN_ID + "-01"));
            AtomicReference<String> traceIdInsideChain = new AtomicReference<>();

            FilterChain chain = (req, res) -> {
                traceIdInsideChain.set(Span.current().getSpanContext().getTraceId());
                attributes.put(GraftAppInsightsRequestTelemetryFilter.CONTROLLER_ATTRIBUTE, "Fetch");
            };

            new GraftAppInsightsRequestTelemetryFilter(telemetry.pipeline).doFilter(request, response(200), chain);

            SpanData span = telemetry.spans().get(0);
            Assertions.assertEquals("TestService.Fetch", span.getName());
            Assertions.assertEquals(SpanKind.SERVER, span.getKind());
            Assertions.assertEquals(TRACE_ID, span.getTraceId());
            Assertions.assertEquals(PARENT_SPAN_ID, span.getParentSpanId());
            Assertions.assertEquals(TRACE_ID, traceIdInsideChain.get());
            Assertions.assertEquals("GET", span.getAttributes().get(AttributeKey.stringKey("http.method")));
            Assertions.assertEquals("/api/fetch/Pawel6", span.getAttributes().get(AttributeKey.stringKey("http.route")));
            Assertions.assertEquals(200L, span.getAttributes().get(AttributeKey.longKey("http.status_code")));
            Assertions.assertFalse(Span.current().getSpanContext().isValid());
        }
    }

    @Test
    void serverErrors_MarkTheSpanAsFailed() throws Exception {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            new GraftAppInsightsRequestTelemetryFilter(telemetry.pipeline).doFilter(
                    request("/boom", new HashMap<>(), Collections.<String, String>emptyMap()),
                    response(503),
                    (req, res) -> {
                    });

            SpanData span = telemetry.spans().get(0);
            Assertions.assertEquals("TestService GET /boom", span.getName());
            Assertions.assertEquals(StatusCode.ERROR, span.getStatus().getStatusCode());
        }
    }

    @Test
    void exceptions_AreRecorded_AndRethrown() {
        try (InMemoryTelemetry telemetry = new InMemoryTelemetry(LogsLevel.Info)) {
            Assertions.assertThrows(IllegalStateException.class, () -> new GraftAppInsightsRequestTelemetryFilter(telemetry.pipeline).doFilter(
                    request("/boom", new HashMap<>(), Collections.<String, String>emptyMap()),
                    response(200),
                    (req, res) -> {
                        throw new IllegalStateException("boom");
                    }));

            SpanData span = telemetry.spans().get(0);
            Assertions.assertEquals(StatusCode.ERROR, span.getStatus().getStatusCode());
            Assertions.assertEquals("exception", span.getEvents().get(0).getName());
        }
    }

    static final class FetchController {
    }

    public static final class FakeHandlerMethod {
        private final Class<?> beanType;

        FakeHandlerMethod(Class<?> beanType) {
            this.beanType = beanType;
        }

        public Class<?> getBeanType() {
            return beanType;
        }
    }

    private static HttpServletRequest request(String uri, Map<String, Object> attributes, Map<String, String> headers) {
        return (HttpServletRequest) Proxy.newProxyInstance(
                HttpServletRequest.class.getClassLoader(),
                new Class<?>[]{HttpServletRequest.class},
                (proxy, method, args) -> {
                    switch (method.getName()) {
                        case "getMethod":
                            return "GET";
                        case "getRequestURI":
                            return uri;
                        case "getContextPath":
                            return "";
                        case "getAttribute":
                            return attributes.get((String) args[0]);
                        case "getHeader":
                            return headers.get(((String) args[0]).toLowerCase());
                        case "getHeaderNames":
                            return Collections.enumeration(headers.keySet());
                        default:
                            return null;
                    }
                });
    }

    private static HttpServletResponse response(int status) {
        return (HttpServletResponse) Proxy.newProxyInstance(
                HttpServletResponse.class.getClassLoader(),
                new Class<?>[]{HttpServletResponse.class},
                (proxy, method, args) -> "getStatus".equals(method.getName()) ? status : null);
    }
}
