import pytest

from hypertube.utils.logging.HypertubeLogger import HypertubeLogger
from hypertube.utils.logging.LogsLevel import LogsLevel


@pytest.fixture(autouse=True)
def silence_console():
    previous = HypertubeLogger.minimum_level
    HypertubeLogger.minimum_level = LogsLevel.Off
    yield
    HypertubeLogger.minimum_level = previous
