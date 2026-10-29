package com.graftcode.telemetry.appinsights;

import io.opentelemetry.api.trace.Span;
import io.opentelemetry.api.trace.SpanKind;
import io.opentelemetry.api.trace.StatusCode;
import io.opentelemetry.api.trace.propagation.W3CTraceContextPropagator;
import io.opentelemetry.context.Context;
import io.opentelemetry.context.Scope;
import io.opentelemetry.context.propagation.TextMapGetter;
import jakarta.servlet.Filter;
import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import java.io.IOException;
import java.lang.reflect.Method;
import java.util.Collections;
import java.util.Objects;

public final class GraftCodeAppInsightsRequestTelemetryFilter implements Filter {
    public static final String CONTROLLER_ATTRIBUTE = "graft.controller";

    private static final String SPRING_HANDLER_ATTRIBUTE =
            "org.springframework.web.servlet.HandlerMapping.bestMatchingHandler";
    private static final String CONTROLLER_SUFFIX = "Controller";

    private static final TextMapGetter<HttpServletRequest> HEADER_GETTER = new TextMapGetter<HttpServletRequest>() {
        @Override
        public Iterable<String> keys(HttpServletRequest carrier) {
            return Collections.list(carrier.getHeaderNames());
        }

        @Override
        public String get(HttpServletRequest carrier, String key) {
            return carrier == null ? null : carrier.getHeader(key);
        }
    };

    private final GraftCodeAppInsightsConnectorPipeline telemetry;

    public GraftCodeAppInsightsRequestTelemetryFilter(GraftCodeAppInsightsConnectorPipeline telemetry) {
        this.telemetry = Objects.requireNonNull(telemetry, "telemetry");
    }

    @Override
    public void doFilter(ServletRequest request, ServletResponse response, FilterChain chain)
            throws IOException, ServletException {
        if (!(request instanceof HttpServletRequest) || !(response instanceof HttpServletResponse)) {
            chain.doFilter(request, response);
            return;
        }

        HttpServletRequest httpRequest = (HttpServletRequest) request;
        HttpServletResponse httpResponse = (HttpServletResponse) response;
        String path = resolvePath(httpRequest);

        Context parent = W3CTraceContextPropagator.getInstance().extract(Context.current(), httpRequest, HEADER_GETTER);
        Span span = telemetry.getTracer()
                .spanBuilder(resolveOperationName(telemetry.getServiceName(), httpRequest.getMethod(), path, null))
                .setParent(parent)
                .setSpanKind(SpanKind.SERVER)
                .startSpan();

        if (span.isRecording()) {
            span.setAttribute("http.method", httpRequest.getMethod());
            span.setAttribute("http.route", path);
        }

        try (Scope ignored = span.makeCurrent()) {
            chain.doFilter(request, response);

            int status = httpResponse.getStatus();
            span.setAttribute("http.status_code", (long) status);
            if (status >= 500) {
                span.setStatus(StatusCode.ERROR);
            }
        } catch (IOException | ServletException | RuntimeException | Error exception) {
            span.setStatus(StatusCode.ERROR, exception.getMessage() == null ? "" : exception.getMessage());
            span.recordException(exception);
            throw exception;
        } finally {
            String controller = resolveControllerName(httpRequest);
            if (controller != null) {
                span.updateName(resolveOperationName(telemetry.getServiceName(), httpRequest.getMethod(), path, controller));
            }

            span.end();
        }
    }

    static String resolveOperationName(String serviceName, String method, String path, String controller) {
        if (controller != null && !controller.trim().isEmpty()) {
            return serviceName + "." + controller;
        }

        return serviceName + " " + method + " " + (path == null || path.isEmpty() ? "/" : path);
    }

    static String resolveControllerName(HttpServletRequest request) {
        Object explicit = request.getAttribute(CONTROLLER_ATTRIBUTE);
        if (explicit instanceof String && !((String) explicit).trim().isEmpty()) {
            return (String) explicit;
        }

        Object handler = request.getAttribute(SPRING_HANDLER_ATTRIBUTE);
        if (handler == null) {
            return null;
        }

        Class<?> beanType = beanTypeOf(handler);
        if (beanType == null) {
            return null;
        }

        String name = beanType.getSimpleName();
        return name.endsWith(CONTROLLER_SUFFIX) && name.length() > CONTROLLER_SUFFIX.length()
                ? name.substring(0, name.length() - CONTROLLER_SUFFIX.length())
                : name;
    }

    private static Class<?> beanTypeOf(Object handler) {
        try {
            Method getBeanType = handler.getClass().getMethod("getBeanType");
            Object beanType = getBeanType.invoke(handler);
            return beanType instanceof Class ? (Class<?>) beanType : null;
        } catch (ReflectiveOperationException | RuntimeException ex) {
            return null;
        }
    }

    private static String resolvePath(HttpServletRequest request) {
        String uri = request.getRequestURI();
        String contextPath = request.getContextPath();
        if (uri != null && contextPath != null && !contextPath.isEmpty() && uri.startsWith(contextPath)) {
            uri = uri.substring(contextPath.length());
        }

        return uri == null || uri.isEmpty() ? "/" : uri;
    }
}
