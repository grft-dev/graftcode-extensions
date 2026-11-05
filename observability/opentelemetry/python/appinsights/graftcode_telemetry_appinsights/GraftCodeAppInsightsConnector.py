from graftcode_telemetry_appinsights.GraftCodeAppInsightsConnectorOptions import GraftCodeAppInsightsConnectorOptions
from graftcode_telemetry_appinsights.GraftCodeAppInsightsConnectorPipeline import GraftCodeAppInsightsConnectorPipeline


class GraftCodeAppInsightsConnector:

    @staticmethod
    def start(service_name_or_configure):
        if service_name_or_configure is None:
            raise ValueError("start requires a service name, a configure callback or options")

        if isinstance(service_name_or_configure, GraftCodeAppInsightsConnectorOptions):
            return GraftCodeAppInsightsConnectorPipeline(service_name_or_configure)

        options = GraftCodeAppInsightsConnectorOptions()
        if callable(service_name_or_configure):
            service_name_or_configure(options)
        else:
            options.service_name = service_name_or_configure

        return GraftCodeAppInsightsConnectorPipeline(options)
