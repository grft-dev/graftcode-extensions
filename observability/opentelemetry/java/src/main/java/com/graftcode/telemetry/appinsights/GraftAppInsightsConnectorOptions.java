package com.graftcode.telemetry.appinsights;

import com.hypertube.utils.logging.LogsLevel;

import java.util.function.Function;

public final class GraftAppInsightsConnectorOptions {
    public static final String CONNECTION_STRING_VARIABLE = "APPLICATIONINSIGHTS_CONNECTION_STRING";

    private String serviceName = "";
    private String connectionString;
    private LogsLevel minimumLogLevel = LogsLevel.Info;
    private Double tracesPerSecond;
    private boolean enableConsoleLogging = true;
    private Function<String, String> environment = System::getenv;

    public String getServiceName() {
        return serviceName;
    }

    public void setServiceName(String serviceName) {
        this.serviceName = serviceName;
    }

    public String getConnectionString() {
        return connectionString;
    }

    public void setConnectionString(String connectionString) {
        this.connectionString = connectionString;
    }

    public LogsLevel getMinimumLogLevel() {
        return minimumLogLevel;
    }

    public void setMinimumLogLevel(LogsLevel minimumLogLevel) {
        this.minimumLogLevel = minimumLogLevel;
    }

    public Double getTracesPerSecond() {
        return tracesPerSecond;
    }

    public void setTracesPerSecond(Double tracesPerSecond) {
        this.tracesPerSecond = tracesPerSecond;
    }

    public boolean isEnableConsoleLogging() {
        return enableConsoleLogging;
    }

    public void setEnableConsoleLogging(boolean enableConsoleLogging) {
        this.enableConsoleLogging = enableConsoleLogging;
    }

    void setEnvironment(Function<String, String> environment) {
        this.environment = environment;
    }

    String resolveConnectionString() {
        if (!isBlank(connectionString)) {
            return connectionString;
        }

        String environmentValue = environment.apply(CONNECTION_STRING_VARIABLE);
        return isBlank(environmentValue) ? null : environmentValue;
    }

    void validate() {
        if (isBlank(serviceName)) {
            throw new IllegalStateException("ServiceName must be configured for Azure Monitor telemetry.");
        }

        if (minimumLogLevel == null) {
            throw new IllegalStateException("MinimumLogLevel must not be null.");
        }

        if (tracesPerSecond != null && !(tracesPerSecond > 0)) {
            throw new IllegalStateException("TracesPerSecond must be greater than zero.");
        }
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }
}
