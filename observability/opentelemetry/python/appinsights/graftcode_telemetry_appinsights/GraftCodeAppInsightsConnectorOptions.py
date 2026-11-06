from __future__ import annotations

import os
from typing import Callable, Optional, Union

from hypertube.utils.logging.LogsLevel import LogsLevel


class GraftCodeAppInsightsConnectorOptions:
    CONNECTION_STRING_VARIABLE = "APPLICATIONINSIGHTS_CONNECTION_STRING"

    def __init__(
            self,
            service_name: str = "",
            connection_string: Optional[str] = None,
            minimum_log_level: LogsLevel = LogsLevel.Info,
            traces_per_second: Optional[float] = None,
            enable_console_logging: bool = True,
    ) -> None:
        self.service_name = service_name
        self.connection_string = connection_string
        self.minimum_log_level = minimum_log_level
        self.traces_per_second = traces_per_second
        self.enable_console_logging = enable_console_logging
        self._environment: Callable[[str], Optional[str]] = os.environ.get

    def resolve_connection_string(self) -> Optional[str]:
        if not GraftCodeAppInsightsConnectorOptions._is_blank(self.connection_string):
            return self.connection_string

        environment_value = self._environment(GraftCodeAppInsightsConnectorOptions.CONNECTION_STRING_VARIABLE)
        return None if GraftCodeAppInsightsConnectorOptions._is_blank(environment_value) else environment_value

    def validate(self) -> None:
        if GraftCodeAppInsightsConnectorOptions._is_blank(self.service_name):
            raise ValueError("service_name must be configured for Azure Monitor telemetry.")

        if self.minimum_log_level is None:
            raise ValueError("minimum_log_level must not be None.")

        if self.traces_per_second is not None and not self.traces_per_second > 0:
            raise ValueError("traces_per_second must be greater than zero.")

    @staticmethod
    def _is_blank(value: Union[str, None]) -> bool:
        return value is None or not str(value).strip()
