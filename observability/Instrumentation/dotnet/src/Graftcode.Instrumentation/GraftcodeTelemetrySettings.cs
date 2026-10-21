using System;
using System.Globalization;
using System.Threading;

namespace Graftcode.Instrumentation
{
    public static class GraftcodeTelemetrySettings
    {
        internal const int MinimumBodyMaxChars = 256;
        internal const int DefaultBodyMaxChars = 8192;
        internal const int MaximumBodyMaxChars = 32768;

        private static int _autoInject = ReadFlag("HYPERTUBE_TELEMETRY_AUTO_INJECT", defaultValue: true) ? 1 : 0;
        private static int _captureBodies = ReadFlag("HYPERTUBE_TELEMETRY_CAPTURE_BODIES", defaultValue: false) ? 1 : 0;
        private static int _bodyMaxCharacters = ReadBodyMaxChars();

        public static bool IsAutoInjectEnabled
        {
            get => Volatile.Read(ref _autoInject) == 1;
            set => Volatile.Write(ref _autoInject, value ? 1 : 0);
        }

        public static bool IsBodyCaptureEnabled
        {
            get => Volatile.Read(ref _captureBodies) == 1;
            set => Volatile.Write(ref _captureBodies, value ? 1 : 0);
        }

        public static int BodyMaxCharacters
        {
            get => Volatile.Read(ref _bodyMaxCharacters);
            set => Volatile.Write(ref _bodyMaxCharacters, ClampBodyMaxCharacters(value));
        }

        internal static int ClampBodyMaxCharacters(int value)
        {
            if (value < MinimumBodyMaxChars)
            {
                return MinimumBodyMaxChars;
            }

            return value > MaximumBodyMaxChars ? MaximumBodyMaxChars : value;
        }

        private static int ReadBodyMaxChars()
        {
            var raw = Environment.GetEnvironmentVariable("HYPERTUBE_TELEMETRY_BODY_MAX_CHARS");
            if (string.IsNullOrWhiteSpace(raw)
                || !int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                return DefaultBodyMaxChars;
            }

            return ClampBodyMaxCharacters(parsed);
        }

        private static bool ReadFlag(string name, bool defaultValue)
        {
            var value = Environment.GetEnvironmentVariable(name);
            if (string.IsNullOrWhiteSpace(value))
            {
                return defaultValue;
            }

            return !string.Equals(value, "false", StringComparison.OrdinalIgnoreCase)
                && !string.Equals(value, "0", StringComparison.OrdinalIgnoreCase);
        }
    }
}
