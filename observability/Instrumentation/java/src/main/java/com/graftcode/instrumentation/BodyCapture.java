package com.graftcode.instrumentation;

import com.hypertube.utils.ICommand;
import com.hypertube.utils.logging.LogsRedactor;

final class BodyCapture {
    static final String TRUNCATION_MARKER = "\"__truncated__\"";

    private static final int MAX_DEPTH = 24;

    private static final int CLOSING_COST_PER_LEVEL = 18;

    private static final int SCALAR_RESERVE = 32;

    private BodyCapture() {
    }

    static final class Result {
        final String body;
        final boolean truncated;

        Result(String body, boolean truncated) {
            this.body = body;
            this.truncated = truncated;
        }
    }

    static Result render(ICommand command, int maxCharacters) {
        if (command == null) {
            return null;
        }

        boolean[] truncated = new boolean[1];
        StringBuilder builder = new StringBuilder(Math.min(maxCharacters, 1024));
        writeCommand(builder, command, 0, maxCharacters, truncated);

        return new Result(LogsRedactor.redactBody(builder.toString()), truncated[0]);
    }

    private static boolean writeCommand(StringBuilder builder, ICommand command, int depth, int budget, boolean[] truncated) {
        builder.append("{\"runtime\":\"");
        builder.append(command.getRuntimeName());
        builder.append("\",\"type\":\"");
        builder.append(command.getCommandType());
        builder.append("\",\"payload\":[");

        Object[] payload = command.getPayload();
        boolean withinBudget = true;
        boolean hasContent = false;
        int localBudget = Math.max(0, budget - ((depth + 1) * CLOSING_COST_PER_LEVEL) - SCALAR_RESERVE);

        for (int i = 0; i < payload.length; i++) {
            if (builder.length() >= localBudget) {
                truncated[0] = true;
                withinBudget = false;
                break;
            }

            if (hasContent) {
                builder.append(',');
            }

            boolean followsSensitiveKey = i > 0
                    && payload[i - 1] instanceof String
                    && LogsRedactor.isSensitiveKey((String) payload[i - 1]);

            if (!writeValue(builder, payload[i], depth + 1, budget, localBudget, followsSensitiveKey, truncated)) {
                hasContent = true;
                withinBudget = false;
                break;
            }

            hasContent = true;
        }

        if (!withinBudget) {
            if (hasContent) {
                builder.append(',');
            }

            builder.append(TRUNCATION_MARKER);
        }

        builder.append("]}");
        return withinBudget;
    }

    private static boolean writeValue(
            StringBuilder builder,
            Object value,
            int depth,
            int budget,
            int localBudget,
            boolean forceRedact,
            boolean[] truncated) {
        if (value == null) {
            builder.append("null");
            return true;
        }

        if (depth >= MAX_DEPTH) {
            truncated[0] = true;
            builder.append(TRUNCATION_MARKER);
            return true;
        }

        if (value instanceof ICommand) {
            return writeCommand(builder, (ICommand) value, depth, budget, truncated);
        }

        if (value instanceof String) {
            return writeString(
                    builder,
                    forceRedact ? LogsRedactor.PLACEHOLDER : LogsRedactor.redactValue((String) value),
                    localBudget,
                    truncated);
        }

        if (value instanceof Boolean) {
            builder.append(((Boolean) value) ? "true" : "false");
            return true;
        }

        if (value instanceof Number) {
            builder.append(value);
            return true;
        }

        if (value instanceof byte[]) {
            builder.append("\"[bytes:").append(((byte[]) value).length).append("]\"");
            return true;
        }

        return writeString(
                builder,
                forceRedact ? LogsRedactor.PLACEHOLDER : LogsRedactor.redactValue(format(value)),
                localBudget,
                truncated);
    }

    private static String format(Object value) {
        try {
            return String.valueOf(value);
        } catch (RuntimeException ex) {
            return "[unrenderable:" + value.getClass().getName() + "]";
        }
    }

    private static boolean writeString(StringBuilder builder, String value, int budget, boolean[] truncated) {
        builder.append('"');

        if (value == null) {
            builder.append('"');
            return true;
        }

        for (int i = 0; i < value.length(); i++) {
            if (builder.length() >= budget) {
                truncated[0] = true;
                builder.append('"');
                return false;
            }

            char character = value.charAt(i);
            switch (character) {
                case '"':
                    builder.append("\\\"");
                    break;
                case '\\':
                    builder.append("\\\\");
                    break;
                case '\n':
                    builder.append("\\n");
                    break;
                case '\r':
                    builder.append("\\r");
                    break;
                case '\t':
                    builder.append("\\t");
                    break;
                default:
                    if (character < ' ') {
                        builder.append(String.format("\\u%04x", (int) character));
                    } else {
                        builder.append(character);
                    }

                    break;
            }
        }

        builder.append('"');
        return true;
    }
}
