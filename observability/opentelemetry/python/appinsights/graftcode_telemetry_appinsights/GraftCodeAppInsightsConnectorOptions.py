import os

from hypertube.utils.logging.LogsLevel import LogsLevel


class GraftCodeAppInsightsConnectorOptions:
    CONNECTION_STRING_VARIABLE = "APPLICATIONINSIGHTS_CONNECTION_STRING"

    def __init__(self, service_name="", connection_string=None, minimum_log_level=LogsLevel.Info,
                 traces_per_second=None, enable_console_logging=True):
        self.service_name = service_name
        self.connection_string = connection_string
        self.minimum_log_level = minimum_log_level
        self.traces_per_second = traces_per_second
        self.enable_console_logging = enable_console_logging
        self._environment = os.environ.get

    def resolve_connection_string(self):
        if not GraftCodeAppInsightsConnectorOptions._is_blank(self.connection_string):
            return self.connection_string

        environment_value = self._environment(GraftCodeAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE)
        return None if GraftCodeAppInsightsConnectorOptions._is_blank(environment_value) else environment_value

    def validate(self):
        if GraftCodeAppInsightsConnectorOptions._is_blank(self.service_name):
            raise ValueError("service_name must be configured for Azure Monitor telemetry.")

        if self.minimum_log_level is None:
            raise ValueError("minimum_log_level must not be None.")

        if self.traces_per_second is not None and not self.traces_per_second > 0:
            raise ValueError("traces_per_second must be greater than zero.")

    @staticmethod
    def _is_blank(value):
        return value is None or not str(value).strip()
