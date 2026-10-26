package com.graftcode.telemetry.appinsights;

import java.util.Objects;
import java.util.function.Consumer;

public final class GraftAppInsightsConnector {
    private GraftAppInsightsConnector() {
    }

    public static GraftAppInsightsConnectorPipeline start(String serviceName) {
        return start(options -> options.setServiceName(serviceName));
    }

    public static GraftAppInsightsConnectorPipeline start(Consumer<GraftAppInsightsConnectorOptions> configure) {
        Objects.requireNonNull(configure, "configure");

        GraftAppInsightsConnectorOptions options = new GraftAppInsightsConnectorOptions();
        configure.accept(options);
        return start(options);
    }

    public static GraftAppInsightsConnectorPipeline start(GraftAppInsightsConnectorOptions options) {
        Objects.requireNonNull(options, "options");
        return new GraftAppInsightsConnectorPipeline(options);
    }
}
