package com.graftcode.telemetry.appinsights;

import java.util.Objects;
import java.util.function.Consumer;

public final class GraftCodeAppInsightsConnector {
    private GraftCodeAppInsightsConnector() {
    }

    public static GraftCodeAppInsightsConnectorPipeline start(String serviceName) {
        return start(options -> options.setServiceName(serviceName));
    }

    public static GraftCodeAppInsightsConnectorPipeline start(Consumer<GraftCodeAppInsightsConnectorOptions> configure) {
        Objects.requireNonNull(configure, "configure");

        GraftCodeAppInsightsConnectorOptions options = new GraftCodeAppInsightsConnectorOptions();
        configure.accept(options);
        return start(options);
    }

    public static GraftCodeAppInsightsConnectorPipeline start(GraftCodeAppInsightsConnectorOptions options) {
        Objects.requireNonNull(options, "options");
        return new GraftCodeAppInsightsConnectorPipeline(options);
    }
}
