using System;
using System.Globalization;
using System.Text;
using Hypertube.Netcore.Utils;
using Hypertube.Netcore.Utils.Logging;

namespace Graftcode.Instrumentation
{
    internal static class BodyCapture
    {
        internal const string TruncationMarker = "\"__truncated__\"";

        private const int MaxDepth = 24;

        private const int ClosingCostPerLevel = 18;

        private const int ScalarReserve = 32;

        internal static string? Render(ICommand? command, int maxCharacters, out bool truncated)
        {
            truncated = false;

            if (command == null)
            {
                return null;
            }

            var builder = new StringBuilder(Math.Min(maxCharacters, 1024));
            WriteCommand(builder, command, depth: 0, maxCharacters, ref truncated);

            return LogsRedactor.RedactBody(builder.ToString());
        }

        private static bool WriteCommand(
            StringBuilder builder,
            ICommand command,
            int depth,
            int budget,
            ref bool truncated)
        {
            builder.Append("{\"runtime\":\"");
            builder.Append(command.RuntimeName.ToString());
            builder.Append("\",\"type\":\"");
            builder.Append(command.CommandType.ToString());
            builder.Append("\",\"payload\":[");

            var payload = command.Payload;
            var withinBudget = true;
            var hasContent = false;
            var localBudget = Math.Max(
                0,
                budget - ((depth + 1) * ClosingCostPerLevel) - ScalarReserve);

            for (var i = 0; i < payload.Length; i++)
            {
                if (builder.Length >= localBudget)
                {
                    truncated = true;
                    withinBudget = false;
                    break;
                }

                if (hasContent)
                {
                    builder.Append(',');
                }

                var followsSensitiveKey = i > 0
                    && payload[i - 1] is string previousKey
                    && LogsRedactor.IsSensitiveKey(previousKey);

                if (!WriteValue(builder, payload[i], depth + 1, budget, localBudget, followsSensitiveKey, ref truncated))
                {
                    hasContent = true;
                    withinBudget = false;
                    break;
                }

                hasContent = true;
            }

            if (!withinBudget)
            {
                if (hasContent)
                {
                    builder.Append(',');
                }

                builder.Append(TruncationMarker);
            }

            builder.Append("]}");
            return withinBudget;
        }

        private static bool WriteValue(
            StringBuilder builder,
            object? value,
            int depth,
            int budget,
            int localBudget,
            bool forceRedact,
            ref bool truncated)
        {
            if (value == null)
            {
                builder.Append("null");
                return true;
            }

            if (depth >= MaxDepth)
            {
                truncated = true;
                builder.Append(TruncationMarker);
                return true;
            }

            if (value is ICommand nested)
            {
                return WriteCommand(builder, nested, depth, budget, ref truncated);
            }

            if (value is string text)
            {
                return WriteString(
                    builder,
                    forceRedact ? LogsRedactor.Placeholder : LogsRedactor.RedactValue(text),
                    localBudget,
                    ref truncated);
            }

            if (value is bool flag)
            {
                builder.Append(flag ? "true" : "false");
                return true;
            }

            if (IsNumeric(value))
            {
                builder.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
                return true;
            }

            if (value is byte[] bytes)
            {
                builder.Append("\"[bytes:");
                builder.Append(bytes.Length.ToString(CultureInfo.InvariantCulture));
                builder.Append("]\"");
                return true;
            }

            return WriteString(
                builder,
                forceRedact
                    ? LogsRedactor.Placeholder
                    : LogsRedactor.RedactValue(Format(value)),
                localBudget,
                ref truncated);
        }

        private static string? Format(object value)
        {
            try
            {
                return Convert.ToString(value, CultureInfo.InvariantCulture);
            }
            catch (Exception)
            {
                return "[unrenderable:" + value.GetType().FullName + "]";
            }
        }

        private static bool IsNumeric(object value)
        {
            return value is byte or sbyte or short or ushort or int or uint
                or long or ulong or float or double or decimal;
        }

        private static bool WriteString(StringBuilder builder, string? value, int budget, ref bool truncated)
        {
            builder.Append('"');

            if (value == null)
            {
                builder.Append('"');
                return true;
            }

            foreach (var character in value)
            {
                if (builder.Length >= budget)
                {
                    truncated = true;
                    builder.Append('"');
                    return false;
                }

                switch (character)
                {
                    case '"':
                        builder.Append("\\\"");
                        break;
                    case '\\':
                        builder.Append("\\\\");
                        break;
                    case '\n':
                        builder.Append("\\n");
                        break;
                    case '\r':
                        builder.Append("\\r");
                        break;
                    case '\t':
                        builder.Append("\\t");
                        break;
                    default:
                        if (character < ' ')
                        {
                            builder.Append("\\u");
                            builder.Append(((int)character).ToString("x4", CultureInfo.InvariantCulture));
                        }
                        else
                        {
                            builder.Append(character);
                        }

                        break;
                }
            }

            builder.Append('"');
            return true;
        }
    }
}
